
TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# build a support library

LIBRARY_IOC += netUtils

USR_CXXFLAGS += -std=c++0x
USR_CFLAGS += -DEPICS -std=c99

# xxxRecord.h will be created from xxxRecord.dbd
#DBDINC += xxxRecord
# install rtemsNetTest.dbd into <top>/dbd
DBD += netUtils.dbd

# specify all source files to be compiled and added to the library
netUtils_SRCS += ping.c
netUtils_SRCS += traceroute.c
netUtils_SRCS += probe.c
netUtils_SRCS += getopt_s.c
#netUtils_SRCS += netstats.cc

INC += ping.h
INC += traceroute.h

netUtils_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

