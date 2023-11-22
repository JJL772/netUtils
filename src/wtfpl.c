/* WTFPL: Where's The Freakin' Packet Loss?!? */
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <stdint.h>
#include <time.h>

#include <netinet/in_systm.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>

#include "ping.h"
#include "traceroute.h"
#include "iputils.h"
#include "getopt_s.h"

typedef struct wtfpl_node {
	in_addr_t addr;
	struct wtfpl_node* next;
	int sent;
	int lost;
	float pl; /* Packet loss percentage (i.e. 1.0 for 100%) */
} wtfpl_node_t;

typedef struct wtfpl_result {
	in_addr_t suspect;
	struct wtfpl_node* first; /* In order of test */
} wtfpl_result_t;

typedef struct wtfpl_opts {
	in_addr_t addr;
	int samples;	/* How many packets we trace to each host */
} wtfpl_opts_t;

int wtfpl(struct wtfpl_opts* opts, wtfpl_result_t** result) {
	struct traceroute_opts tro;
	traceroute_opts_init(&tro);
	tro.ip.sin_addr.s_addr = opts->addr;
	tro.ip.sin_family = AF_INET;
	tro.log_type = TR_LOG_NONE;
	tro.max_hops = 255;

	struct traceroute_result* tres = NULL;
	if (!traceroute(&tro, &tres)) {
		printf("traceroute failed\n");
		return 0;
	}

	if (!tres) {
		printf("No route\n");
		return 0;
	}

	int i = 1;
	struct wtfpl_node* lastn = NULL;
	struct wtfpl_node* first = NULL;
	for(struct traceroute_node* n = tres->first; n; n = n->next, ++i) {
		printf("%d %s", i, n->addr);
		struct ping_opts popts;
		icmp_ping_opts_init(&popts);
		popts.addr = n->in_addr;
		popts.num_packets = opts->samples;
		popts.interval = 0.5;

		struct ping_stats stats;
		if (!icmp_ping(&popts, &stats)) {
			printf(" failed\n");
			continue;
		}

		struct wtfpl_node* nod = calloc(1, sizeof(struct wtfpl_node));
		nod->addr = n->in_addr;
		nod->lost = stats.lost;
		nod->sent = stats.sent;
		nod->pl = stats.lost / (float)stats.sent;
		if (lastn) lastn->next = nod;
		if (!first) first = nod;
		lastn = nod;
		
		printf(" %.2f%% PL\n", nod->pl);
	}
	
	struct wtfpl_result* res = calloc(1, sizeof(struct wtfpl_result));
	res->first = first;
	
	/* Determine the suspect */
	for(struct wtfpl_node* n = first; n; n = n->next) {
		if (n->pl > 0) {
			res->suspect = n->addr;
			break;
		}
	}

	*result = res;
	return 1;
}

void wtfpl_opts_init(wtfpl_opts_t* opts) {
	opts->addr = 0;
	opts->samples = 50;
}

void wtfpl_result_free(wtfpl_result_t* result) {
	for (struct wtfpl_node* n = result->first; n;) {
		struct wtfpl_node* next = n->next;
		free(n);
		n = next;
	}
	free(result);
}

static void _wtfpl_cmd(int argc, char** argv) {
	wtfpl_opts_t opts;
	wtfpl_opts_init(&opts);

	getopt_state_t st;
	getopt_state_init(&st);
	int opt;
	while ((opt = getopt_s(argc, argv, "s:", &st)) != -1) {
		switch(opt) {
		case 's':
			opts.samples = atoi(st.optarg);
			break;
		}
	}

	if (st.optind < argc)
		opts.addr = inet_addr(argv[st.optind]);
	else {
		printf("No address\n");
		return;
	}

	wtfpl_result_t* result = NULL;
	wtfpl(&opts, &result);
	struct in_addr a = {result->suspect};
	printf("Likely bad node: %s\n", inet_ntoa(a));
	wtfpl_result_free(result);
}


#ifdef EPICS
#include <iocsh.h>
#include <epicsExport.h>

void register_wtfpl() {

}
epicsExportRegistrar(register_wtfpl);
#endif

#ifdef WTFPL_MAIN

int main(int argc, char** argv) {
	_wtfpl_cmd(argc, argv);
}

#endif