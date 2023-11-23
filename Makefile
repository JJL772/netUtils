# Makefile at top of application tree
TOP = ..
-include $(TOP)/configure/CONFIG

ifneq ($(EPICS_BASE),)
EPICS_BUILD ?= YES
endif

ifeq ($(EPICS_BUILD),YES)
# Include again to ensure it's actually pulled in
include $(TOP)/configure/CONFIG

# Directories to be built, in any order.
# You can replace these wildcards with an explicit list
DIRS += $(wildcard src* *Src*)
DIRS += $(wildcard db* *Db*)

# If the build order matters, add dependency rules like this,
# which specifies that xxxSrc must be built after src:
#xxxSrc_DEPEND_DIRS += src

include $(TOP)/configure/RULES_DIRS

else

ARCH?=$(shell uname -s | tr A-Z a-z)-$(shell uname -m)
OUT=bin/$(ARCH)
CPPFLAGS=-ggdb -D_GNU_SOURCE
CFLAGS+=-std=c99 $(CPPFLAGS)
CXXFLAGS:=$(CPPFLAGS) -std=c++0x
PREFIX?=/usr/local
LDFLAGS+=-lm

ifeq ($(ASAN),YES)
CPPFLAGS+=-fsanitize=address 
endif

all: $(OUT)/ping $(OUT)/traceroute $(OUT)/netstats $(OUT)/probe $(OUT)/wtfpl

bin/$(ARCH):
	mkdir -p bin/$(ARCH)

$(OUT)/traceroute: src/traceroute.c src/getopt_s.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -DTRACEROUTE_MAIN -o $@ $^ $(LDFLAGS)

$(OUT)/ping: src/ping.c src/getopt_s.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -DPING_MAIN -o $@ $^ $(LDFLAGS)

$(OUT)/netstats: src/netstats.c src/getopt_s.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OUT)/probe: src/probe.c src/ping.c src/traceroute.c src/getopt_s.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -DPROBE_MAIN -o $@ $^ $(LDFLAGS)

$(OUT)/wtfpl: src/wtfpl.c src/ping.c src/traceroute.c src/getopt_s.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -DWTFPL_MAIN -o $@ $^ $(LDFLAGS)

install:
	mkdir -p $(PREFIX)/include/netutils
	cp src/ping.h $(PREFIX)/include/netutils
	cp src/traceroute.h $(PREFIX)/include/netutils

clean:
	rm -rf $(OUT) || true

.PHONY: clean install
endif
