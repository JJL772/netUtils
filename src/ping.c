/**
 * ICMP ping utility for EPICS
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in_systm.h>
#include <net/if.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include <memory.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "iputils.h"
#include "getopt_s.h"
#include "ping.h"

#ifndef EPICS
#define epicsThreadSleep(x) usleep(x * 1e6)
#else
#include <epicsThread.h>
#endif

// Why on earth is this missing from RTEMS??? Not in limits.h or stdint.h????
#ifndef UINT64_MAX
#	define UINT64_MAX 0xffffffffffffffffULL
#endif

// Not available on RTEMS either, but that's okay...
#ifndef MSG_DONTWAIT
#	define MSG_DONTWAIT 0
#endif

#if defined(__rtems__)
#	define USE_RAW_SOCK 1
#endif

#define PING_PAYLOAD_SIZE 64

struct ping_ctx {
	int fd;
	struct sockaddr_in addr;
};

struct __attribute__((packed)) ping_packet {
    struct icmp icmp;
    uint32_t sec;
    uint32_t nsec;
    char payload[PING_PAYLOAD_SIZE];
};

static bool _icmp_validate(struct ping_packet* packet);
static void _generate_payload(void* payload, size_t size);
static void _generate_packet(struct ping_packet* packet, uint16_t seq, uint16_t ident);

#ifdef EPICS

#include <iocsh.h>
#include <epicsExport.h>

static void ping(const iocshArgBuf* args);

static void register_icmp() {
    static iocshArg arg0 = {"args", iocshArgArgv};
    static iocshArg* args[] = {&arg0};
    static iocshFuncDef func = {"ping", 1, args};
    iocshRegister(&func, ping);
}

epicsExportRegistrar(register_icmp);

static void ping(const iocshArgBuf* args) {
	icmp_ping_cmd(args->aval.ac, args->aval.av);
}

#endif


static struct timeval sec_to_tv(double ms) {
	double us = fmod(ms, 1000) * 1e3f;
	struct timeval tv;
	tv.tv_sec = round(ms / 1000.f);
	tv.tv_usec = us;
	return tv;
}

static bool _ping_open(const char* addr, const struct ping_opts* opts, struct ping_ctx* p) {
#ifdef USE_RAW_SOCK
    p->fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
#else
    p->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
#endif
    if (p->fd < 0) {
        perror("Socket creation failed");
	#if __linux__
		if (errno == EPERM) {
			printf("Permissions issues on Linux may be fixed by allowing ICMP for all users:\n sysctl -w net.ipv4.ping_group_range=\"0 2147483647\"\n");
		}
	#endif
        return false;
    }
    memset(&p->addr, 0, sizeof(p->addr));
    p->addr.sin_family = AF_INET;
    p->addr.sin_port = 5555;
    p->addr.sin_addr.s_addr = inet_addr(addr);

	struct timeval tv = sec_to_tv(opts->read_timeout);
	if (setsockopt(p->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		perror("Failed to set SO_RCVTIMEO");
		close(p->fd);
		return false;
	}

	tv = sec_to_tv(opts->send_timeout);
	if (setsockopt(p->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
		perror("Failed to set SO_SNDTIMEO");
		close(p->fd);
		return false;
	}

    return true;
}

static void ping_help() {
	printf("Usage: ping [-c count] [-i interval] [-p progress interval] [-q] ADDR\n");
}

static inline uint16_t _cksum(const void* hdr, size_t size) {
    uint16_t* p = (uint16_t*)hdr;
    assert(size % 2 == 0);
    size /= 2;
    uint16_t r = 0;
    for (size_t n = 0; n < size; ++n)
        r = ones_sum(r, p[n]);
    return ~r;
}

void icmp_ping_defaults(struct ping_opts* opts) {
	memset(opts, 0, sizeof(*opts));
	opts->num_packets = 5;
	opts->interval = 1;
	opts->log_type = PING_LOG_FULL;
	opts->read_timeout = 0.01f; /* 10ms */
	opts->send_timeout = 5;
}

bool icmp_ping_cmd(int argc, char** argv) {
	struct ping_opts opts;
	icmp_ping_defaults(&opts);
	
    int opt;
    getopt_state_t st;
    while ((opt = getopt_s(argc, argv, "i:c:qp:h", &st)) != -1) {
        switch(opt) {
        case 'i':
            opts.interval = atof(st.optarg);
            break;
        case 'c':
        {
            int n = atoi(st.optarg);
            if (n <= 0)
                opts.num_packets = UINT64_MAX;
            else
                opts.num_packets = n;
            break;
        }
        case 'p':
            opts.progress = atoi(st.optarg);
            break;
        case 'h':
            ping_help();
            break;
        case 'q':
            opts.log_type = PING_LOG_NONE;
            break;
        }
    }

	if (st.optind >= argc) {
		ping_help();
		return false;
	}

    opts.addr = argv[st.optind];

	struct ping_stats stats;
	return icmp_ping(&opts, &stats);
}

bool icmp_ping(const struct ping_opts* opts, struct ping_stats* stats) {

	memset(stats, 0, sizeof(*stats));

    struct ping_ctx ctx;
    if (!_ping_open(opts->addr, opts, &ctx))
        return false;

    const uint16_t ident = 9239;
    int lastseq = -1;

	const bool quiet = opts->log_type < PING_LOG_FULL;
    const bool silent = opts->log_type < PING_LOG_MINIMAL;

    uint64_t seq;
    for (seq = 0; seq < opts->num_packets; ++seq) {
        struct ping_packet msg;
        _generate_packet(&msg, seq, ident);

        if (sendto(ctx.fd, &msg, sizeof(msg), 0, (struct sockaddr*)&ctx.addr, sizeof(ctx.addr)) < 0) {
            if (!quiet)
                perror("failed");
        }
        stats->lost++;

        struct timespec now = time_now();

        // Recv some ICMP packets, accounting for some out of order delivery
        for (int rp = 0; rp < 10; ++rp) {
            struct sockaddr_in fromaddr;
            socklen_t fromsize = sizeof(fromaddr);
            ssize_t ret;

            // Recv logic is a bit more convoluted when using SOCK_RAW. We recv a full IP frame instead of just the payload
            char data[4096];
            struct ip* ipf = (struct ip*)data;

            if ((ret = recvfrom(ctx.fd, data, sizeof(data), MSG_DONTWAIT, (struct sockaddr*)&fromaddr, &fromsize)) < (ssize_t)(sizeof(struct ping_packet))) {
                // No more data left, bail out!
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                if (!quiet)
                    perror("Malformed ICMP payload");
                continue;
            }

            struct ping_packet* rmsg = 
        #if USE_RAW_SOCK
                (struct ping_packet*)(data + (ipf->ip_hl * 4)); /* hl = number of 32-bit words in header */
        #else
                (struct ping_packet*)data;
        #endif

            // Filter out any non-echo requests, or messages we don't own
            if (rmsg->icmp.icmp_type != ICMP_ECHOREPLY /*|| msg.icmp.icmp_hun.ih_idseq.icd_id != ident*/)
                continue;

            // Validate ICMP packet
            if (!_icmp_validate(rmsg)) {
                if (!silent)
                    printf("malformed ICMP packet with SEQ %d!\n", rmsg->icmp.icmp_hun.ih_idseq.icd_seq);
                ++stats->corrupted;
                continue;
            }

            struct timespec ts = {rmsg->sec, rmsg->nsec};
            float diffms = time_diff(&now, &ts) * 1000.f;
            stats->avgTime = ((seq+1) * stats->avgTime + diffms) / (seq+2);
            stats->minTime = diffms < stats->minTime ? diffms : stats->minTime;
            stats->maxTime = diffms > stats->maxTime ? diffms : stats->maxTime;

            if (!quiet)
                printf("%ld bytes from %s: icmp_seq=%lu time=%.2f ms %s%s\n", 
                    (long int)(ret - ipf->ip_hl * 4), inet_ntoa(fromaddr.sin_addr), (long unsigned)seq, diffms,
                    (lastseq != (int)(rmsg->icmp.icmp_hun.ih_idseq.icd_seq) - 1) ? "(OUT OF ORDER)" : "",
                    stats->lost == 0 ? "(DUP)" : "");

            lastseq = rmsg->icmp.icmp_hun.ih_idseq.icd_seq;
            --stats->lost;
        }

        // Display progress every so often
        if (!silent && quiet && opts->progress > 0 && seq % opts->progress == 0) {
            printf("%llu sent so far, %lli in-flight (or lost), %lli corrupted, icmp_seq=%llu\n",
                (long long unsigned)seq+1, (long long)stats->lost, (long long)stats->corrupted, (long long unsigned)seq);
            printf("  min=%.2f ms, max=%.2f ms, avg=%.2f ms\n", stats->minTime, stats->maxTime, stats->avgTime);
        }

        epicsThreadSleep(opts->interval);
    }

    // Print stats
    if (!silent) {
        printf("%llu packets transmitted, %llu received, %lld corrupted, %.2f%% packet loss\n", (long long unsigned)seq, 
            (long long unsigned)seq - stats->lost, (long long)stats->corrupted, ((float)(stats->corrupted) / (float)(seq)) * 100.f);
        printf("min=%.2f ms, max=%.2f ms, avg=%.2f ms\n", stats->minTime, stats->maxTime, stats->avgTime);
    }
    return stats->corrupted == 0 && stats->lost == 0;
}

static bool _icmp_validate(struct ping_packet* packet) {
    int sum = packet->icmp.icmp_cksum;
    packet->icmp.icmp_cksum = 0;
    const uint16_t actualSum = _cksum(packet, sizeof(*packet));
    packet->icmp.icmp_cksum = sum;

    if (sum != actualSum) {
        printf("validation failed for icmp_seq=%d: packet checksum=0x%X, computed checksum=0x%X\n",
            packet->icmp.icmp_hun.ih_idseq.icd_seq, packet->icmp.icmp_cksum, sum);
    }

    return sum == actualSum;
}

static void _generate_payload(void* payload, size_t size) {
    static uint8_t pattern = 0xF0;
    pattern = ~pattern;
    memset(payload, pattern, size);
}

static void _generate_packet(struct ping_packet* msg, uint16_t seq, uint16_t ident) {
    memset(msg, 0, sizeof(*msg));
    msg->icmp.icmp_type = ICMP_ECHO;
    msg->icmp.icmp_code = 0;
    msg->icmp.icmp_hun.ih_idseq.icd_id = ident;
    msg->icmp.icmp_hun.ih_idseq.icd_seq = seq;
    _generate_payload(msg->payload, sizeof(msg->payload));

    struct timespec sentat = time_now();

    msg->sec = sentat.tv_sec;
    msg->nsec = sentat.tv_nsec;

    msg->icmp.icmp_cksum = _cksum(&msg, sizeof(*msg));
}

#ifdef INCLUDE_MAIN

int main(int argc, char** argv) {
    return icmp_ping_cmd(argc, argv) ? 0 : 1;
}

#endif
