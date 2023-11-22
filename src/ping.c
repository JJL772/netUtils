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
#define epicsStdoutPrintf printf
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
#define USE_RAW_SOCK 1

struct ping_ctx {
	int fd;
	struct sockaddr_in addr;
};

struct __attribute__((packed)) ping_packet {
    struct icmp icmp;
    uint32_t sec;
    uint32_t nsec;
    char payload[];
};

static bool _icmp_validate(const struct ping_opts* opts, struct ping_packet* packet, ssize_t recv_size);
static void _generate_packet(const struct ping_opts* opts, struct ping_packet* packet, uint16_t seq, uint16_t ident);

#ifdef EPICS

#include <iocsh.h>
#include <epicsExport.h>

static void ping(const iocshArgBuf* args);

static void register_icmp() {
    static iocshArg arg0 = {"args", iocshArgArgv};
    static const iocshArg* const args[] = {&arg0};
    static iocshFuncDef func = {"ping", 1, args};
    iocshRegister(&func, ping);
}

epicsExportRegistrar(register_icmp);

static void ping(const iocshArgBuf* args) {
	icmp_ping_cmd(args->aval.ac, args->aval.av);
}

#endif


static struct timeval ms_to_tv(double ms) {
	double us = fmod(ms, 1000) * 1e3f;
	struct timeval tv;
	tv.tv_sec = floor(ms / 1000.f);
	tv.tv_usec = us;
	return tv;
}

static bool _ping_open(in_addr_t addr, const struct ping_opts* opts, struct ping_ctx* p) {
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
    p->addr.sin_addr.s_addr = addr;

	struct timeval tv = ms_to_tv(opts->read_timeout * 1e3);
	if (setsockopt(p->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		perror("Failed to set SO_RCVTIMEO");
		close(p->fd);
		return false;
	}

	tv = ms_to_tv(opts->send_timeout * 1e3);
	if (setsockopt(p->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
		perror("Failed to set SO_SNDTIMEO");
		close(p->fd);
		return false;
	}

    return true;
}

static void ping_help() {
	printf("Usage: ping [-c count] [-i interval] [-s payload size] [-p pattern] [-l progress interval] [-q] ADDR\n");
}

void icmp_ping_opts_init(struct ping_opts* opts) {
	memset(opts, 0, sizeof(*opts));
	opts->num_packets = 5;
	opts->interval = 1;
	opts->log_type = PING_LOG_FULL;
	opts->read_timeout = 0.5; /* 500ms */
	opts->send_timeout = 5;
    opts->pattern = 0x0;
    opts->payload_size = 64;
}

bool icmp_ping_cmd(int argc, char** argv) {
	struct ping_opts opts;
	icmp_ping_opts_init(&opts);
	
    int opt;
    getopt_state_t st;
    getopt_state_init(&st);
    while ((opt = getopt_s(argc, argv, "i:c:ql:hp:s:", &st)) != -1) {
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
        case 'l':
            opts.progress = atoi(st.optarg);
            break;
        case 'h':
            ping_help();
            return false;
        case 's':
            opts.payload_size = atoi(st.optarg);
            if (opts.payload_size > MAX_PING_PAYLOAD_SIZE) {
                printf("-s is too large, needs to be less than %d\n", MAX_PING_PAYLOAD_SIZE);
                ping_help();
                return false;
            }
            break;
        case 'p':
            opts.pattern = strtol(st.optarg, NULL, 16);
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

    opts.addr = inet_addr(argv[st.optind]);

    struct in_addr a = {opts.addr};
    printf("PING %s %d (%zu) bytes of data, pattern %d\n", inet_ntoa(a), opts.payload_size, opts.payload_size + sizeof(struct ping_packet), opts.pattern);

	struct ping_stats stats;
	return icmp_ping(&opts, &stats);
}

bool icmp_ping(const struct ping_opts* opts, struct ping_stats* stats) {

	memset(stats, 0, sizeof(*stats));
    stats->minTime = 999999;

    struct ping_ctx ctx;
    if (!_ping_open(opts->addr, opts, &ctx))
        return false;

    const uint16_t ident = 9239;
    int lastseq = -1;
    int finaltries = 15;

	const bool quiet = opts->log_type < PING_LOG_FULL;
    const bool silent = opts->log_type < PING_LOG_MINIMAL;
    int64_t packetIdx = 0;

    uint64_t seq;
    for (seq = 0; seq < opts->num_packets; ++seq) {
        union {
            struct ping_packet msg;
            char raw[65535];
        } m;

        const size_t packet_size = sizeof(struct ping_packet) + opts->payload_size;
        _generate_packet(opts, &m.msg, seq, ident);

        if (sendto(ctx.fd, &m.msg, packet_size, 0, (struct sockaddr*)&ctx.addr, sizeof(ctx.addr)) < 0) {
            if (!quiet)
                perror("failed");
        }
        stats->lost++;
        stats->sent++;

        // Store time, so we can compute how long to sleep for
        struct timespec recv_start = time_now();

        // Recv some ICMP packets, accounting for some out of order delivery
recvagain:
        while(1) {

            struct timespec recv_now = time_now();
            if (time_diff(&recv_now, &recv_start) >= opts->interval) {
                break;
            }

            struct sockaddr_in fromaddr;
            socklen_t fromsize = sizeof(fromaddr);
            ssize_t ret;

            // Recv logic is a bit more convoluted when using SOCK_RAW. We recv a full IP frame instead of just the payload
            struct ip* ipf = (struct ip*)m.raw;

            const size_t recv_size = packet_size + sizeof(struct ip);
            if ((ret = recvfrom(ctx.fd, m.raw, recv_size, 0, (struct sockaddr*)&fromaddr, &fromsize)) < (ssize_t)(sizeof(struct ping_packet))) {
                // No more data left, bail out!
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                continue;
            }

        #ifdef USE_RAW_SOCK
            ret -= sizeof(struct ip);
        #endif

            // Certain servers may be configured to truncate ICMP requests above a certain size (i.e. google.com)
            int trunc = ret < sizeof(struct ping_packet);

            struct timespec now = time_now();

            struct ping_packet* rmsg = 
        #if USE_RAW_SOCK
                (struct ping_packet*)(m.raw + (ipf->ip_hl * 4)); /* hl = number of 32-bit words in header */
        #else
                (struct ping_packet*)m.raw;
        #endif

            // Filter out any non-echo requests, or messages we don't own
            if (rmsg->icmp.icmp_type != ICMP_ECHOREPLY || rmsg->icmp.icmp_hun.ih_idseq.icd_id != ident)
                continue;

            // Validate ICMP packet
            if (!_icmp_validate(opts, rmsg, ret)) {
                if (!silent)
                    printf("malformed ICMP packet with SEQ %d!\n", rmsg->icmp.icmp_hun.ih_idseq.icd_seq);
                ++stats->corrupted;
                continue;
            }

            struct timespec ts = {rmsg->sec, rmsg->nsec};
            float diffms = time_diff(&now, &ts) * 1000.f;
            stats->avgTime = ((packetIdx) * stats->avgTime + diffms) / (packetIdx+1);
            stats->minTime = diffms < stats->minTime ? diffms : stats->minTime;
            stats->maxTime = diffms > stats->maxTime ? diffms : stats->maxTime;
            packetIdx++;

            if (!quiet)
                printf("%ld bytes from %s: icmp_seq=%d time=%.2f ms %s%s%s\n", 
                    (long int)(ret - ipf->ip_hl * 4), inet_ntoa(fromaddr.sin_addr), rmsg->icmp.icmp_hun.ih_idseq.icd_seq, diffms,
                    (lastseq != (int)(rmsg->icmp.icmp_hun.ih_idseq.icd_seq) - 1) ? "(OUT OF ORDER)" : "",
                    stats->lost == 0 ? "(DUP)" : "", trunc ? "(TRUNC)" : "");

            lastseq = rmsg->icmp.icmp_hun.ih_idseq.icd_seq;
            --stats->lost;
        }

        // Display progress every so often
        if (!silent && quiet && opts->progress > 0 && seq % opts->progress == 0) {
            printf("%llu sent so far, %lli in-flight (or lost), %lli corrupted, icmp_seq=%llu\n",
                (long long unsigned)seq+1, (long long)stats->lost, (long long)stats->corrupted, (long long unsigned)seq);
            printf("  min=%.2f ms, max=%.2f ms, avg=%.2f ms\n", stats->minTime, stats->maxTime, stats->avgTime);
        }

        struct timespec recv_end = time_now();

        // We're about to exit, but we could still have packets in-flight! Wait for them
        if (stats->lost && seq == opts->num_packets-1 && finaltries-- > 0) {
            epicsThreadSleep(0.001);
            goto recvagain;
        }

        double to_sleep = opts->interval - time_diff(&recv_end, &recv_start);
        if (to_sleep > 0 && opts->num_packets-1 != seq)
            epicsThreadSleep(to_sleep);
    }

    close(ctx.fd);

    // Print stats
    if (!silent) {
        printf("%llu packets transmitted, %llu received, %lld corrupted, %.2f%% packet loss\n", (long long unsigned)seq, 
            (long long unsigned)seq - stats->lost, (long long)stats->corrupted, 100.f * ((float)(stats->lost) / seq));
        printf("min=%.2f ms, max=%.2f ms, avg=%.2f ms\n", stats->minTime, stats->maxTime, stats->avgTime);
    }
    return stats->corrupted == 0 && stats->lost == 0;
}

static bool _icmp_validate(const struct ping_opts* opts, struct ping_packet* packet, ssize_t recv_size) {
    uint16_t sum = packet->icmp.icmp_cksum;
    packet->icmp.icmp_cksum = 0;
    const uint16_t actualSum = ip_cksum(packet, recv_size);

    if (sum != actualSum) {
        printf("bad checksum for icmp_seq=%d: packet checksum=0x%X, expected checksum=0x%X\n",
            packet->icmp.icmp_hun.ih_idseq.icd_seq, sum, actualSum);
    }

    packet->icmp.icmp_cksum = sum;

    const ssize_t toCheck = recv_size - sizeof(struct ping_packet);

    bool ok = true;
    for (int i = 0; i < toCheck; ++i) {
        if ((char)opts->pattern != packet->payload[i]) {
            printf("Data failed to validate, expected %d at byte %d, but got %d insteaad!\n", opts->pattern, i, packet->payload[i]);
            ok = false;
        }
    }

    return sum == actualSum && ok;
}

static void _generate_packet(const struct ping_opts* opts, struct ping_packet* msg, uint16_t seq, uint16_t ident) {
    memset(msg, 0, sizeof(*msg));
    msg->icmp.icmp_type = ICMP_ECHO;
    msg->icmp.icmp_code = 0;
    msg->icmp.icmp_hun.ih_idseq.icd_id = ident;
    msg->icmp.icmp_hun.ih_idseq.icd_seq = seq;
    memset(msg->payload, opts->pattern, opts->payload_size);

    struct timespec sentat = time_now();

    msg->sec = sentat.tv_sec;
    msg->nsec = sentat.tv_nsec;

    msg->icmp.icmp_cksum = ip_cksum(msg, sizeof(*msg) + opts->payload_size);
}

#ifdef PING_MAIN

int main(int argc, char** argv) {
    return icmp_ping_cmd(argc, argv) ? 0 : 1;
}

#endif
