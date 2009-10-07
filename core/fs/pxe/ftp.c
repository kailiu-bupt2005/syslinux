/*
 * ftp.c
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
#include "netstream.h"
#include "lwip/api.h"
#include "lwip/dns.h"

static int ftp_cmd_response(struct netstream *s, const char *cmd,
			    uint8_t *pasv_data, int *pn_ptr)
{
    int c;
    int pos, code;
    int pb, pn;
    bool first_line, done;
    err_t err;

    err = netconn_write(s->conn, cmd, strlen(cmd), NETCONN_NOCOPY);
    if (err)
	return -1;

    pos = code = pn = pb = 0;
    first_line = true;
    done = false;

    while ((c = netstream_getc(s)) >= 0) {
	switch (pos++) {
	case 0:
	case 1:
	case 2:
	    if (c < '0' || c > '9') {
		if (first_line)
		    return -1;
		else
		    pos = 4;	/* Skip this line */
	    } else {
		code = (code*10) + (c - '0');
	    }
	    break;

	case 3:
	    pn = 0;
	    pb = 0;
	    if (c == ' ')
		done = true;
	    else if (c == '-')
		done = false;
	    else if (first_line)
		return -1;
	    else
		done = false;
	    break;

	default:
	    if (pasv_data) {
		if (c >= '0' && c <= '9') {
		    pb = (pb*10) + (c-'0');
		    if (pn < 6)
			pasv_data[pn] = pb;
		} else if (c == ',') {
		    pn++;
		    pb = 0;
		} else if (pn) {
		    if (pn_ptr)
			*pn_ptr = pn;
		    pn = pb = 0;
		}
	    }
	    if (c == '\n') {
		pos = 0;
		if (done)
		    return code;
	    }
	    break;
	}
    }

    return -1;
}

int ftp_open(struct file *file, struct url_info *url,
	     struct strbuf **redirect)
{
    struct ip_addr ip;
    int rv = -1;
    struct open_file_t *of = file->open_file;
    uint8_t pasv_data[6];
    int pasv_bytes;
    int resp;
    err_t err;

    *redirect = NULL;		/* FTP can't redirect... */

    err = netconn_gethostbyname(url->host, &ip);
    if (err)
	return -1;

    memset(&of->ctl, 0, sizeof of->ctl);
    of->ctl.conn = netconn_new(NETCONN_TCP);
    err = netconn_connect(of->ctl.conn, &ip, url->port ? url->port : 21);
    if (err)
	goto err_delete;

    do {
	resp = ftp_cmd_response(&of->ctl, "", NULL, NULL);
    } while (resp == 120);
    if (resp != 220)
	goto err_disconnect;

    resp = ftp_cmd_response(&of->ctl, "USER anonymous\r\n", NULL, NULL);
    if (resp != 202 && resp != 230) {
	if (resp != 331)
	    goto err_disconnect;

	resp = ftp_cmd_response(&of->ctl, "PASS pxelinux@\r\n", NULL, NULL);
	if (resp != 230)
	    goto err_disconnect;
    }

    resp = ftp_cmd_response(&of->ctl, "TYPE I\r\n", NULL, NULL);
    if (resp != 200)
	goto err_disconnect;

    resp = ftp_cmd_response(&of->ctl, "PASV\r\n", pasv_data, &pasv_bytes);
    if (resp != 227 || pasv_bytes != 6)
	goto err_disconnect;

    resp = ftp_cmd_response(&of->ctl, "RETR filename\r\n", NULL, NULL);
    if (resp != 125 && resp != 150)
	goto err_disconnect;

    memset(&of->data, 0, sizeof of->data);
    of->data.conn = netconn_new(NETCONN_TCP);
    err = netconn_connect(of->data.conn, (struct ip_addr *)&pasv_data[0],
			  ntohs(*(uint16_t *)&pasv_data[4]));
    if (err)
	goto err_disconnect;

err_disconnect:
    if (of->data.conn)
	netconn_delete(of->data.conn);

    if (of->ctl.buf)
	netbuf_delete(of->ctl.buf);
    netconn_disconnect(of->ctl.conn);
err_delete:
    netconn_delete(of->ctl.conn);
    return rv;
}

