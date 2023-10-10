
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct getopt_state {
#ifdef __cplusplus
    getopt_state() :
        opterr(1), optind(1), optopt(0),
        optreset(0), optarg(NULL), place(NULL) {};
#endif

	int	opterr,	    	/* if error message should be printed */
		optind, 		/* index into parent argv vector */
		optopt,			/* character checked for validity */
		optreset;		/* reset getopt */
	char	*optarg;	/* argument associated with option */
	char	*place;
};

typedef struct getopt_state getopt_state_t;

static inline void getopt_state_init(struct getopt_state* st) {
	st->optarg = NULL;
	st->opterr = 0;
	st->optind = 1;
	st->optopt = 0;
	st->optreset = 0;
	st->place = NULL;
}

/**
 * Thread-safe and reentry-safe implementation of getopt
 * Create struct getopt_state either on the stack or heap, and then pass pointer into this
 * access optind, optopt, optarg, etc. off of that structure.
 */
int getopt_s(int nargc, char* const nargv[], const char* ostr, getopt_state_t* state);

#ifdef __cplusplus
}
#endif
