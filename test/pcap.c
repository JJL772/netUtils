
#define PCAP_IMPL
#include "../src/pcap.h"

#include "../src/iputils.h"

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
#include <unistd.h>

struct __attribute__((packed)) tr_packet {
	struct icmp icmp_packet;
	uint32_t sec;
	uint32_t nsec;
};

/* Build an IP frame, returns the length of the entire packet */
static ssize_t _tr_make_ip_frame(struct ip* ipf, uint8_t ttl, size_t datalen) {
	//ipf->ip_dst = opts->ip.sin_addr;
	ipf->ip_v = IPVERSION;
	ipf->ip_tos = 0; /* Type of service should just be normal... */
	ipf->ip_id = 1234;
	ipf->ip_p = IPPROTO_ICMP;
	//ipf->ip_src = ctx->local.sin_addr;
	ipf->ip_ttl = ttl;
	ipf->ip_hl = sizeof(*ipf) / 4;
	ipf->ip_len = htons(datalen + sizeof(*ipf));
	ipf->ip_off = 0;
	ipf->ip_sum = 0;
	ipf->ip_sum = ip_cksum(ipf, sizeof(*ipf));
	return ipf->ip_len;
}

static void _tr_make_icmp(struct tr_packet* packet) {
	memset(packet, 0, sizeof(*packet));
    packet->icmp_packet.icmp_type = ICMP_ECHO;
    packet->icmp_packet.icmp_code = 0;
    packet->icmp_packet.icmp_hun.ih_idseq.icd_id = 1234;
    packet->icmp_packet.icmp_hun.ih_idseq.icd_seq = 0;

	struct timespec sentat = time_now();

    packet->sec = sentat.tv_sec;
    packet->nsec = sentat.tv_nsec;
	packet->icmp_packet.icmp_cksum = 0;

    packet->icmp_packet.icmp_cksum = ip_cksum(&packet->icmp_packet, sizeof(packet->icmp_packet));
}

int main() {
    pcap_file_t* pf = pcap_file_create("test.pcap", PCAP_LLT_RAWIP4);

	for (int i = 0; i < 30; ++i) {
    	struct __attribute__((packed)) packet {
    	    struct ip ip;
    	    struct tr_packet tr;
    	} p = {0};
    	_tr_make_ip_frame(&p.ip, 12, sizeof(p.tr));
    	_tr_make_icmp(&p.tr);
	
    	pcap_add_packet_now(pf, &p, sizeof(p), sizeof(p));
		usleep(1000);
	}
    pcap_file_close(pf);
}