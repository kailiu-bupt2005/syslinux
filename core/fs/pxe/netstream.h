/*
 * netstream.h
 *
 * Simple interface to get data from a network stream; used during
 * header processing.  It is much more lightweight than the socket
 * interface.
 */

#ifndef CORE_PXE_NETSTREAM_H
#define CORE_PXE_NETSTREAM_H

#include <stddef.h>

struct netconn;
struct netbuf;

struct netstream {
    struct netconn *conn;	/* Network connection */
    struct netbuf  *buf;	/* Network buffer */
    size_t          offs;	/* Offset from ptr */
};

int netstream_getc(struct netstream *stream);
int netstream_read(struct netstream *stream, void *buf, int bytes);

#endif /* CORE_PXE_NETSTREAM_H */
