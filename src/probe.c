
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

static void show_help();

#define MAX_ADDRS 64
struct probe_opts_s {
    float time;
    int numaddrs;
    in_addr_t addrs[MAX_ADDRS];
    int verbose;
    int tries;          /* How many samples? */
    int max_size;
};

struct probe_result_s {
    struct ping_stats pstat;
    struct traceroute_result* tstat;
};

static void probe(struct probe_opts_s* opts);
void probe_opts_init(struct probe_opts_s* opts);

int probe_cmd(int argc, char** argv) {
    getopt_state_t st;
    getopt_state_init(&st);

    struct probe_opts_s opts;
    probe_opts_init(&opts);

    int opt = 0;
    float time = 60 * 5; // Probe for 5 minutes by default
    while ((opt = getopt_s(argc, argv, "t:hvc:m:", &st)) != -1) {
        switch(opt) {
        case 't':
            time = atof(st.optarg);
            break;
        case 'h':
            show_help();
            break;
        case 'v':
            opts.verbose++;
            break;
        case 'c':
            opts.tries = atoi(st.optarg);
            break;
        case 'm':
            opts.max_size = atoi(st.optarg);
            break;
        default:
            break;
        }
    }

    for (int i = st.optind; i < argc; ++i) {
        if (opts.numaddrs < MAX_ADDRS)
            opts.addrs[opts.numaddrs++] = inet_addr(argv[i]);
    }

    if (opts.numaddrs <= 0) {
        show_help();
        return -1;
    }

    opts.time = time;

    probe(&opts);
    return 0;
}

static void probe_one(struct probe_opts_s* probe_opts, int cur_addr, struct probe_result_s* result) {
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
    defpopts.num_packets = 100;
    defpopts.interval = 0.25; /* ~4 packets a second */
    defpopts.log_type = probe_opts->verbose ? PING_LOG_FULL : PING_LOG_MINIMAL;

    struct timespec start = time_now();

    const uint8_t patterns[10] = {0xA5, 0xAA, 0xFF, 0x1, 0x10, 0xF0, 0x0F, 0x7F, 0x0, 0x5A};
    const uint32_t sizes[10] = {128, 512, 4096, 8192, 16384, 20000, 30000, 32768, 48000, 50000};

    while (1)
    {
        for (int i = 0; i < probe_opts->tries; ++i) {
            const uint32_t size = CLAMP(sizes[i], 1, probe_opts->max_size);
            struct ping_opts popts = defpopts;
            popts.pattern = patterns[i];
            popts.payload_size = size;

            printf("------------------------\nPinging %s, size %u, \n", strAddr, size);

            struct ping_stats pstat;
            if (!icmp_ping(&popts, &pstat)) {
                printf("  Failed.\n");
                continue;
            }

            /* TODO: Merge this pstat with the result */
            printf("  Completed (pattern 0x%X, size %u): %d sent, %d lost, %d corrupted, maxTime %f, minTime %f, avgTime %f\n",
                (int)patterns[i], size, pstat.sent, pstat.lost, pstat.corrupted, pstat.maxTime, pstat.minTime, pstat.avgTime);
        }

        struct timespec now = time_now();
        if (time_diff(&now, &start) >= probe_opts->time)
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
        probe_one(opts, i, &res);
    }
}

static void show_help() {
    printf("probe [-t time] [-m max_size] [-c count] [-v] ADDRS...\n");
}

#ifdef EPICS
#include <iocsh.h>
#include <epicsExport.h>

static void probe_iocsh(const iocshArgBuf* buf) {
    probe_cmd(buf[0].aval.ac, buf[0].aval.av);
}

void register_probe() {
    static const iocshArg arg = {"args", iocshArgArgv};
    static const iocshArg* args[] = { &arg };
    static const iocshFuncDef func = {"probe", 1, args};
    iocshRegister(&func, probe_iocsh);
}
epicsExportRegistrar(register_probe);
#endif

#ifdef PROBE_MAIN
int main(int argc, char** argv) {
    return probe_cmd(argc, argv);
}
#endif
