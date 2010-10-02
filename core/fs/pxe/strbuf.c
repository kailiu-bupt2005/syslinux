/*
 * strbuf.c
 *
 * A self-extending string buffer type.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "strbuf.h"

#define MINSIZE	 96
#define SIZESTEP 32		/* Power of two, please! */

struct strbuf __strbuf_error_buf = {
    .size = 0,
    .len  = 0,
    .str  = ""
};

void strbuf_free(struct strbuf **bufp)
{
    if (*bufp != &__strbuf_error_buf)
	free(*bufp);
    *bufp = NULL;
}

#if 0

void *strbuf_cat(struct strbuf **bufp, const char *str)
{
    struct strbuf *buf, *xbuf;
    size_t len = strlen(str);
    size_t size;

    buf = *bufp;

    if (buf == &__strbuf_error_buf)
	return;

    if (!buf) {
	size = (len+SIZESTEP) & ~(SIZESTEP-1);
	if (size < MINSIZE)
	    size = MINSIZE;
	buf = malloc(sizeof(struct strbuf) + MINSIZE);
	if (!buf) {
	    *bufp = &__strbuf_error_buf;
	    return;
	}

	buf->len  = len;
	buf->size = size;
	memcpy(buf->str, str, len+1);
	return;
    }

    if (buf->size - buf->len > len) {
	size = (buf.size + len + SIZESTEP) & ~(SIZESTEP-1);
	xbuf = realloc(buf, sizeof(struct strbuf) + MINSIZE);
	if (!xbuf) {
	    free(buf);
	    *bufp = &__strbuf_error_buf;
	    return;
	}
	*bufp = buf = xbuf;
	buf->size = size;
    }

    memcpy(buf->str + buf->len, str, len+1);
    buf->len += len;
}

#else
void strbuf_cat(struct strbuf **bufp, const char *str)
{
    return sbprintf(bufp, "%s", str);
}
#endif

void strbuf_putc(struct strbuf **bufp, char c)
{
    char str[2];

    str[0] = c;
    str[1] = '\0';
    return strbuf_cat(bufp, str);
}

void sbprintf(struct strbuf **bufp, const char *fmt, ...)
{
    struct strbuf *buf, *xbuf;
    size_t len;
    size_t size;
    size_t slack;
    va_list ap;
    char *p;

    buf = *bufp;

    if (buf == &__strbuf_error_buf)
	return;			/* Buffer in error state */

    va_start(ap, fmt);

    if (buf) {
	p = buf->str + buf->len;
	slack = buf->size - buf->len;
    } else {
	p = NULL;
	slack = 0;
    }

    len = vsnprintf(p, slack, fmt, ap);
    va_end(ap);

    if (len < slack) {
	buf->len += len;
	return;			/* It all fit */
    }

    size = (strbuf_len(buf) + len + SIZESTEP) & ~SIZESTEP;
    if (size < MINSIZE)
	size = MINSIZE;

    xbuf = realloc(buf, sizeof(struct strbuf) + size);
    if (!xbuf) {
	free(buf);
	*bufp = &__strbuf_error_buf;
	return;
    }
    if (!buf)
	xbuf->len = 0;
    *bufp = buf = xbuf;

    buf->size = size;

    va_start(ap, fmt);
    vsnprintf(buf->str + buf->len, buf->size - buf->len, fmt, ap);
    va_end(ap);

    buf->len += len;
}
