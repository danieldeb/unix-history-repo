/*
 * Copyright (c) 1984, 1985, 1986, 1987 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)ns.h	7.8 (Berkeley) 2/22/91
 *	$Id: ns.h,v 1.3 1993/11/07 17:50:24 wollman Exp $
 */

#ifndef _NETNS_NS_H_
#define _NETNS_NS_H_ 1

/*
 * Constants and Structures defined by the Xerox Network Software
 * per "Internet Transport Protocols", XSIS 028112, December 1981
 */

/*
 * Protocols
 */
#define NSPROTO_RI	1		/* Routing Information */
#define NSPROTO_ECHO	2		/* Echo Protocol */
#define NSPROTO_ERROR	3		/* Error Protocol */
#define NSPROTO_PE	4		/* Packet Exchange */
#define NSPROTO_SPP	5		/* Sequenced Packet */
#define NSPROTO_RAW	255		/* Placemarker*/
#define NSPROTO_MAX	256		/* Placemarker*/


/*
 * Port/Socket numbers: network standard functions
 */

#define NSPORT_RI	1		/* Routing Information */
#define NSPORT_ECHO	2		/* Echo */
#define NSPORT_RE	3		/* Router Error */

/*
 * Ports < NSPORT_RESERVED are reserved for priveleged
 * processes (e.g. root).
 */
#define NSPORT_RESERVED		3000

/* flags passed to ns_output as last parameter */

#define	NS_FORWARDING		0x1	/* most of idp header exists */
#define	NS_ROUTETOIF		0x10	/* same as SO_DONTROUTE */
#define	NS_ALLOWBROADCAST	SO_BROADCAST	/* can send broadcast packets */

#define NS_MAXHOPS		15

/* flags passed to get/set socket option */
#define	SO_HEADERS_ON_INPUT	1
#define	SO_HEADERS_ON_OUTPUT	2
#define	SO_DEFAULT_HEADERS	3
#define	SO_LAST_HEADER		4
#define	SO_NSIP_ROUTE		5
#define SO_SEQNO		6
#define	SO_ALL_PACKETS		7
#define SO_MTU			8


/*
 * NS addressing
 */
union ns_host {
	u_char	c_host[6];
	u_short	s_host[3];
};

union ns_net {
	u_char	c_net[4];
	u_short	s_net[2];
};

union ns_net_u {
	union ns_net	net_e;
	u_long		long_e;
};

struct ns_addr {
	union ns_net	x_net;
	union ns_host	x_host;
	u_short	x_port;
};

/*
 * Socket address, Xerox style
 */
struct sockaddr_ns {
	u_char		sns_len;
	u_char		sns_family;
	struct ns_addr	sns_addr;
	char		sns_zero[2];
};
#define sns_port sns_addr.x_port

#ifdef vax
#define ns_netof(a) (*(long *) & ((a).x_net)) /* XXX - not needed */
#endif
#define ns_neteqnn(a,b) (((a).s_net[0]==(b).s_net[0]) && \
					((a).s_net[1]==(b).s_net[1]))
#define ns_neteq(a,b) ns_neteqnn((a).x_net, (b).x_net)
#define satons_addr(sa)	(((struct sockaddr_ns *)&(sa))->sns_addr)
#define ns_hosteqnh(s,t) ((s).s_host[0] == (t).s_host[0] && \
	(s).s_host[1] == (t).s_host[1] && (s).s_host[2] == (t).s_host[2])
#define ns_hosteq(s,t) (ns_hosteqnh((s).x_host,(t).x_host))
#define ns_nullhost(x) (((x).x_host.s_host[0]==0) && \
	((x).x_host.s_host[1]==0) && ((x).x_host.s_host[2]==0))

#ifdef KERNEL
extern struct domain nsdomain;
extern union ns_host ns_thishost;
extern union ns_host ns_zerohost;
extern union ns_host ns_broadhost;
extern union ns_net ns_zeronet;
extern union ns_net ns_broadnet;
u_short ns_cksum();
extern int ns_err_x(int);
extern void ns_error(struct mbuf *, int, int);
extern void ns_printhost(struct ns_addr *);
extern void ns_err_input(struct mbuf *);
extern int ns_echo(struct mbuf *);
extern void idpip_input(struct mbuf *, struct ifnet *);
extern int nsip_route(struct mbuf *);
extern void nsip_ctlinput(int, struct sockaddr *);
struct in_addr;
extern void nsip_rtchange(struct in_addr *);
extern void ns_init(void);
extern void nsintr(void);
extern u_char nsctlerrmap[];
extern void idp_ctlinput(int, caddr_t);
struct route;
extern int idp_do_route(struct ns_addr *, struct route *);
extern void ns_watch_output(struct mbuf *, struct ifnet *);
extern int ns_output(struct mbuf *, struct route *, int);

struct nspcb;

/* This is a pun for struct protosw in sys/protosw.h: */
struct ns_protosw {
	short	pr_type;		/* socket type used for */
	struct	domain *pr_domain;	/* domain protocol a member of */
	short	pr_protocol;		/* protocol number */
	short	pr_flags;
	void	(*pr_input)(struct mbuf *, struct nspcb *);
	int	(*pr_output)(struct nspcb *, struct mbuf *);
	void	(*pr_ctlinput)(int, caddr_t);
	int	(*pr_ctloutput)(int, struct socket *, int, int, 
				struct mbuf **);
	int	(*pr_usrreq)(struct socket *, int, struct mbuf *, 
			     struct mbuf *, struct mbuf *, struct mbuf *);
	void	(*pr_init)(void); /* initialization hook */
	void	(*pr_fasttimo)(void); /* fast timeout (200ms) */
	void	(*pr_slowtimo)(void); /* slow timeout (500ms) */
	void	(*pr_drain)(void); /* flush any excess space possible */
};

extern struct ns_protosw nssw[];
#else

#include <sys/cdefs.h>

__BEGIN_DECLS
extern struct ns_addr ns_addr __P((const char *));
extern char *ns_ntoa __P((struct ns_addr));
__END_DECLS

#endif /* not KERNEL */
#endif /* _NETNS_NS_H_ */
