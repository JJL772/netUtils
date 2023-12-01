
#ifndef _PCAP_H_
#define _PCAP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

#pragma pack(1)

#define PCAP_MAGIC 0xA1B2C3D4
#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4

typedef struct pcap_header {
    uint32_t magic;
    uint16_t ver_major;
    uint16_t ver_minor;
    uint32_t tz;    /* Timezone */
    uint32_t tsa;   /* Timestamp accuracy */
    uint32_t snl;   /* Snap len */
    uint32_t llt;   /* Link-layer type */
} pcap_header_t;

#define PCAP_LLT_RAWIP 101U
#define PCAP_LLT_ETH8023 1U
#define PCAP_LLT_80211 105U
#define PCAP_LLT_RAWIP4 228U
#define PCAP_LLT_RAWIP6 229U

typedef struct pcap_packet_header {
    uint32_t tss;       /* Timestamp S */
    uint32_t tsm;       /* Timestamp uS */
    uint32_t caplen;    /* Captured length */
    uint32_t orglen;    /* Original length */
} pcap_packet_header_t;

typedef struct pcap_file {
    FILE* fp;
    struct pcap_header header;
} pcap_file_t;

typedef struct pcap_timestamp {
    uint32_t sec;
    uint32_t nsec;
} pcap_timestamp_t;

extern pcap_timestamp_t pcap_timestamp_now();

extern pcap_file_t* pcap_file_create(const char* path, int link_layer_type);

extern void pcap_file_close(pcap_file_t* file);

extern void pcap_file_flush(pcap_file_t* file);

extern int pcap_add_packet(pcap_file_t* file, pcap_timestamp_t ts, const void* data, int64_t datalen, int64_t orglen);

inline static int pcap_add_packet_now(pcap_file_t* file, const void* data, int64_t datalen, int64_t orglen) {
    return pcap_add_packet(file, pcap_timestamp_now(), data, datalen, orglen);
}

#pragma pack(0)

#ifdef __clangd__
#define PCAP_IMPL
#endif

#ifdef PCAP_IMPL

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

pcap_timestamp_t pcap_timestamp_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    pcap_timestamp_t t = {tv.tv_sec, tv.tv_usec * 1e3};
    return t;
}

pcap_file_t* pcap_file_create(const char* path, int link_layer_type) {
    pcap_file_t* f = calloc(1, sizeof(pcap_file_t));
    f->header.ver_major = PCAP_VERSION_MAJOR;
    f->header.ver_minor = PCAP_VERSION_MINOR;
    f->header.magic = PCAP_MAGIC;
    f->header.tsa = 0; /* All tools set this to 0, apparently */
    f->header.llt = link_layer_type;
    f->header.snl = 65535;

    time_t t = time(0);
    struct tm tp = {0};
    localtime_r(&t, &tp);

    f->header.tz = tp.tm_gmtoff;

    f->fp = fopen(path, "wb");
    if (!f->fp) {
        free(f);
        return NULL;
    }

    fwrite(&f->header, sizeof(f->header), 1, f->fp);
    return f;
}

void pcap_file_close(pcap_file_t* file) {
    if (file->fp)
        fclose(file->fp);
    free(file);
}

void pcap_file_flush(pcap_file_t* file) {
    if (file->fp)
        fflush(file->fp);
}

int pcap_add_packet(pcap_file_t* file, pcap_timestamp_t ts, const void* data, int64_t datalen, int64_t orglen) {
    pcap_packet_header_t pack;
    pack.caplen = datalen;
    pack.orglen = orglen;
    pack.tss = ts.sec;
    pack.tsm = ts.nsec / 1e3;
    if (fwrite(&pack, sizeof(pack), 1, file->fp) != 1)
        return -1;
    if (fwrite(data, datalen, 1, file->fp) != 1)
        return -1;
    return 0;
}

#endif

#ifdef __cplusplus
}
#endif

#endif