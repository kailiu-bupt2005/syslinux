/*
 * urlparse.c
 *
 * Decompose a URL into its components.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "url.h"

void parse_url(struct url_info *ui, char *url)
{
    char *p = url;
    char *q, *r, *s;

    memset(ui, 0, sizeof *ui);

    q = strstr(p, "://");
    if (!q) {
	q = strstr(p, "::");
	if (q) {
	    *q = '\0';
	    ui->scheme = "tftp";
	    ui->host = p;
	    ui->path = q+2;
	    ui->type = URL_OLD_TFTP;
	    return;
	} else {
	    ui->path = p;
    	    ui->type = URL_PREFIX;
	    return;
	}
    }

    ui->type = URL_NORMAL;

    ui->scheme = p;
    *q = '\0';
    p = q+3;

    q = strchr(p, '/');
    if (q) {
	*q = '\0';
	ui->path = q+1;
	q = strchr(q+1, '#');
	if (q)
	    *q = '\0';
    } else {
	ui->path = "";
    }
    
    r = strchr(p, '@');
    if (r) {
	ui->user = p;
	*r = '\0';
	s = strchr(p, ':');
	if (s) {
	    *s = '\0';
	    ui->passwd = s+1;
	}
	p = r+1;
    }

    ui->host = p;
    r = strchr(p, ':');
    if (r) {
	*r = '\0';
	ui->port = atoi(r+1);
    }
}

char *url_escape_unsafe(const char *input)
{
    const char *p = input;
    unsigned char c;
    char *out, *q;
    size_t n = 0;

    while ((c = *p++)) {
	if (c < ' ' || c > '~') {
	    n += 3;		/* Need escaping */
	} else {
	    n++;
	}
    }

    q = out = malloc(n+1);
    while ((c = *p++)) {
	if (c < ' ' || c > '~') {
	    q += snprintf(q, 3, "%%02X", c);
	} else {
	    *q++ = c;
	}
    }

    *q = '\0';

    return out;
}

static int hexdigit(char c)
{
    if (c >= '0' && c <= '9')
	return c - '0';
    c |= 0x20;
    if (c >= 'a' && c <= 'f')
	return c - 'a' + 10;
    return -1;
}

void url_unescape(char *buffer)
{
    const char *p = buffer;
    char *q = buffer;
    unsigned char c;
    int x, y;

    while ((c = *p++)) {
	if (c == '%') {
	    x = hexdigit(p[0]);
	    if (x >= 0) {
		y = hexdigit(p[1]);
		if (y >= 0) {
		    *q++ = (x << 4) + y;
		    p += 2;
		    continue;
		}
	    }
	}
	*q++ = c;
    }
    *q = '\0';
}

#if 0

int main(int argc, char *argv[])
{
    int i;
    struct url_info url;

    for (i = 1; i < argc; i++) {
	parse_url(&url, argv[i]);
	printf("scheme:  %s\n"
	       "user:    %s\n"
	       "passwd:  %s\n"
	       "host:    %s\n"
	       "port:    %d\n"
	       "path:    %s\n"
	       "type:    %d\n",
	       url.scheme, url.user, url.passwd, url.host, url.port,
	       url.path, url.type);
    }

    return 0;
}

#endif
