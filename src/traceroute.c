/**
 * traceroute.c -- Simple traceroute utility and library
 */
#include <memory.h>

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
#include <netdb.h>

#include "iputils.h"

#include <memory.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "traceroute.h"
#include "getopt_s.h"

#ifdef __rtems__
#	define ICMP_TIME_EXCEEDED ICMP_TIMXCEED
#endif

struct traceroute_ctx {
	int fd;
	struct sockaddr_in local;
	uint16_t ident;
};

struct __attribute__((packed)) tr_packet {
	struct icmp icmp_packet;
	uint32_t sec;
	uint32_t nsec;
};

static bool _tr_open(const struct traceroute_opts* opts, struct traceroute_ctx* ctx);
static ssize_t _tr_make_ip_frame(const struct traceroute_opts* opts, const struct traceroute_ctx* ctx, struct ip* ipf, uint8_t ttl, size_t datalen);
static void _tr_make_icmp(const struct traceroute_ctx* ctx, struct tr_packet* packet);
static void traceroute_help();

#if EPICS

#include <iocsh.h>
#include <epicsExport.h>
#include <epicsStdlib.h>

static void traceroute_iocsh(const iocshArgBuf* args);

void register_traceroute() {
	static const iocshArg arg0 = {"args", iocshArgArgv};
	static const iocshArg* args[] = {&arg0};
	static const iocshFuncDef func = {"traceroute", 1, args};
	iocshRegister(&func, traceroute_iocsh);
}

epicsExportRegistrar(register_traceroute);

static void traceroute_iocsh(const iocshArgBuf* args) {
	traceroute_cmd(args->aval.ac, args->aval.av);
}
#endif

void traceroute_cmd(int argc, char** argv) {
	getopt_state_t st;
	getopt_state_init(&st);

	struct traceroute_opts opts;
	traceroute_opts_init(&opts);

	int opt;
	while ((opt = getopt_s(argc, argv, "n:hv", &st)) != -1) {
		switch(opt) {
		case 'n':
			opts.max_hops = atoi(st.optarg);
			break;
		case 'v':
			opts.log_type = TR_LOG_VERBOSE;
			break;
		case 'h':
			traceroute_help();
			return;
		default:
			break;
		}
	}

	if (st.optind >= argc) {
		traceroute_help();
		return;
	}

	opts.ip.sin_addr.s_addr = inet_addr(argv[st.optind]);
	opts.ip.sin_port = 0;
	opts.ip.sin_family = AF_INET;

	struct traceroute_result* result = NULL;
	traceroute(&opts, &result);
	traceroute_result_free(result);
}

static void traceroute_help() {
	printf("Usage: traceroute [-n max_hops] addr\n");
}

void traceroute_opts_init(struct traceroute_opts* opts) {
	memset(opts, 0, sizeof(*opts));
	opts->log_type = TR_LOG_FULL;
	opts->max_hops = 128;
}

void traceroute_result_free(struct traceroute_result* result) {
	if (!result)
		return;

	for (struct traceroute_node* n = result->first; n;) {
		struct traceroute_node* o = n;
		n = n->next;
		free(o);
	}
	free(result);
}

bool traceroute(const struct traceroute_opts* opts, struct traceroute_result** resptr) {
	struct traceroute_ctx ctx;
	if (!_tr_open(opts, &ctx))
		return false;

	const bool quiet = opts->log_type < TR_LOG_FULL;
	const bool verbose = opts->log_type == TR_LOG_VERBOSE;

	struct traceroute_result* result = (struct traceroute_result*)calloc(1, sizeof(struct traceroute_result));
	result->first = NULL;
	result->hops = 0;
	struct traceroute_node* last = NULL;

	int hops = opts->max_hops, retries = 10;
	uint8_t ttl = 1;
	while(hops > 0) {
		/* We've retried too many times, probably lost our connection :( */
		if (retries <= 0) {
			if (!quiet)
				printf("Max retries exceeded, exiting..\n");
			break;
		}

		char data[4096];

		/* Build IP frame + ICMP payload */
		const ssize_t len = _tr_make_ip_frame(opts, &ctx, (struct ip*)data, ttl, sizeof(struct icmp));
		_tr_make_icmp(&ctx, (struct tr_packet*)(data + sizeof(struct ip)));

		if (sendto(ctx.fd, data, len, 0, (struct sockaddr*)&opts->ip, sizeof(opts->ip)) < len) {
			if (!quiet)
				perror("Send failed");
			if (verbose)
				printf("Retry %d...\n", retries);
			retries--;
			continue;
		}

		/* Listen for the reply */
		struct sockaddr_in fromaddr;
		socklen_t fromlen = sizeof(fromaddr);
		ssize_t recv;
	recvagain:
		if ((recv = recvfrom(ctx.fd, data, len, 0, (struct sockaddr*)&fromaddr, &fromlen))) {
			struct ip* hdr = (struct ip*)data;
			struct icmp* packet = (struct icmp*)(data + hdr->ip_hl * 4);

			/* Failure, we'll retry */
			if (recv < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					goto done; /* Increase ttl, send again */
				if (!quiet)
					perror("Recv failed, packet lost");
				if (verbose)
					printf("Retry %d...\n", retries);
				retries--;
				continue;
			}

			/* Probably not our data! */
			if (recv < sizeof(struct ip) + sizeof(uint64_t))
				goto recvagain;

			/* Check if it's actually ours and what we expect... */
			if (packet->icmp_type != ICMP_ECHOREPLY && packet->icmp_type != ICMP_TIME_EXCEEDED && packet->icmp_hun.ih_idseq.icd_id != ctx.ident)
				goto recvagain;

			struct traceroute_node* n = (struct traceroute_node*)calloc(1, sizeof(struct traceroute_node));
			if (last)
				last->next = n;
			last = n;
			if (!result->first)
				result->first = last;
			++result->hops;

			last->in_addr = fromaddr.sin_addr.s_addr;
			strncpy(last->addr, inet_ntoa(fromaddr.sin_addr), sizeof(last->addr)-1);

			if (!quiet)
				printf("%2d %s\n", result->hops, last->addr);

			/* If we get ECHOREPLY, we've reached out destination and are finished */
			if (packet->icmp_code == ICMP_ECHOREPLY && fromaddr.sin_addr.s_addr == opts->ip.sin_addr.s_addr)
				break;
		}
	done:
		--hops;
		retries = 10;
		++ttl;
	}

	close(ctx.fd);
	if (hops > 0)
		*resptr = result;
	else
		free(result);
	return hops > 0;
}

static bool _tr_open(const struct traceroute_opts* opts, struct traceroute_ctx* ctx) {
	const bool quiet = opts->log_type < TR_LOG_FULL;
	socklen_t sockl;
	int opt = 1;

	ctx->fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (ctx->fd < 0) {
		if (!quiet)
			perror("Socket creation failed");
		return false;
	}

	ctx->ident = 5930;

	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	if (setsockopt(ctx->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		if (!quiet)
			perror("Failed to set SO_RCVTIMEO");
		goto error;
	}

	if (setsockopt(ctx->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
		if (!quiet)
			perror("Failed to set SO_SNDTIMEO");
		goto error;
	}

	/* Enable HDRINCL because we need to tweak the IP header */
	opt = 1;
	if (setsockopt(ctx->fd, IPPROTO_IP, IP_HDRINCL, &opt, sizeof(opt)) < 0) {
		if (!quiet)
			perror("Failed to set IP_HDRINCL");
		goto error;
	}

	/* Determine local address so we can build an IP frame */
	if (getsockname(ctx->fd, (struct sockaddr*)&ctx->local, &sockl) < 0) {
		if (!quiet)
			perror("Could not determine local address");
		goto error;
	}

	return true;

error:
	close(ctx->fd);
	return false;
}

/* Build an IP frame, returns the length of the entire packet */
static ssize_t _tr_make_ip_frame(const struct traceroute_opts* opts, const struct traceroute_ctx* ctx, struct ip* ipf, uint8_t ttl, size_t datalen) {
	ipf->ip_dst = opts->ip.sin_addr;
	ipf->ip_v = IPVERSION;
	ipf->ip_tos = 0; /* Type of service should just be normal... */
	ipf->ip_id = ctx->ident;
	ipf->ip_p = IPPROTO_ICMP;
	ipf->ip_src = ctx->local.sin_addr;
	ipf->ip_ttl = ttl;
	ipf->ip_hl = sizeof(*ipf) / 4;
	ipf->ip_len = datalen + sizeof(*ipf);
	ipf->ip_off = 0;
	ipf->ip_sum = 0;
	ipf->ip_sum = ip_cksum(ipf, sizeof(*ipf));
	return ipf->ip_len;
}


static void _tr_make_icmp(const struct traceroute_ctx* ctx, struct tr_packet* packet) {
	memset(packet, 0, sizeof(*packet));
    packet->icmp_packet.icmp_type = ICMP_ECHO;
    packet->icmp_packet.icmp_code = 0;
    packet->icmp_packet.icmp_hun.ih_idseq.icd_id = ctx->ident;
    packet->icmp_packet.icmp_hun.ih_idseq.icd_seq = 0;

	struct timespec sentat = time_now();

    packet->sec = sentat.tv_sec;
    packet->nsec = sentat.tv_nsec;
	packet->icmp_packet.icmp_cksum = 0;

    packet->icmp_packet.icmp_cksum = ip_cksum(&packet->icmp_packet, sizeof(packet->icmp_packet));
}

#ifdef TRACEROUTE_MAIN
int main(int argc, char** argv) {
	traceroute_cmd(argc, argv);
}
#endif
