
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "getopt_s.h"

#define MAX_DATA 512

struct data {
    char* title;
    uint64_t val;
};

/* Parse /proc/net/netstat. Modifies data in place */
static size_t _parse_netstats(char* data, struct data* outdata, size_t maxd) {
    size_t numdata = 0;

    char* outer = 0;
    for (char* s = strtok_r(data, "\n", &outer); s; s = strtok_r(0, "\n", &outer)) {
        const size_t pfx_len = strlen("TcpExt: ");
        if (!strncmp(s, "TcpExt: ", pfx_len)) {
            char* n = strtok_r(0, "\n", &outer);

            if (!n || strncmp(n, "TcpExt: ", pfx_len))
                printf("Error parsing data, expected next line!\n");

            char* tp = 0, *vp = 0;

            for(char* title = strtok_r(s + pfx_len, " ", &tp), *value = strtok_r(n + pfx_len, " ", &vp);
                title && value && numdata < maxd; title = strtok_r(0, " ", &tp), value = strtok_r(0, " ", &vp))
            {
                outdata[numdata].val = strtoull(value, 0, 10);
                outdata[numdata].title = title;
                ++numdata;
            }
        }
    }
    return numdata;
}

int main(int argc, char** argv) {
    int opt;
    getopt_state_t st;
    getopt_state_init(&st);
    
    int all = 0, fromstdin = 0;
    while((opt = getopt_s(argc, argv, "a-", &st)) != -1) {
        switch(opt) {
        case 'a':
            all = 1;
            break;
        case '-':
            fromstdin = 1;
            break;
        }
    }

    char* buf = NULL;
    if (!fromstdin) {
#if __linux__
        struct stat stbuf;
        if (stat("/proc/net/netstat", &stbuf) == -1) {
            perror("Unable to stat /proc/net/netstat");
            return 1;
        }
        buf = (char*)malloc(stbuf.st_size + 4096);
        int fd = open("/proc/net/netstat", O_RDONLY);
        if (fd < 0) {
            perror("Unable to open /proc/net/netstat");
            free(buf);
            return 1;
        }
        if (read(fd, buf, stbuf.st_size + 4096) <= 0) {
            perror("unable to read /proc/net/netstat");
            close(fd);
            free(buf);
            return 1;
        }
#else
        printf("/proc/net/netstat is not supported on this platform\n");
        return 1;
#endif
    }
    else {
        buf = (char*)malloc(4096);
        ssize_t pos = 0, rem = 4096;
        ssize_t numread = 0;
        while((numread = read(fileno(stdin), buf + pos, 4096)) != 0) {
            pos += numread;
            rem -= numread;
            if (rem <= 0)
                buf = (char*)realloc(buf, pos + 4096);
        }
        buf[pos] = 0;
    }

    struct data data[MAX_DATA];
    size_t n = _parse_netstats(buf, data, MAX_DATA);
    for (size_t i = 0; i < n; ++i) {
        printf("%-30s: %lu\n", data[i].title, data[i].val);
    }

    free(buf);
}