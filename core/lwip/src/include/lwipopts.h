#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

#define MEM_SIZE		(1 << 18)
#define MEMP_OVERFLOW_CHECK	2 /* XXX: for debugging */
#define MEMP_SANITY_CHECK	1 /* XXX: for debugging */
#define MEM_USE_POOLS_TRY_BIGGER_POOL	1

#define TCPIP_MBOX_SIZE         64
#define TCPIP_THREAD_PRIO	0
#define TCPIP_THREAD_STACKSIZE	16384

#define MEMP_NUM_TCP_PCB	64
#define MEMP_NUM_TCP_SEG	256
#define MEMP_NUM_REASSDATA	32
#define MEMP_NUM_SYS_TIMEOUT	8
#define ARP_TABLE_SIZE		16
#define IP_REASS_MAX_PBUFS	32

#define LWIP_NETIF_API		1

#define LWIP_DNS		1
#define DNS_MAX_SERVERS		4
#define TCP_WND			32768
#define TCP_MSS			4096
#define TCP_SND_BUF		4096

#define LWIP_PLATFORM_BYTESWAP	1

#endif /* __LWIPOPTS_H__ */
