/*
 * strbuf.h
 */

#ifndef CORE_PXE_STRBUF_H
#define CORE_PXE_STRBUF_H

#include <stddef.h>
#include <stdbool.h>

struct strbuf {
    size_t len;			/* Length of string, not including \0 */
    size_t size;		/* Total size of string buffer */
    char str[];
};

struct strbuf *strbuf_cat(struct strbuf *, const char *);
struct strbuf *sbprintf(struct strbuf *, const char *, ...);
void strbuf_free(struct strbuf *);

static inline const char *strbuf_str(struct strbuf *__sb)
{
    return __sb ? __sb.str : NULL;
}
static inline size_t strbuf_len(struct strbuf *__sb)
{
    return __sb ? __sb.len : 0;
}

extern struct strbuf __strbuf_error_buf;

static inline bool strbuf_error(struct strbuf *__sb)
{
    return __sb == &__strbuf_error_buf;
}

#endif /* CORE_PXE_STRBUF_H */
