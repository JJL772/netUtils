ifeq ($(EPICS_BASE),)

# Makefile at top of application tree
TOP = ..
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
CXXFLAGS:=$(CXXFLAGS) $(CFLAGS) -DINCLUDE_MAIN=1
PREFIX?=/usr/local
LDFLAGS+=-lm

all: $(OUT)/ping $(OUT)/traceroute

bin/$(ARCH):
	mkdir -p bin/$(ARCH)

$(OUT)/traceroute: src/traceroute.c src/getopt_s.c
	mkdir -p $(OUT)
	$(CC) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(OUT)/ping: src/ping.c src/getopt_s.c
	mkdir -p $(OUT)
	$(CC) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

install:
	mkdir -p $(PREFIX)/include/netutils
	cp src/ping.h $(PREFIX)/include/netutils
	cp src/traceroute.h $(PREFIX)/include/netutils

clean:
	rm -rf $(OUT) || true

.PHONY: clean install
endif