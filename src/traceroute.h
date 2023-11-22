/**
 * traceroute utility/library
 */
#pragma once

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

enum TracerouteLog {
	TR_LOG_NONE,
	TR_LOG_FULL,
	TR_LOG_VERBOSE
};

struct traceroute_opts {
	struct sockaddr_in ip;
	int max_hops;		/* Max number of hops */
	int log_type;
};

struct traceroute_node {
	char addr[64];
	in_addr_t in_addr;
	struct traceroute_node* next;
};

struct traceroute_result {
	int hops;
	struct traceroute_node* first;
};

void traceroute_opts_init(struct traceroute_opts* opts);

void traceroute_cmd(int argc, char** argv);

bool traceroute(const struct traceroute_opts* opts, struct traceroute_result** result);

void traceroute_result_free(struct traceroute_result* result);

#ifdef __cplusplus
}
#endif