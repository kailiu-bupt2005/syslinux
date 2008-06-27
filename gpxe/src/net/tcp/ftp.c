#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <byteswap.h>
#include <gpxe/socket.h>
#include <gpxe/tcpip.h>
#include <gpxe/in.h>
#include <gpxe/xfer.h>
#include <gpxe/open.h>
#include <gpxe/uri.h>
#include <gpxe/features.h>
#include <gpxe/ftp.h>

/** @file
 *
 * File transfer protocol
 *
 */

FEATURE ( FEATURE_PROTOCOL, "FTP", DHCP_EB_FEATURE_FTP, 1 );

/**
 * FTP states
 *
 * These @b must be sequential, i.e. a successful FTP session must
 * pass through each of these states in order.
 */
enum ftp_state {
	FTP_CONNECT = 0,
	FTP_USER,
	FTP_PASS,
	FTP_TYPE,
	FTP_PASV,
	FTP_RETR,
	FTP_QUIT,
	FTP_DONE,
};

/**
 * An FTP request
 *
 */
struct ftp_request {
	/** Reference counter */
	struct refcnt refcnt;
	/** Data transfer interface */
	struct xfer_interface xfer;

	/** URI being fetched */
	struct uri *uri;
	/** FTP control channel interface */
	struct xfer_interface control;
	/** FTP data channel interface */
	struct xfer_interface data;

	/** Current state */
	enum ftp_state state;
	/** Buffer to be filled with data received via the control channel */
	char *recvbuf;
	/** Remaining size of recvbuf */
	size_t recvsize;
	/** FTP status code, as text */
	char status_text[5];
	/** Passive-mode parameters, as text */
	char passive_text[24]; /* "aaa,bbb,ccc,ddd,eee,fff" */
};

/**
 * Free FTP request
 *
 * @v refcnt		Reference counter
 */
static void ftp_free ( struct refcnt *refcnt ) {
	struct ftp_request *ftp =
		container_of ( refcnt, struct ftp_request, refcnt );

	DBGC ( ftp, "FTP %p freed\n", ftp );

	uri_put ( ftp->uri );
	free ( ftp );
}

/**
 * Mark FTP operation as complete
 *
 * @v ftp		FTP request
 * @v rc		Return status code
 */
static void ftp_done ( struct ftp_request *ftp, int rc ) {

	DBGC ( ftp, "FTP %p completed (%s)\n", ftp, strerror ( rc ) );

	/* Close all data transfer interfaces */
	xfer_nullify ( &ftp->xfer );
	xfer_close ( &ftp->xfer, rc );
	xfer_nullify ( &ftp->control );
	xfer_close ( &ftp->control, rc );
	xfer_nullify ( &ftp->data );
	xfer_close ( &ftp->data, rc );
}

/*****************************************************************************
 *
 * FTP control channel
 *
 */

/**
 * FTP control channel strings
 *
 * These are used as printf() format strings.  Since only one of them
 * (RETR) takes an argument, we always supply that argument to the
 * snprintf() call.
 */
static const char * ftp_strings[] = {
	[FTP_CONNECT]	= "",
	[FTP_USER]	= "USER anonymous\r\n",
	[FTP_PASS]	= "PASS etherboot@etherboot.org\r\n",
	[FTP_TYPE]	= "TYPE I\r\n",
	[FTP_PASV]	= "PASV\r\n",
	[FTP_RETR]	= "RETR %s\r\n", 
	[FTP_QUIT]	= "QUIT\r\n",
	[FTP_DONE]	= "",
};

/**
 * Handle control channel being closed
 *
 * @v control		FTP control channel interface
 * @v rc		Reason for close
 *
 * When the control channel is closed, the data channel must also be
 * closed, if it is currently open.
 */
static void ftp_control_close ( struct xfer_interface *control, int rc ) {
	struct ftp_request *ftp =
		container_of ( control, struct ftp_request, control );

	DBGC ( ftp, "FTP %p control connection closed: %s\n",
	       ftp, strerror ( rc ) );

	/* Complete FTP operation */
	ftp_done ( ftp, rc );
}

/**
 * Parse FTP byte sequence value
 *
 * @v text		Text string
 * @v value		Value buffer
 * @v len		Length of value buffer
 *
 * This parses an FTP byte sequence value (e.g. the "aaa,bbb,ccc,ddd"
 * form for IP addresses in PORT commands) into a byte sequence.  @c
 * *text will be updated to point beyond the end of the parsed byte
 * sequence.
 *
 * This function is safe in the presence of malformed data, though the
 * output is undefined.
 */
static void ftp_parse_value ( char **text, uint8_t *value, size_t len ) {
	do {
		*(value++) = strtoul ( *text, text, 10 );
		if ( **text )
			(*text)++;
	} while ( --len );
}

/**
 * Handle an FTP control channel response
 *
 * @v ftp		FTP request
 *
 * This is called once we have received a complete response line.
 */
static void ftp_reply ( struct ftp_request *ftp ) {
	char status_major = ftp->status_text[0];
	char separator = ftp->status_text[3];

	DBGC ( ftp, "FTP %p received status %s\n", ftp, ftp->status_text );

	/* Ignore malformed lines */
	if ( separator != ' ' )
		return;

	/* Ignore "intermediate" responses (1xx codes) */
	if ( status_major == '1' )
		return;

	/* Anything other than success (2xx) or, in the case of a
	 * repsonse to a "USER" command, a password prompt (3xx), is a
	 * fatal error.
	 */
	if ( ! ( ( status_major == '2' ) ||
		 ( ( status_major == '3' ) && ( ftp->state == FTP_USER ) ) ) ){
		/* Flag protocol error and close connections */
		ftp_done ( ftp, -EPROTO );
	}

	/* Open passive connection when we get "PASV" response */
	if ( ftp->state == FTP_PASV ) {
		char *ptr = ftp->passive_text;
		union {
			struct sockaddr_in sin;
			struct sockaddr sa;
		} sa;
		int rc;

		sa.sin.sin_family = AF_INET;
		ftp_parse_value ( &ptr, ( uint8_t * ) &sa.sin.sin_addr,
				  sizeof ( sa.sin.sin_addr ) );
		ftp_parse_value ( &ptr, ( uint8_t * ) &sa.sin.sin_port,
				  sizeof ( sa.sin.sin_port ) );
		if ( ( rc = xfer_open_socket ( &ftp->data, SOCK_STREAM,
					       &sa.sa, NULL ) ) != 0 ) {
			DBGC ( ftp, "FTP %p could not open data connection\n",
			       ftp );
			ftp_done ( ftp, rc );
			return;
		}
	}

	/* Move to next state */
	if ( ftp->state < FTP_DONE )
		ftp->state++;

	/* Send control string */
	if ( ftp->state < FTP_DONE ) {
		DBGC ( ftp, "FTP %p sending ", ftp );
		DBGC ( ftp, ftp_strings[ftp->state], ftp->uri->path );
		xfer_printf ( &ftp->control, ftp_strings[ftp->state],
			      ftp->uri->path );
	}
}

/**
 * Handle new data arriving on FTP control channel
 *
 * @v control		FTP control channel interface
 * @v data		New data
 * @v len		Length of new data
 *
 * Data is collected until a complete line is received, at which point
 * its information is passed to ftp_reply().
 */
static int ftp_control_deliver_raw ( struct xfer_interface *control,
				     const void *data, size_t len ) {
	struct ftp_request *ftp =
		container_of ( control, struct ftp_request, control );
	char *recvbuf = ftp->recvbuf;
	size_t recvsize = ftp->recvsize;
	char c;
	
	while ( len-- ) {
		c = * ( ( char * ) data++ );
		switch ( c ) {
		case '\r' :
		case '\n' :
			/* End of line: call ftp_reply() to handle
			 * completed reply.  Avoid calling ftp_reply()
			 * twice if we receive both \r and \n.
			 */
			if ( recvsize == 0 )
				ftp_reply ( ftp );
			/* Start filling up the status code buffer */
			recvbuf = ftp->status_text;
			recvsize = sizeof ( ftp->status_text ) - 1;
			break;
		case '(' :
			/* Start filling up the passive parameter buffer */
			recvbuf = ftp->passive_text;
			recvsize = sizeof ( ftp->passive_text ) - 1;
			break;
		case ')' :
			/* Stop filling the passive parameter buffer */
			recvsize = 0;
			break;
		default :
			/* Fill up buffer if applicable */
			if ( recvsize > 0 ) {
				*(recvbuf++) = c;
				recvsize--;
			}
			break;
		}
	}

	/* Store for next invocation */
	ftp->recvbuf = recvbuf;
	ftp->recvsize = recvsize;

	return 0;
}

/** FTP control channel operations */
static struct xfer_interface_operations ftp_control_operations = {
	.close		= ftp_control_close,
	.vredirect	= xfer_vopen,
	.window		= unlimited_xfer_window,
	.alloc_iob	= default_xfer_alloc_iob,
	.deliver_iob	= xfer_deliver_as_raw,
	.deliver_raw	= ftp_control_deliver_raw,
};

/*****************************************************************************
 *
 * FTP data channel
 *
 */

/**
 * Handle FTP data channel being closed
 *
 * @v data		FTP data channel interface
 * @v rc		Reason for closure
 *
 * When the data channel is closed, the control channel should be left
 * alone; the server will send a completion message via the control
 * channel which we'll pick up.
 *
 * If the data channel is closed due to an error, we abort the request.
 */
static void ftp_data_closed ( struct xfer_interface *data, int rc ) {
	struct ftp_request *ftp =
		container_of ( data, struct ftp_request, data );

	DBGC ( ftp, "FTP %p data connection closed: %s\n",
	       ftp, strerror ( rc ) );
	
	/* If there was an error, close control channel and record status */
	if ( rc )
		ftp_done ( ftp, rc );
}

/**
 * Handle data delivery via FTP data channel
 *
 * @v xfer		FTP data channel interface
 * @v iobuf		I/O buffer
 * @v meta		Data transfer metadata, or NULL
 * @ret rc		Return status code
 */
static int ftp_data_deliver_iob ( struct xfer_interface *data,
				  struct io_buffer *iobuf,
				  struct xfer_metadata *meta __unused ) {
	struct ftp_request *ftp =
		container_of ( data, struct ftp_request, data );
	int rc;

	if ( ( rc = xfer_deliver_iob ( &ftp->xfer, iobuf ) ) != 0 ) {
		DBGC ( ftp, "FTP %p failed to deliver data: %s\n",
		       ftp, strerror ( rc ) );
		return rc;
	}

	return 0;
}

/** FTP data channel operations */
static struct xfer_interface_operations ftp_data_operations = {
	.close		= ftp_data_closed,
	.vredirect	= xfer_vopen,
	.window		= unlimited_xfer_window,
	.alloc_iob	= default_xfer_alloc_iob,
	.deliver_iob	= ftp_data_deliver_iob,
	.deliver_raw	= xfer_deliver_as_iob,
};

/*****************************************************************************
 *
 * Data transfer interface
 *
 */

/**
 * Close FTP data transfer interface
 *
 * @v xfer		FTP data transfer interface
 * @v rc		Reason for close
 */
static void ftp_xfer_closed ( struct xfer_interface *xfer, int rc ) {
	struct ftp_request *ftp =
		container_of ( xfer, struct ftp_request, xfer );

	DBGC ( ftp, "FTP %p data transfer interface closed: %s\n",
	       ftp, strerror ( rc ) );
	
	ftp_done ( ftp, rc );
}

/** FTP data transfer interface operations */
static struct xfer_interface_operations ftp_xfer_operations = {
	.close		= ftp_xfer_closed,
	.vredirect	= ignore_xfer_vredirect,
	.window		= unlimited_xfer_window,
	.alloc_iob	= default_xfer_alloc_iob,
	.deliver_iob	= xfer_deliver_as_raw,
	.deliver_raw	= ignore_xfer_deliver_raw,
};

/*****************************************************************************
 *
 * URI opener
 *
 */

/**
 * Initiate an FTP connection
 *
 * @v xfer		Data transfer interface
 * @v uri		Uniform Resource Identifier
 * @ret rc		Return status code
 */
static int ftp_open ( struct xfer_interface *xfer, struct uri *uri ) {
	struct ftp_request *ftp;
	struct sockaddr_tcpip server;
	int rc;

	/* Sanity checks */
	if ( ! uri->path )
		return -EINVAL;
	if ( ! uri->host )
		return -EINVAL;

	/* Allocate and populate structure */
	ftp = zalloc ( sizeof ( *ftp ) );
	if ( ! ftp )
		return -ENOMEM;
	ftp->refcnt.free = ftp_free;
	xfer_init ( &ftp->xfer, &ftp_xfer_operations, &ftp->refcnt );
	ftp->uri = uri_get ( uri );
	xfer_init ( &ftp->control, &ftp_control_operations, &ftp->refcnt );
	xfer_init ( &ftp->data, &ftp_data_operations, &ftp->refcnt );
	ftp->recvbuf = ftp->status_text;
	ftp->recvsize = sizeof ( ftp->status_text ) - 1;

	DBGC ( ftp, "FTP %p fetching %s\n", ftp, ftp->uri->path );

	/* Open control connection */
	memset ( &server, 0, sizeof ( server ) );
	server.st_port = htons ( uri_port ( uri, FTP_PORT ) );
	if ( ( rc = xfer_open_named_socket ( &ftp->control, SOCK_STREAM,
					     ( struct sockaddr * ) &server,
					     uri->host, NULL ) ) != 0 )
		goto err;

	/* Attach to parent interface, mortalise self, and return */
	xfer_plug_plug ( &ftp->xfer, xfer );
	ref_put ( &ftp->refcnt );
	return 0;

 err:
	DBGC ( ftp, "FTP %p could not create request: %s\n", 
	       ftp, strerror ( rc ) );
	ftp_done ( ftp, rc );
	ref_put ( &ftp->refcnt );
	return rc;
}

/** FTP URI opener */
struct uri_opener ftp_uri_opener __uri_opener = {
	.scheme	= "ftp",
	.open	= ftp_open,
};