# Portable Networking Utils

Well, sorta portable... winsock not supported, only UNIX sockets.

Builds with and without EPICS.

Requirements:
- C99 compiler
- C library support for POSIX.1b
- BSD-like networking stack (Known to work with Linux, FreeBSD and RTEMS 4.X networking)

## Tools

* wtfpl (Where's My Freakin' Packet Loss? Determines PL to each node on the route to a host)
* ping
* traceroute
* probe (daemonized ping)
