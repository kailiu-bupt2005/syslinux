/*
 * netstream.c
 *
 * Simple interface to get data from a network stream; used during
 * header processing.  It is much more lightweight than the socket
 * interface.
 */

#include <string.h>
#include "lwip/api.h"
#include "pxe.h"

int netstream_getc(struct netstream *stream)
{
    int rv;

    if (!stream->conn)
	return -1;

    if (!stream->buf || !stream->buf->ptr) {
	stream->buf = netconn_recv(stream->conn);
	if (!stream->buf)
	    return -1;
    }

    rv = ((unsigned char *)stream->buf->ptr->payload)[stream->offs++];
    if (stream->offs >= stream->buf->ptr->len) {
	stream->buf->ptr = stream->buf->ptr->next;
	if (!stream->buf->ptr) {
	    netbuf_delete(stream->buf);
	    stream->buf = NULL;
	}
	stream->offs = 0;
    }

    return rv;
}

int netstream_read(struct netstream *stream, void *buf, int bytes)
{
    char *p = buf;
    int rv = 0;
    int avail;
    
    if (!stream->conn)
	return -1;

    while (bytes) {
	if (!stream->buf || !stream->buf->ptr) {
	    stream->buf = netconn_recv(stream->conn);
	    if (!stream->buf)
		break;
	    stream->offs = 0;
	}

	avail = stream->buf->ptr->len - stream->offs;
	if (avail > bytes)
	    avail = bytes;

	memcpy(p, (char *)stream->buf->ptr->payload + stream->offs, avail);
	p += avail;
	stream->offs += avail;
	bytes -= avail;
	rv += avail;

	if (stream->offs >= stream->buf->ptr->len) {
	    stream->buf->ptr = stream->buf->ptr->next;
	    if (!stream->buf->ptr) {
		netbuf_delete(stream->buf);
		stream->buf = NULL;
	    }
	    stream->offs = 0;
	}
    }

    return rv;
}

	

