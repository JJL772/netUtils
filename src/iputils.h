
#pragma once

#include <unistd.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief One's complement sum between lhs and rhs
 */
static inline uint16_t ones_sum(uint16_t lhs, uint16_t rhs) {
    int a = lhs + rhs;
    return (a & 0xFFFF) + (a >> 16);
}

static inline uint16_t ip_cksum(const void* data, size_t len) {
	size_t rem = len % 2;
	len /= 2;
	uint16_t s = 0;
	for (size_t i = 0; i < len; ++i)
		s = ones_sum(s, *((const uint16_t*)(data) + i));
	if (rem) {
		union { uint16_t a; uint8_t b[2]; } tb = {0};
		tb.b[0] = *(((const uint8_t*)data) + len*2);
		return ones_sum(s, tb.a);
	}
	return ~s;
}

static inline struct timespec time_now() {
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp;
}

static inline double time_diff(struct timespec* a, struct timespec* b) {
    double af = a->tv_sec + a->tv_nsec / 1e9;
    double bf = b->tv_sec + b->tv_nsec / 1e9;
    return af - bf;
}

#ifdef __cplusplus
}
#endif
