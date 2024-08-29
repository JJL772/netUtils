# Portable Networking Utils

Well, sorta portable... winsock not supported, only BSD sockets.

Builds with and without EPICS.

Requirements:
- C99 compiler
- C library support for POSIX.1b
- BSD-like networking stack (Known to work with Linux, FreeBSD and RTEMS 4.X networking)

## Tools

* ping
* traceroute
* probe (daemonized ping)
