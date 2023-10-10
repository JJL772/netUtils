/**
 * ICMP ping utility/library
 */


#pragma once

#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ping_stats {
	float minTime;
	float maxTime;
	float avgTime;
	int64_t sent;
	int64_t lost;
	int64_t corrupted;
};

enum LogType {
	PING_LOG_NONE = 0,
	PING_LOG_MINIMAL,
	PING_LOG_FULL
};

#define MAX_PING_PAYLOAD_SIZE 65500 /* Good enough... */

struct ping_opts {
	const char* addr;
	double interval;
	float send_timeout;
	float read_timeout;
	int64_t num_packets; /* -1 = infinite */
	int log_type;
	int progress; /* For use with PING_LOG_MINIMAL, every `progress` packets, display status */
	uint8_t pattern;
	uint16_t payload_size;
};

/* Fill ping_opts struct with defaults */
void icmp_ping_defaults(struct ping_opts* opts);

bool icmp_ping_cmd(int argc, char** argv);

bool icmp_ping(const struct ping_opts* opts, struct ping_stats* stats);

#ifdef __cplusplus
}
#endif