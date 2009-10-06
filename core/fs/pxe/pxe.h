/* -----------------------------------------------------------------------
 *
 *   Copyright 1999-2008 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * pxe.h
 *
 * PXE opcodes
 *
 */
#ifndef PXE_H
#define PXE_H

#include <syslinux/pxe_api.h>
#include "lwip/api.h"
#include "netstream.h"
#include "fs.h"			/* For MAX_OPEN, should go away */

/*
 * Some basic defines...
 */
#define TFTP_PORT        htons(69)              /* Default TFTP port */
#define TFTP_BLOCKSIZE_LG2 9
#define TFTP_BLOCKSIZE  (1 << TFTP_BLOCKSIZE_LG2)
#define PKTBUF_SEG      0x4000
#define PKTBUF_SIZE     (65536 / MAX_OPEN)

#define is_digit(c)     (((c) >= '0') && ((c) <= '9'))
#define major_ver(v)    (((v) >> 8) && 0xff)

/*
 * TFTP operation codes
 */
#define TFTP_RRQ	 htons(1)		// Read rest
#define TFTP_WRQ	 htons(2)		// Write rest
#define TFTP_DATA	 htons(3)		// Data packet
#define TFTP_ACK	 htons(4)		// ACK packet
#define TFTP_ERROR	 htons(5)		// ERROR packet
#define TFTP_OACK	 htons(6)		// OACK packet

/*
 * TFTP error codes
 */
#define TFTP_EUNDEF	 htons(0)		// Unspecified error
#define TFTP_ENOTFOUND	 htons(1)		// File not found
#define TFTP_EACCESS	 htons(2)		// Access violation
#define TFTP_ENOSPACE	 htons(3)		// Disk full
#define TFTP_EBADOP	 htons(4)		// Invalid TFTP operation
#define TFTP_EBADID	 htons(5)		// Unknown transfer
#define TFTP_EEXISTS	 htons(6)		// File exists
#define TFTP_ENOUSER	 htons(7)		// No such user
#define TFTP_EOPTNEG	 htons(8)		// Option negotiation failure


#define BOOTP_OPTION_MAGIC  htonl(0x63825363)
#define MAC_MAX 32

/* Defines for DNS */
#define DNS_PORT	htons(53)		/* Default DNS port */
#define DNS_MAX_PACKET	512			/* Defined by protocol */
/* All local DNS queries come from this port */
#define DNS_LOCAL_PORT	htons(60053) 
#define DNS_MAX_SERVERS 4			/* Max no of DNS servers */


/*
 * structures 
 */

struct pxenv_t {
    uint8_t    signature[6];	/* PXENV+ */
    uint16_t   version;
    uint8_t    length;
    uint8_t    checksum;
    segoff16_t rmentry;
    uint32_t   pmoffset;
    uint16_t   pmselector;
    uint16_t   stackseg;
    uint16_t   stacksize;
    uint16_t   bc_codeseg;
    uint16_t   bc_codesize;
    uint16_t   bc_dataseg;
    uint16_t   bc_datasize;
    uint16_t   undidataseg;
    uint16_t   undidatasize;
    uint16_t   undicodeseg;
    uint16_t   undicodesize;
    segoff16_t pxeptr;
} __packed;

struct pxe_t {
    uint8_t    signature[4];	/* !PXE */
    uint8_t    structlength;
    uint8_t    structcksum;
    uint8_t    structrev;
    uint8_t    _pad1;
    segoff16_t undiromid;
    segoff16_t baseromid;
    segoff16_t entrypointsp;
    segoff16_t entrypointesp;
    segoff16_t statuscallout;
    uint8_t    _pad2;
    uint8_t    segdesccnt;
    uint16_t   firstselector;
    pxe_segdesc_t  seg[7];
} __packed;

enum pxe_segments {
    PXE_Seg_Stack         = 0,
    PXE_Seg_UNDIData      = 1,
    PXE_Seg_UNDICode      = 2,
    PXE_Seg_UNDICodeWrite = 3,
    PXE_Seg_BC_Data       = 4,
    PXE_Seg_BC_Code       = 5,
    PXE_Seg_BC_CodeWrite  = 6
};

struct bootp_t {
    uint8_t  opcode;        /* BOOTP/DHCP "opcode" */
    uint8_t  hardware;      /* ARP hreadware type */
    uint8_t  hardlen;       /* Hardware address length */
    uint8_t  gatehops;      /* Used by forwarders */
    uint32_t ident;         /* Transaction ID */
    uint16_t seconds;       /* Seconds elapsed */
    uint16_t flags;         /* Broadcast flags */
    uint32_t cip;           /* Cient IP */
    uint32_t yip;           /* "Your" IP */
    uint32_t sip;           /* Next Server IP */
    uint32_t gip;           /* Relay agent IP */
    uint8_t  macaddr[16];   /* Client MAC address */
    uint8_t  sname[64];     /* Server name (optional) */
    char     bootfile[128]; /* Boot file name */
    uint32_t option_magic;  /* Vendor option magic cookie */
    uint8_t  options[1260]; /* Vendor options */
} __attribute__ ((packed));

struct open_file_t {
    struct netstream data;	/* Data network connection */
    struct netstream ctl;	/* Control network connection (used by FTP) */

    uint16_t tftp_localport;   /* Local port number  (0=not in us)*/
    uint16_t tftp_remoteport;  /* Remote port number */
    uint32_t tftp_remoteip;    /* Remote IP address */
    uint32_t tftp_filepos;     /* bytes downloaded (includeing buffer) */
    uint32_t tftp_filesize;    /* Total file size(*) */
    uint32_t tftp_blksize;     /* Block size for this connection(*) */
    uint16_t tftp_bytesleft;   /* Unclaimed data bytes */
    uint16_t tftp_lastpkt;     /* Sequence number of last packet (NBO) */
    uint16_t tftp_dataptr;     /* Pointer to available data */
    uint8_t  tftp_goteof;      /* 1 if the EOF packet received */
    uint8_t  tftp_unused;      /* Currently unused */
    /* These values are preinitialized and not zeroed on close */
    uint16_t tftp_nextport;    /* Next port number for this slot (HBO) */
    uint16_t tftp_pktbuf;      /* Packet buffer offset */
} __attribute__ ((packed));

/*
 * Variable externs
 */
extern uint32_t server_ip;
extern uint32_t MyIP;
extern uint32_t net_mask;
extern uint32_t gate_way;
extern uint16_t server_port;

extern char MAC_str[];
extern char MAC[];
extern char BOOTIFStr[];
extern uint8_t MAC_len;
extern uint8_t MAC_type;

extern uint8_t  DHCPMagic;
extern uint32_t RebootTime;

extern char boot_file[];
extern char path_prefix[];
extern char LocalDomain[];

extern char packet_buf[];

extern char IPOption[];
extern char dot_quad_buf[];

extern uint32_t dns_server[];

extern uint16_t real_base_mem;
extern uint16_t APIVer;
extern far_ptr_t PXEEntry;
extern uint8_t KeepPXE;

extern far_ptr_t InitStack;

extern int have_uuid;
extern uint8_t uuid_type;
extern char uuid[];

extern uint16_t BIOS_fbm;
extern const uint8_t TimeoutTable[];


/*
 * functions 
 */

/* pxe.c */
int ip_ok(uint32_t);
int pxe_call(int, void *);

/* dhcp_options.c */
void parse_dhcp(int);
void parse_dhcp_options(void *, int, int);

/* dnsresolv.c */
int dns_mangle(char **, const char *);
uint32_t dns_resolv(const char *);

/* idle.c */
void pxe_idle_init(void);
void pxe_idle_cleanup(void);

#endif /* pxe.h */
