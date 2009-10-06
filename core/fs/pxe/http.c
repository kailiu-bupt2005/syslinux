/*
 * http.c
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <core.h>
#include <fs.h>
#include <minmax.h>
#include <sys/cpu.h>
#include <netinet/in.h>
#include "pxe.h"
#include "thread.h"
#include "strbuf.h"
#include "url.h"
#include "lwip/api.h"
#include "lwip/dns.h"

struct file *http_open(struct url_info *url, struct strbuf **redirect)
{
    const char *p;
    char c;
    int err;
    struct ip_addr ip;
    struct netconn *conn = NULL;
    struct netbuf *buf = NULL;
    int pos;
    enum state {
	st_httpver,
	st_stcode,
	st_skipline,
	st_headerfirst,
	st_header,
	st_headergap,
	st_contline,
	st_headertoken,
	st_eoh,
    } state;
    int status;
    struct strbuf *http_header = NULL;
    struct strbuf *location    = NULL;
    struct strbuf *header_name = NULL;
    struct strbuf *header_data = NULL;
    int64_t content_length;
    char *ep;
    struct file *file;

    *redirect = NULL;

    strbuf_cat(&http_header, "GET /");

    p = url->path;
    while ((c = *p)) {
	if (c <= ' ' || c > '~' || c == '"' ||
	    c == '#' || c == '<' || c == '>') {
	    sbprintf(&http_header, "%%%02X", (unsigned char)c);
	} else {
	    strbuf_putc(&http_header, c);
	}
    }

    sbprintf(&http_header,
	     " HTTP/1.0\r\n"
	     "Host: %s\r\n"
	     "User-Agent: %s\r\n"
	     "\r\n",
	     url->host,
	     "PXELINUX/something-or-other");

    if (strbuf_error(http_header))
	return NULL;

    /* XXX: implement at least basic authentication here */

    err = netconn_gethostbyname(url->host, &ip);
    if (err)
	return NULL;

    file = NULL;

    conn = netconn_new(NETCONN_TCP);
    err = netconn_connect(conn, &ip, url->port ? url->port : 80);
    if (err)
	goto err_delete;

    err = netconn_write(conn, strbuf_str(http_header), strbuf_len(http_header),
			NETCONN_NOCOPY);
    if (err)
	goto err_disconnect;

    strbuf_free(&http_header);

    /* Parse the HTTP header */
    state = st_stcode;
    pos = 0;
    status = 0;
    location = NULL;
    content_length = -1;

    while (state != st_eoh) {
	void *data;
	u16_t len;
	
	buf = netconn_recv(conn);
	if (!buf)
	    break;
	
	do {
	    netbuf_data(buf, &data, &len);
	    
	    p = data;
	    while (state != st_eoh && len) {
		c = *p++;
		len--;

		if (c == '\r' || c == '\0')
		    continue;

		switch (state) {
		case st_httpver:
		    if (c == ' ') {
			state = st_stcode;
			pos = 0;
		    }
		    break;

		case st_stcode:
		    if (c < '0' || c > '9')
			goto err_disconnect ;
		    status = (status*10) + (c - '0');
		    if (++pos == 3)
			state = st_skipline;
		    break;

		case st_skipline:
		    if (c == '\n') {
			state = st_headerfirst;
			pos = 0;
		    }
		    break;

		case st_headerfirst:
		    if (c == '\n') {
			state = st_eoh;
			break;
		    } else if (isspace(c)) {
			state = st_skipline;
			break;
		    }
		    /* else fall through */
		    
		case st_header:
		    if (!isspace(c)) {
			strbuf_putc(&header_name, c);
			break;
		    }
		    /* else fall through */

		case st_headergap:
		    if (c == '\n')
			state = st_contline;
		    else if (!isspace(c)) {
			strbuf_putc(&header_data, c);
			state = st_headertoken;
		    }
		    break;

		case st_contline:
		    if (c == '\n') {
			strbuf_free(&header_name);
			state = st_eoh;
		    } else if (isspace(c)) {
			state = st_headergap;
		    } else {
			strbuf_putc(&header_data, c);
			state = st_headertoken;
		    }
		    break;

		case st_headertoken:
		    if (isspace(c)) {
			state = (c == '\n') ? st_headerfirst : st_skipline;

			if (!strcasecmp("Location:",
					strbuf_str(header_name))) {
			    location = header_data;
			    header_data = NULL;
			} else if (!strcasecmp("Content-Length:",
					       strbuf_str(header_name))) {
			    content_length =
				strtoull(strbuf_str(header_data), &ep, 10);
			    if (*ep)
				content_length = -1;
			    strbuf_free(&header_data);
			} else {
			    strbuf_free(&header_data);
			}
			strbuf_free(&header_name);
		    } else {
			strbuf_putc(&header_data, c);
		    }
		    break;
		    
		case st_eoh:
		    break;	/* Should never happen */
		}
	    }
	} while (state != st_eoh && netbuf_next(buf) >= 0);

	if (state != st_eoh)
	    netbuf_delete(buf);
    }

    if (state != st_eoh)
	status = 0;

    switch (status) {
    case 200:
	/*
	 * All OK, need to mark header data consumed and set up a file
	 * structure...
	 */
	break;

    case 301:
    case 302:
    case 303:
    case 307:
	*redirect = location;
	location = NULL;
	break;

    default:
	break;
    }

err_disconnect:
    strbuf_free(&header_name);
    strbuf_free(&header_data);
    strbuf_free(&location);

    if (file)
	return file;

    netconn_disconnect(conn);
err_delete:
    netconn_delete(conn);
    return NULL;
}

