/* Junk! Just probes a list of hosts for a period of time. */
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
#include <pthread.h>

#include "ping.h"
#include "traceroute.h"
#include "iputils.h"
#include "getopt_s.h"

static void show_help();

#define MAX_ADDRS 64
struct probe_opts_s {
    float time;
    int numaddrs;
    in_addr_t addrs[MAX_ADDRS];
    int verbose;
    int tries;          /* How many samples? */
    int max_size;
    int sentry;
};

struct probe_result_s {
    struct ping_stats pstat;
    struct traceroute_result* tstat;
};

static pthread_t s_thread;
static pthread_attr_t s_thrattr;
static int s_threadRun = 1;

static void probe(struct probe_opts_s* opts);
void probe_opts_init(struct probe_opts_s* opts);

static void* _probe_sentry(void*);

int probe_cmd(int argc, char** argv) {

    if (s_thread) {
        printf("Thread already running\n");
        return -1;
    }

    getopt_state_t st;
    getopt_state_init(&st);

    struct probe_opts_s *opts = calloc(1, sizeof(struct probe_opts_s));
    probe_opts_init(opts);

    int opt = 0;
    float time = 60 * 5; // Probe for 5 minutes by default
    while ((opt = getopt_s(argc, argv, "t:hvc:m:s", &st)) != -1) {
        switch(opt) {
        case 't':
            time = atof(st.optarg);
			if (time < 0)
				time = 60 * 60 * 24 * 365 * 10; /* Just run for 10 years !!! */
            break;
        case 'h':
            show_help();
            break;
        case 'v':
            opts->verbose++;
            break;
        case 'c':
            opts->tries = atoi(st.optarg);
            break;
        case 's':
            opts->sentry = 1;
            break;
        case 'm':
            opts->max_size = atoi(st.optarg);
            break;
        default:
            break;
        }
    }

    for (int i = st.optind; i < argc; ++i) {
        if (opts->numaddrs < MAX_ADDRS)
            opts->addrs[opts->numaddrs++] = inet_addr(argv[i]);
    }

    if (opts->numaddrs <= 0) {
        show_help();
        return -1;
    }

    opts->time = time;

    if (opts->sentry) {
        pthread_attr_init(&s_thrattr);
        pthread_create(&s_thread, &s_thrattr, _probe_sentry, opts);
    }
    else {
        probe(opts);
        free(opts);
    }
    return 0;
}

static void* _probe_sentry(void* p) {
    probe(p);
    free(p);
    pthread_attr_destroy(&s_thrattr);
    s_thread = 0;
    return NULL;
}

static void _probe_one(struct probe_opts_s* probe_opts, int cur_addr, struct probe_result_s* result) {
    struct traceroute_opts opts;
    traceroute_opts_init(&opts);
    opts.ip.sin_addr.s_addr = probe_opts->addrs[cur_addr];
    opts.ip.sin_family = AF_INET;

    memset(result, 0, sizeof(*result));
    /* Grab a route */
    traceroute(&opts, &result->tstat);

    /* Ping with varying patterns and sizes */
    struct ping_opts defpopts;
    icmp_ping_opts_init(&defpopts);

    char strAddr[128];
    {
        const struct in_addr a = { probe_opts->addrs[cur_addr] };
        strncpy(strAddr, inet_ntoa(a), sizeof(strAddr));
        strAddr[sizeof(strAddr)-1] = 0;
    }

    defpopts.addr = probe_opts->addrs[cur_addr];
    defpopts.num_packets = probe_opts->sentry ? 10 : 100;
    defpopts.interval = 0.25; /* ~4 packets a second */
    defpopts.log_type = probe_opts->verbose ? PING_LOG_FULL : probe_opts->sentry ? PING_LOG_NONE : PING_LOG_MINIMAL;

    struct timespec start = time_now();

	#define NUM_SAMPLES 10
    const uint8_t patterns[NUM_SAMPLES] = {0xA5, 0xAA, 0xFF, 0x1, 0x10, 0xF0, 0x0F, 0x7F, 0x0, 0x5A};
    const uint32_t sizes[NUM_SAMPLES] = {128, 256, 512, 760, 1024, 2048, 4096, 8192, 16384, 20000};
	const float intervals[NUM_SAMPLES] = {0.25, 0.1, 0.05, 0.5, 0.25, 0.25, 0.1, 0.5, 0.25, 0.25};

    while (1)
    {
        for (int i = 0; i < probe_opts->tries; ++i) {
			if (!s_threadRun)
				return;

            const uint32_t size = CLAMP(sizes[i % NUM_SAMPLES], 1, probe_opts->max_size);
            struct ping_opts popts = defpopts;
            popts.pattern = patterns[rand() % NUM_SAMPLES];
            popts.payload_size = probe_opts->sentry ? 80 : size;
			popts.interval = probe_opts->sentry ? 0.5 : intervals[rand() % NUM_SAMPLES];

            if (!probe_opts->sentry)
                printf("------------------------\nPinging %s, size %u, pattern 0x%X, interval %f\n", strAddr, size, (int)popts.pattern, popts.interval);

            struct ping_stats pstat;
            if (!icmp_ping(&popts, &pstat) && !pstat.sent) {
                printf("  Failed.\n");
                continue;
            }

            /* TODO: Merge this pstat with the result */
            if (!probe_opts->sentry)
                printf("  Completed (pattern 0x%X, size %u): %d sent, %d lost, %d corrupted, maxTime %f, minTime %f, avgTime %f\n",
                    (int)popts.pattern, size, pstat.sent, pstat.lost, pstat.corrupted, pstat.maxTime, pstat.minTime, pstat.avgTime);
            else if (pstat.lost) {
                char b[128];
                printf("[%s] lost %d packets to %s\n", time_now_str(b, sizeof(b)), pstat.lost, strAddr);
            }
        }

        struct timespec now = time_now();
        if (!probe_opts->sentry && time_diff(&now, &start) >= probe_opts->time)
            break;
    }
}

void probe_opts_init(struct probe_opts_s* opts) {
    memset(opts, 0, sizeof(*opts));
    opts->time = 60 * 5; /* 5 minutes by default */
    opts->tries = 100;
    opts->max_size = (1<<30);
}

static void probe(struct probe_opts_s* opts) {
    for (int i = 0; i < opts->numaddrs; ++i) {
        struct probe_result_s res;
        _probe_one(opts, i, &res);
	#ifdef EPICS
		if (!s_threadRun)
			return;
	#endif
    }
}

static void show_help() {
    printf("probe [-t time] [-m max_size] [-c count] [-v] ADDRS...\n");
}

#ifdef EPICS
#include <iocsh.h>
#include <epicsExport.h>

struct thr_arg {
	int ac;
	char* av[64];
};

static void probe_iocsh(const iocshArgBuf* buf) {
	probe_cmd(buf[0].aval.ac, buf[0].aval.av);
}

static void probe_kill(const iocshArgBuf* buf) {
    if (!s_thread)
        return;
    s_threadRun = 0;
    pthread_join(s_thread, NULL);
    s_threadRun = 1;
}

void register_probe() {
    static const iocshArg arg = {"args", iocshArgArgv};
    static const iocshArg* args[] = { &arg };
    static const iocshFuncDef func = {"probe", 1, args};
    iocshRegister(&func, probe_iocsh);

	static const iocshFuncDef kill_func = {"probeStop", 0, NULL};
	iocshRegister(&kill_func, probe_kill);
}
epicsExportRegistrar(register_probe);
#endif

#ifdef PROBE_MAIN
int main(int argc, char** argv) {
    return probe_cmd(argc, argv);
}
#endif
