/* xensocket.c
 *
 * XVMSocket module for a shared-memory sockets transport for communications
 * between two domains on the same machine, under the Xen hypervisor.
 *
 * Authors: Xiaolan (Catherine) Zhang <cxzhang@us.ibm.com>
 *          Suzanne McIntosh <skranjac@us.ibm.com>
 *          John Griffin
 *
 * History:   
 *          Suzanne McIntosh    13-Aug-07     Initial open source version
 *
 * Copyright (c) 2007, IBM Corporation
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>

#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif

#include <net/compat.h>
#include <net/sock.h>
#include <net/tcp_states.h>

//#include <xen/driver_util.h>
//#include <xen/gnttab.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/interface/event_channel.h>
#include <xen/interface/grant_table.h>
#include <xen/interface/xen.h>
#include <xen/evtchn.h>
#include <xen/xenbus.h>

#include <asm/xen/page.h>

#include "xensocket.h"

#define DPRINTK( x, args... ) printk(KERN_CRIT "%s: line %d: " x, __FUNCTION__ , (int)__LINE__ , ## args ); 

//#define DEBUG
#ifdef DEBUG
#define TRACE_ENTRY printk(KERN_CRIT "Entering %s\n", __func__)
#define TRACE_EXIT  printk(KERN_CRIT "Exiting %s\n", __func__)
#else
#define TRACE_ENTRY do {} while (0)
#define TRACE_EXIT  do {} while (0)
#endif
#define TRACE_ERROR printk(KERN_CRIT "Exiting (ERROR) %s\n", __func__)

struct descriptor_page;
struct xen_sock;

static void initialize_descriptor_page (struct descriptor_page *d);
static void initialize_xen_sock (struct xen_sock *x);

static int xen_create (struct net *net, struct socket *sock, int protocol, int kern);
static int xen_bind (struct socket *sock, struct sockaddr *uaddr, int addr_len);
static int xen_release (struct socket *sock);
static int xen_shutdown (struct socket *sock, int how);
static int xen_connect (struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags);
static int xen_sendmsg (struct socket *sock, struct msghdr *m, size_t len);
static int xen_recvmsg (struct socket *sock, struct msghdr *m, size_t size, int flags);
static int xen_accept(struct socket *sock, struct socket *newsock, int flags, bool kern);
static int xen_getname(struct socket *sock, struct sockaddr *addr, int *sockaddr_len, int peer);
static int xen_listen (struct socket *sock, int backlog);

static void xen_watch_accept(struct xenbus_watch *xbw, const char *path, const char *token);
static void xen_watch_connect(struct xenbus_watch *xbw, const char *path, const char *token);
static int server_allocate_descriptor_page (struct xen_sock *x);
static int server_allocate_event_channel (struct xen_sock *x);
static int server_allocate_buffer_pages (struct xen_sock *x);
static int client_map_descriptor_page (struct xen_sock *x);
static int client_bind_event_channel (struct xen_sock *x);
static int client_map_buffer_pages (struct xen_sock *x);
static inline int is_writeable (struct descriptor_page *d);
static long send_data_wait (struct sock *sk, long timeo);
static irqreturn_t client_interrupt (int irq, void *dev_id);
static inline int is_readable (struct descriptor_page *d);
static long receive_data_wait (struct sock *sk, long timeo);
static irqreturn_t server_interrupt (int irq, void *dev_id);
static int local_memcpy_toiovecend (const struct iovec *iov, unsigned char *kdata, int offset, int len);
static void server_unallocate_buffer_pages (struct xen_sock *x);
static void server_unallocate_descriptor_page (struct xen_sock *x);
static void client_unmap_buffer_pages (struct xen_sock *x);
static void client_unmap_descriptor_page (struct xen_sock *x);
static int __init xensocket_init (void);
static void __exit xensocket_exit (void);

/************************************************************************
 * Data structures for internal recordkeeping and shared memory.
 ************************************************************************/

struct descriptor_page {
	uint32_t        server_evtchn_port;
	int             buffer_order; /* num_pages = (1 << buffer_order) */
	int             buffer_first_gref;
	unsigned int    send_offset;
	unsigned int    recv_offset;
	unsigned int    total_bytes_sent;
	unsigned int    total_bytes_received;
	unsigned int    sender_is_blocking;
	atomic_t        avail_bytes;
	atomic_t        sender_has_shutdown;
	atomic_t        force_sender_shutdown;
};

	static void
initialize_descriptor_page (struct descriptor_page *d)
{
	d->server_evtchn_port = -1;
	d->buffer_order = -1;
	d->buffer_first_gref = -ENOSPC;
	d->send_offset = 0;
	d->recv_offset = 0;
	d->total_bytes_sent = 0;
	d->total_bytes_received = 0;
	d->sender_is_blocking = 0;
	atomic_set(&d->avail_bytes, 0);
	atomic_set(&d->sender_has_shutdown, 0);
	atomic_set(&d->force_sender_shutdown, 0);
}

/* struct xen_sock:
 *
 * @sk: this must be the first element in the structure.
 */
struct xen_sock {
	struct sock             sk;
	unsigned char           is_server, is_client;
	domid_t                 otherend_id;
    char                    service[XENSRVLEN];
	struct descriptor_page *descriptor_addr;    /* server and client */
	int                     descriptor_gref;    /* server only */
	struct vm_struct       *descriptor_area;    /* client only */
	grant_handle_t          descriptor_handle;  /* client only */
	unsigned int            evtchn_local_port;
	unsigned int            irq;
	unsigned long           buffer_addr;    /* server and client */
	int                    *buffer_grefs;   /* server */
	struct vm_struct       *buffer_area;    /* client */
	grant_handle_t         *buffer_handles; /* client */
	int                     buffer_order;
};

static void
initialize_xen_sock (struct xen_sock *x) {
	x->is_server = 0;
	x->is_client = 0;
	x->otherend_id = -1;
	x->descriptor_addr = NULL;
	x->descriptor_gref = -ENOSPC;
	x->descriptor_area = NULL;
	x->descriptor_handle = -1;
	x->evtchn_local_port = -1;
	x->irq = -1;
	x->buffer_addr = 0;
	x->buffer_area = NULL;
	x->buffer_handles = NULL;
	x->buffer_order = -1;
}

/* struct xensocket_xenbus_watch:
 * 
 * @xbw: this must be the first element in the structure.
 */
struct xensocket_xenbus_watch {
    struct xenbus_watch xbw;
    struct semaphore sem;
    struct xenbus_transaction xbt;
    unsigned int path_len;
    int gref;
    domid_t domid;
};

static struct proto xen_proto = {
	.name           = "XEN",
	.owner          = THIS_MODULE,
	.obj_size       = sizeof(struct xen_sock),
};

static int mydomid;

static const struct proto_ops xen_stream_ops = {
	.family         = AF_XEN,
	.owner          = THIS_MODULE,
	.release        = xen_release,
	.bind           = xen_bind,
	.connect        = xen_connect,
	.socketpair     = sock_no_socketpair,
	.accept         = xen_accept,
	.getname        = xen_getname,
	.poll           = sock_no_poll,
	.ioctl          = sock_no_ioctl,
	.listen         = xen_listen,
	.shutdown       = xen_shutdown,
	.getsockopt     = sock_no_getsockopt,
	.setsockopt     = sock_no_setsockopt,
	.sendmsg        = xen_sendmsg,
	.recvmsg        = xen_recvmsg,
	.mmap           = sock_no_mmap,
	.sendpage       = sock_no_sendpage,
};

static struct net_proto_family xen_family_ops = {
	.family         = AF_XEN,
	.create         = xen_create,
	.owner          = THIS_MODULE,
};

static int
xen_shutdown (struct socket *sock, int how) {
	struct sock *sk = sock->sk;
	struct xen_sock *x;
	struct descriptor_page *d;

	x = xen_sk(sk);
	d = x->descriptor_addr;

	if (x->is_server) {
		atomic_set(&d->force_sender_shutdown, 1);
	}

	return xen_release(sock);
}

/************************************************************************
 * Socket initialization (common to both server and client code).
 *
 * When a user-level program calls socket(), the xen_create() function
 * is called to set up the local structures (struct sock) that describe
 * the socket.  Our treatment of this is currently simple; there are a
 * lot of components of the sock structure that we do not use.  For
 * comparison, see the function unix_create in linux/net/unix/af_unix.c.
 ************************************************************************/

static int
xen_create (struct net *net, struct socket *res_sock, int protocol, int kern) {
	int    rc = 0;
	struct sock *sk;
	struct xen_sock *x;

	TRACE_ENTRY;
    DPRINTK("res_sock@%p\n", res_sock);

	res_sock->state = SS_UNCONNECTED;

	switch (res_sock->type) {
		case SOCK_STREAM:
			res_sock->ops = &xen_stream_ops;
			break;
		default:
			rc = -ESOCKTNOSUPPORT;
			goto out;
	}

	printk(KERN_CRIT "pfxen: sk_alloc");
	sk = sk_alloc(net, PF_XEN, GFP_KERNEL, &xen_proto, 1);
	if (!sk) {
		rc = -ENOMEM;
		goto out;
	}
	printk(KERN_CRIT "pfxen: sock_init_data");
	sock_init_data(res_sock, sk);

	sk->sk_family   = PF_XEN;
	sk->sk_protocol = protocol;
	x = xen_sk(sk);
	printk(KERN_CRIT "pfxen: initialize_xen_sock");
	initialize_xen_sock(x);
	printk(KERN_CRIT "pfxen: created socket");

out:
	TRACE_EXIT;
	return rc;
}

/************************************************************************
 * Server-side connection setup functions.
 ************************************************************************/

/* In our nonstandard use of the bind function, the return value is the
 * grant table entry of the descriptor page.
 */
static int
xen_bind (struct socket *sock, struct sockaddr *uaddr, int addr_len) {
	int    rc = -EINVAL;
	struct sock *sk = sock->sk;
	struct xen_sock *x = xen_sk(sk);
	struct sockaddr_xe *sxeaddr = (struct sockaddr_xe *)uaddr;
	struct xenbus_transaction t;

	TRACE_ENTRY;
    DPRINTK("sock@%p\n", sock);

	if (sxeaddr->sxe_family != AF_XEN) {
		goto err;
	}
    DPRINTK("bind to service = %s\n", sxeaddr->service);

    // store sxeaddr->service in socket
    strcpy(x->service, sxeaddr->service);

	/* Ensure that bind() is only called once for this socket.
	 */

	if (x->is_server) {
		DPRINTK("error: cannot call bind() more than once on a socket\n");
		goto err;
	}
	if (x->is_client) {
		DPRINTK("error: cannot call both bind() and connect() on the same socket\n");
		goto err;
	}
	x->is_server = 1;

	xenbus_transaction_start(&t);
    if(xenbus_exists(t, "/xensocket/service", sxeaddr->service)) {
        DPRINTK("error: cannot bind(): /xensocket/service/%s is already in use\n", sxeaddr->service);
        xenbus_transaction_end(t, 0);
        goto err;
    }
	xenbus_transaction_end(t, 0);

	TRACE_EXIT;

	return x->descriptor_gref;

err:
	TRACE_ERROR;
	return rc;
}

static int
server_allocate_descriptor_page (struct xen_sock *x) {
	TRACE_ENTRY;

	if (x->descriptor_addr) {
		DPRINTK("error: already allocated server descriptor page\n");
		goto err;
	}

	if (!(x->descriptor_addr = (struct descriptor_page *)__get_free_page(GFP_KERNEL))) {
		DPRINTK("error: cannot allocate free page\n");
		goto err_unalloc;
	}

	initialize_descriptor_page(x->descriptor_addr);

	if ((x->descriptor_gref = gnttab_grant_foreign_access(x->otherend_id, virt_to_mfn(x->descriptor_addr), 0)) == -ENOSPC) {
		DPRINTK("error: cannot share descriptor page %p\n", x->descriptor_addr);
		goto err_unalloc;
	}

	TRACE_EXIT;
	return 0;

err_unalloc:
	server_unallocate_descriptor_page(x);

err:
	TRACE_ERROR;
	return -ENOMEM;
}

static int
server_allocate_event_channel (struct xen_sock *x) {
	struct evtchn_alloc_unbound op;
	int         rc;

	TRACE_ENTRY;

	op.dom = mydomid;
	op.remote_dom = x->otherend_id;
	
	printk(KERN_CRIT "own id: %d\n", op.dom);
	printk(KERN_CRIT "other end id: %d\n", op.remote_dom);

	if ((rc = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &op)) != 0) {
		DPRINTK("Unable to allocate event channel\n");
		goto err;
	}

	x->evtchn_local_port = op.port;
	x->descriptor_addr->server_evtchn_port = x->evtchn_local_port;

	/* Next bind this end of the event channel to our local callback
	 * function. */

	if ((rc = bind_evtchn_to_irqhandler(x->evtchn_local_port, server_interrupt, 0, "xensocket", x)) <= 0) {
		DPRINTK("Unable to bind event channel to irqhandler\n");
		goto err;
	}

	TRACE_EXIT;
	return 0;

err:
	TRACE_ERROR;
	return rc;
}

static int
server_allocate_buffer_pages (struct xen_sock *x) {
	struct descriptor_page *d = x->descriptor_addr;
	int    buffer_num_pages;
	int    i;

	TRACE_ENTRY;

	if (!d) {
		/* must call server_allocate_descriptor_page first */
		DPRINTK("error: descriptor page not yet allocated\n");
		goto err;
	}

	if (x->buffer_addr) {
		DPRINTK("error: already allocated server buffer pages\n");
		goto err;
	}

	x->buffer_order = 5;  //32 pages    /* you can change this as desired */
	buffer_num_pages = (1 << x->buffer_order);

	if (!(x->buffer_addr = __get_free_pages(GFP_KERNEL, x->buffer_order))) {
		DPRINTK("error: cannot allocate %d pages\n", buffer_num_pages);
		goto err;
	}

	if (!(x->buffer_grefs = kmalloc(buffer_num_pages * sizeof(int), GFP_KERNEL))) {
		DPRINTK("error: unexpected memory allocation failure\n");
		goto err_unallocate;
	} 
	else {
		/* Success, so first invalidate all the entries */
		for (i = 0; i < buffer_num_pages; i++) {
			x->buffer_grefs[i] = -ENOSPC;
		}
	}

	printk("x->buffer_addr = %lx  PAGE_SIZE = %li  buffer_num_pages = %d\n", x->buffer_addr, PAGE_SIZE, buffer_num_pages);
	for (i = 0; i < buffer_num_pages; i++) {
		if ((x->buffer_grefs[i] = gnttab_grant_foreign_access(x->otherend_id, virt_to_mfn(x->buffer_addr + i * PAGE_SIZE), 0)) == -ENOSPC) {
			DPRINTK("error: cannot share buffer page #%d\n", i);
			goto err_unallocate;
		}
	}

	/* In this scheme, we initially use each page to hold
	 * the grant table reference for the next page.  The client maps
	 * the next page by reading the gref from the current page.
	 */

	d->buffer_first_gref = x->buffer_grefs[0];
	for (i = 1; i < buffer_num_pages; i++) {
		int *next_gref = (int *)(x->buffer_addr + (i-1) * PAGE_SIZE);
		*next_gref = x->buffer_grefs[i];
	}

	d->buffer_order = x->buffer_order;
	atomic_set(&d->avail_bytes, (1 << d->buffer_order) * PAGE_SIZE);

	TRACE_EXIT;
	return 0;

err_unallocate:
	server_unallocate_buffer_pages(x);

err:
	TRACE_ERROR;
	return -ENOMEM;
}

/************************************************************************
 * Client-side connection setup functions.
 ************************************************************************/

static void
xen_watch_connect(struct xenbus_watch *xbw, const char *path, const char *token)
{
		// struct watch_adapter *adap;
		// const char *token_caller;
		int path_len; //, tok_len, body_len;

    struct xensocket_xenbus_watch *x = (struct xensocket_xenbus_watch*)xbw;
    int rc = -EINVAL;

		// adap = container_of(xbw, struct watch_adapter, xbw);

		// token_caller = adap->token;

		path_len = strlen(path) + 1;
		// tok_len = strlen(token_caller) + 1;
		// body_len = path_len + tok_len;

    TRACE_ENTRY;
    if(path_len > 0) {
				DPRINTK("[%d] %s\n", path_len, path);
						
        xenbus_transaction_start(&(x->xbt));
        rc = xenbus_exists(x->xbt, path, "");
        xenbus_transaction_end(x->xbt, 0);
        if(!rc) {
            DPRINTK("%s was removed!\n", path);
            // unregister myself:
            unregister_xenbus_watch(xbw);
            // release sem:
            up(&(x->sem));
        }
    }
    TRACE_EXIT;
}

static int
xen_connect (struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags) {
	int    rc = -EINVAL;
	struct sock *sk = sock->sk;
	struct xen_sock *x = xen_sk(sk);
	struct sockaddr_xe *sxeaddr = (struct sockaddr_xe *)uaddr;
	char dir[256];
	struct xenbus_transaction t;
    char gref_str[15];
    int domid;
    int otherend_id;
    struct xensocket_xenbus_watch xsbw;

	TRACE_ENTRY;
    DPRINTK("sock@%p, service = %s\n", sock, sxeaddr->service);

	if (sxeaddr->sxe_family != AF_XEN) {
		goto err;
	}

	/* Ensure that connect() is only called once for this socket.
	 */

	if (x->is_client) {
		DPRINTK("error: cannot call connect() more than once on a socket\n");
		goto err;
	}
	if (x->is_server) {
		DPRINTK("error: cannot call both bind() and connect() on the same socket\n");
		goto err;
	}
	x->is_client = 1;

	xenbus_transaction_start(&(xsbw.xbt));
    // read remote domid from xenstore
    if((rc = xenbus_scanf(t, "/xensocket/service", sxeaddr->service, "%d", &otherend_id)) < 0) {
        goto err;
    }
    x->otherend_id = otherend_id;

	printk(KERN_CRIT "pfxen: allocating descriptor page...");
	if ((rc = server_allocate_descriptor_page(x)) != 0) {
		goto err;
	}
	printk(KERN_CRIT "pfxen: allocating event channel...");
	if ((rc = server_allocate_event_channel(x)) != 0) {
		goto err;
	}
	printk(KERN_CRIT "pfxen: allocating buffer pages...");
	if ((rc = server_allocate_buffer_pages(x)) != 0) {
		goto err;
	}

    sprintf(dir, "/xensocket/service/%s", sxeaddr->service);
    sprintf(gref_str, "%d", x->descriptor_gref);
    if(xenbus_exists(t, dir, gref_str)) {
        // already exists
        xenbus_transaction_end(t, 0);
        goto err;
    }
    // write own domid to xenstore
    xenbus_scanf(t, "domid", "", "%d", &domid);
    if((rc = xenbus_printf(t, dir, gref_str, "%d", domid)) < 0) {
        goto err;
    }
	xenbus_transaction_end(t, 0);

    // wait for accept:
    strcat(dir, "/");
    strcat(dir, gref_str);
    xsbw.xbw.node = dir;
    xsbw.xbw.callback = xen_watch_connect;
    sema_init(&(xsbw.sem), 0);
    register_xenbus_watch((struct xenbus_watch*)&xsbw);
    DPRINTK("registered watch on %s\n", xsbw.xbw.node);
    //down(&(xsbw.sem));
    if(down_interruptible(&(xsbw.sem))) {
        DPRINTK("connect got interrupted!\n");
        rc = -EINTR;
        unregister_xenbus_watch((struct xenbus_watch*)&xsbw);
    }
    DPRINTK("");

    sock->state = SS_CONNECTED;
	TRACE_EXIT;
	return 0;

err:
	TRACE_ERROR;
	return rc;
    
}

static int
client_map_descriptor_page (struct xen_sock *x) {
	struct gnttab_map_grant_ref op;
	int    rc = -ENOMEM;

	TRACE_ENTRY;

	if (x->descriptor_addr) {
		DPRINTK("error: already allocated client descriptor page\n");
		goto err;
	}

	if ((x->descriptor_area = alloc_vm_area(PAGE_SIZE, NULL)) == NULL) {
		DPRINTK("error: cannot allocate memory for descriptor page\n");
		goto err;
	}

	x->descriptor_addr = x->descriptor_area->addr;

	memset(&op, 0, sizeof(op));
	op.host_addr = (unsigned long)x->descriptor_addr;
	op.flags = GNTMAP_host_map;
	op.ref = x->descriptor_gref;
	op.dom = x->otherend_id;

	//lock_vm_area(x->descriptor_area);
	rc = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &op, 1);
	//unlock_vm_area(x->descriptor_area);
	if (rc == -ENOSYS) {
		goto err_unmap;
	}

	if (op.status < 0) {
		DPRINTK("error: grant table mapping operation failed\n");
		goto err_unmap;
	}

	x->descriptor_handle = op.handle;

	TRACE_EXIT;
	return 0;

err_unmap:
	client_unmap_descriptor_page(x);

err:
	TRACE_ERROR;
	return rc;
}

static int
client_bind_event_channel (struct xen_sock *x) {
	struct evtchn_bind_interdomain op;
	int         rc;

	TRACE_ENTRY;

	/* Start by binding this end of the event channel to the other
	 * end of the event channel. */

	op.remote_dom = x->otherend_id;
	op.remote_port = x->descriptor_addr->server_evtchn_port;

	printk("pfxen: remote dom: %d\n ", op.remote_dom);
	printk("pfxen: remote_port: %d\n", op.remote_port);

	if ((rc = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain, &op)) != 0) {
		DPRINTK("Unable to bind to server's event channel\n");
		goto err;
	}

	x->evtchn_local_port = op.local_port;

	DPRINTK("Other port is %d\n", x->descriptor_addr->server_evtchn_port);
	DPRINTK("My port is %d\n", x->evtchn_local_port);

	/* Next bind this end of the event channel to our local callback
	 * function. */
	if ((rc = bind_evtchn_to_irqhandler(x->evtchn_local_port, client_interrupt, 0, "xensocket", x)) <= 0) {
		DPRINTK("Unable to bind event channel to irqhandler\n");
		goto err;
	}

	x->irq = rc;

	TRACE_EXIT;
	return 0;

err:
	TRACE_ERROR;
	return rc;
}

static int
client_map_buffer_pages (struct xen_sock *x) {
	struct descriptor_page *d = x->descriptor_addr;
	int    buffer_num_pages;
	int    *grefp;
	int    i;
	struct gnttab_map_grant_ref op;
	int    rc = -ENOMEM;

	TRACE_ENTRY;

	if (!d) {
		/* must call client_map_descriptor_page first */
		DPRINTK("error: descriptor page not yet mapped\n");
		goto err;
	}

	if (x->buffer_area) {
		DPRINTK("error: already allocated client buffer pages\n");
		goto err;
	}

	if (d->buffer_order == -1) {
		DPRINTK("error: server has not yet allocated buffer pages\n");
		goto err;
	}

	x->buffer_order = d->buffer_order;
	buffer_num_pages = (1 << x->buffer_order);

	if (!(x->buffer_handles = kmalloc(buffer_num_pages * sizeof(grant_handle_t), GFP_KERNEL))) {
		DPRINTK("error: unexpected memory allocation failure\n");
		goto err;
	} 
	else {
		for (i = 0; i < buffer_num_pages; i++) {
			x->buffer_handles[i] = -1;
		}
	}

	if (!(x->buffer_area = alloc_vm_area(buffer_num_pages * PAGE_SIZE, NULL))) {
		DPRINTK("error: cannot allocate %d buffer pages\n", buffer_num_pages);
		goto err_unmap;
	}

	x->buffer_addr = (unsigned long)x->buffer_area->addr;

	grefp = &d->buffer_first_gref;
	for (i = 0; i < buffer_num_pages; i++) {
		memset(&op, 0, sizeof(op));
		op.host_addr = x->buffer_addr + i * PAGE_SIZE;
		op.flags = GNTMAP_host_map;
		op.ref = *grefp;
		op.dom = x->otherend_id;

		//lock_vm_area(x->buffer_area);
		rc = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &op, 1);
		//unlock_vm_area(x->buffer_area);
		if (rc == -ENOSYS) {
			goto err_unmap;
		}

		if (op.status) {
			DPRINTK("error: grant table mapping failed\n");
			goto err_unmap;
		}

		x->buffer_handles[i] = op.handle;
		grefp = (int *)(x->buffer_addr + i * PAGE_SIZE);
	}

	TRACE_EXIT;
	return 0;

err_unmap:
	client_unmap_buffer_pages(x);

err:
	TRACE_ERROR;
	return rc;
}

/************************************************************************
 * Data transmission functions (client-only in a one-way communication
 * channel).
 ************************************************************************/

static int
xen_sendmsg (struct socket *sock, struct msghdr *msg, size_t len) {
	int                     rc = -EINVAL;
	struct sock            *sk = sock->sk;
	struct xen_sock        *x = xen_sk(sk);
	struct descriptor_page *d = x->descriptor_addr;
	unsigned int            max_offset = (1 << x->buffer_order) * PAGE_SIZE;
	long                    timeo;
	unsigned int            copied = 0;
	unsigned int		not_copied = len;

	TRACE_ENTRY;
    DPRINTK("sock@%p\n", sock);

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);

	while(not_copied > 0) {
		unsigned int send_offset = d->send_offset;
		unsigned int avail_bytes = atomic_read(&d->avail_bytes);
		unsigned int bytes;

		if (atomic_read(&d->force_sender_shutdown) != 0) {
			rc = xen_release(sock);
			goto err;
		}

		/* Determine the maximum amount that can be written */
		bytes = not_copied;
		bytes = min(bytes, avail_bytes);

		/* Block if no space is available */
		if (bytes == 0) {
			timeo = send_data_wait(sk, timeo);
			if (signal_pending(current)) {
				rc = sock_intr_errno(timeo);
				goto err;
			}
			continue;
		}

		if ((send_offset + bytes) > max_offset) {
			/* wrap around, need to copy twice */
			unsigned int bytes_segment1 = max_offset - send_offset;
			unsigned int bytes_segment2 = bytes - bytes_segment1;

			if(copy_from_iter((unsigned char*)(x->buffer_addr + send_offset), bytes_segment1, &(msg->msg_iter)) != bytes_segment1) {
				DPRINTK("error: copy_from_user failed\n");
				goto err;
			}
			if(copy_from_iter((unsigned char*)(x->buffer_addr), bytes_segment2, &(msg->msg_iter)) != bytes_segment2) {
				DPRINTK("error: copy_from_user failed\n");
			}

			/*
			if (memcpy_fromiovecend((unsigned char *)(x->buffer_addr + send_offset), 
						msg->msg_iov, copied, bytes_segment1) == -EFAULT) {
				DPRINTK("error: copy_from_user failed\n");
				goto err;
			} 
			if (memcpy_fromiovecend((unsigned char *)(x->buffer_addr), 
						msg->msg_iov, copied + bytes_segment1, bytes_segment2) == -EFAULT) {
				DPRINTK("error: copy_from_user failed\n");
				goto err;
			}
			*/
		} 
		else {
            size_t res_bytes = copy_from_iter((unsigned char *)(x->buffer_addr + send_offset), bytes, &(msg->msg_iter));
            if(res_bytes != bytes) {
				DPRINTK("error: copy_from_user failed, res_bytes = %d\n", (int)res_bytes);
				goto err;
			}
			/* no need to wrap around
			if (memcpy_fromiovecend((unsigned char *)(x->buffer_addr + send_offset), 
						msg->msg_iov, copied, bytes) == -EFAULT) {
				DPRINTK("error: copy_from_user failed\n");
				goto err;
			}
			*/
		}

		/* Update values */
		copied += bytes;
		d->send_offset = (send_offset + bytes) % max_offset;
		d->total_bytes_sent += bytes;
		atomic_sub(bytes, &d->avail_bytes);
	}

	notify_remote_via_evtchn(x->evtchn_local_port);

	TRACE_EXIT;
	return copied;

err:
	TRACE_ERROR;
	return copied; 
}

static inline int
is_writeable (struct descriptor_page *d) {
	unsigned int avail_bytes = atomic_read(&d->avail_bytes);
	if (avail_bytes > 0) 
		return 1;

	return 0;
}

static long
send_data_wait (struct sock *sk, long timeo) {
	struct xen_sock *x = xen_sk(sk);
	struct descriptor_page *d = x->descriptor_addr;
	DEFINE_WAIT(wait);

	TRACE_ENTRY;

	d->sender_is_blocking = 1;
	notify_remote_via_evtchn(x->evtchn_local_port);

	for (;;) {
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);

		if (is_writeable(d)
				|| !skb_queue_empty(&sk->sk_receive_queue)
				|| sk->sk_err
				|| (sk->sk_shutdown & RCV_SHUTDOWN)
				|| signal_pending(current)
				|| !timeo
				|| atomic_read(&d->force_sender_shutdown)) {
			break;
		}

		timeo = schedule_timeout(timeo);
	}

	d->sender_is_blocking = 0;

	finish_wait(sk_sleep(sk), &wait);

	TRACE_EXIT;
	return timeo;
}

static irqreturn_t
client_interrupt (int irq, void *dev_id) {
	struct xen_sock *x = dev_id;
	struct sock     *sk = &x->sk;

	TRACE_ENTRY;

	if (sk_sleep(sk) && waitqueue_active(sk_sleep(sk))) {
		wake_up_interruptible(sk_sleep(sk));
	}

	TRACE_EXIT;
	return IRQ_HANDLED;
}

/************************************************************************
 * Data reception functions (server-only in a one-way communication
 * channel, but common to both in a two-way channel).
 ***********************************************************************/

static int
xen_recvmsg (struct socket *sock, struct msghdr *msg, size_t size, int flags) {
	int                     rc = -EINVAL;
	struct sock            *sk = sock->sk;
	struct xen_sock        *x = xen_sk(sk);
	struct descriptor_page *d = x->descriptor_addr;
	unsigned int            max_offset = (1 << x->buffer_order) * PAGE_SIZE;
	long                    timeo;
	int                     copied = 0;
	int                     target;

	TRACE_ENTRY;
    DPRINTK("sock@%p\n", sock);

	target = sock_rcvlowat(sk, flags&MSG_WAITALL, size);
	timeo = sock_rcvtimeo(sk, flags&MSG_DONTWAIT);
	while (copied < size) {
		unsigned int recv_offset = d->recv_offset;
		unsigned int bytes;
		unsigned int avail_bytes = max_offset - atomic_read(&d->avail_bytes);  /* bytes available for read */

		/* Determine the maximum amount that can be read */
		bytes = min((unsigned int)(size - copied), avail_bytes);

		if (atomic_read(&d->sender_has_shutdown) != 0) {
			if (avail_bytes == 0) {
				copied = 0;
				break;
			}
		}

		/* Block if the buffer is empty */
		if (bytes == 0) {
			if (copied > target) {
				break;
			}

			timeo = receive_data_wait(sk, timeo);
			if (signal_pending(current)) {
				rc = sock_intr_errno(timeo);
				DPRINTK("error: signal\n");
				goto err;
			}
			continue;
		}

		/* Perform the read */
		if ((recv_offset + bytes) > max_offset) {
			/* wrap around, need to perform the read twice */
			unsigned int bytes_segment1 = max_offset - recv_offset;
			unsigned int bytes_segment2 = bytes - bytes_segment1;
			if (local_memcpy_toiovecend(msg->msg_iter.iov, (unsigned char *)(x->buffer_addr + recv_offset), 
						copied, bytes_segment1) == -EFAULT) {
				DPRINTK("error: copy_to_user failed\n");
				goto err;
			}
			if (local_memcpy_toiovecend(msg->msg_iter.iov, (unsigned char *)(x->buffer_addr), 
						copied + bytes_segment1, bytes_segment2) == -EFAULT) {
				DPRINTK("error: copy_to_user failed\n");
				goto err;
			}
		} 
		else {
			/* no wrap around, proceed with one copy */
			if (local_memcpy_toiovecend(msg->msg_iter.iov, (unsigned char *)(x->buffer_addr + recv_offset), 
						copied, bytes) == -EFAULT) {
				DPRINTK("error: copy_to_user failed\n");
				goto err;
			}
		}

		/* Update values */
		copied += bytes;
		d->recv_offset = (recv_offset + bytes) % max_offset;
		d->total_bytes_received += bytes;
		atomic_add(bytes, &d->avail_bytes);
		if (d->sender_is_blocking) {
			notify_remote_via_evtchn(x->evtchn_local_port);
		}
	}

	TRACE_EXIT;
	return copied;

err:
	TRACE_ERROR;
	return copied;
}

static inline int
is_readable (struct descriptor_page *d) {
	unsigned int max_offset = (1 << d->buffer_order) * PAGE_SIZE;
	unsigned int avail_bytes = max_offset - atomic_read(&d->avail_bytes);
	if (avail_bytes > 0)
		return 1;

	return 0;
}

static long
receive_data_wait (struct sock *sk, long timeo) {
	struct xen_sock        *x = xen_sk(sk);
	struct descriptor_page *d = x->descriptor_addr;
	DEFINE_WAIT(wait);

	TRACE_ENTRY;

	for (;;) {
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		if (is_readable(d)
				|| (atomic_read(&d->sender_has_shutdown) != 0)
				|| !skb_queue_empty(&sk->sk_receive_queue)
				|| sk->sk_err
				|| (sk->sk_shutdown & RCV_SHUTDOWN)
				|| signal_pending(current)
				|| !timeo) {
			break;
		}

		timeo = schedule_timeout(timeo);
	}

	finish_wait(sk_sleep(sk), &wait);

	TRACE_EXIT;
	return timeo;
}

static irqreturn_t
server_interrupt (int irq, void *dev_id) {
	struct xen_sock *x = dev_id;
	struct sock     *sk = &x->sk;

	TRACE_ENTRY;

	if (sk_sleep(sk) && waitqueue_active(sk_sleep(sk))) {
		wake_up_interruptible(sk_sleep(sk));
	}

	TRACE_EXIT;
	return IRQ_HANDLED;
}

static int
local_memcpy_toiovecend (const struct iovec *iov, unsigned char *kdata, int offset, int len) {
	int err = -EFAULT; 

	/* Skip over the finished iovecs */
	while (offset >= iov->iov_len) {
		offset -= iov->iov_len;
		iov++;
	}

	while (len > 0) {
		u8 *base = iov->iov_base + offset;
		int copy = min((unsigned int)len, (unsigned int)(iov->iov_len - offset));

		offset = 0;
		if (copy_to_user(base, kdata, copy)) {
			goto out;
		}
		kdata += copy;
		len -= copy;
		iov++;
	}
	err = 0;

out:
	return err;
}

/************************************************************************
 * Connection teardown functions (common to both server and client).
 ************************************************************************/

static int
xen_release (struct socket *sock) {
	struct sock            *sk = sock->sk;
	struct xen_sock        *x;
	struct descriptor_page *d;

	TRACE_ENTRY;
    DPRINTK("sock@%p\n", sock);
	if (!sk) {
		return 0;
	}

	sock->sk = NULL;
	x = xen_sk(sk);
	d = x->descriptor_addr;

	// if map didn't succeed, gracefully exit 
	if (x->descriptor_handle == -1) 
		goto out;

	if (x->is_server) {
		while (atomic_read(&d->sender_has_shutdown) == 0 ) {
		}

		server_unallocate_buffer_pages(x);
		server_unallocate_descriptor_page(x);
	}

	if (x->is_client) {
		if ((atomic_read(&d->sender_has_shutdown)) == 0) {
			client_unmap_buffer_pages(x);
			client_unmap_descriptor_page(x);
			notify_remote_via_evtchn(x->evtchn_local_port);
		}
		else {
			printk(KERN_CRIT "    xen_release: SENDER ALREADY SHUT DOWN!\n");
		}
	}

out:
	sock_put(sk);

	TRACE_EXIT;
	return 0;
}

static void
server_unallocate_buffer_pages (struct xen_sock *x) {
	if (x->buffer_grefs) {
		int buffer_num_pages = (1 << x->buffer_order);
		int i;

		for (i = 0; i < buffer_num_pages; i++) {
			if (x->buffer_grefs[i] == -ENOSPC) {
				break;
			}

			gnttab_end_foreign_access(x->buffer_grefs[i], 0, 0);
			x->buffer_grefs[i] = -ENOSPC;
		}

		kfree(x->buffer_grefs);
		x->buffer_grefs = NULL;
	}

	if (x->buffer_addr) {
		struct descriptor_page *d = x->descriptor_addr;

		free_pages(x->buffer_addr, x->buffer_order);
		x->buffer_addr = 0;
		x->buffer_order = -1;
		if (d) {
			d->buffer_order = -1;
		}
	}
}

static void
server_unallocate_descriptor_page (struct xen_sock *x) {
	if (x->descriptor_gref != -ENOSPC) {
		gnttab_end_foreign_access(x->descriptor_gref, 0, 0);
		x->descriptor_gref = -ENOSPC;
	}
	if (x->descriptor_addr) {
		free_page((unsigned long)(x->descriptor_addr));
		x->descriptor_addr = NULL;
	}
}

static void
client_unmap_buffer_pages (struct xen_sock *x) {

	if (x->buffer_handles) {
		struct descriptor_page *d = x->descriptor_addr;
		int                     buffer_order = d->buffer_order;
		int                     buffer_num_pages = (1 << buffer_order);
		int                     i;
		struct                  gnttab_unmap_grant_ref op;
		int                     rc = 0;

		for (i = 0; i < buffer_num_pages; i++) {
			if (x->buffer_handles[i] == -1) {
				break;
			}

			memset(&op, 0, sizeof(op));
			op.host_addr = x->buffer_addr + i * PAGE_SIZE;
			op.handle = x->buffer_handles[i];
			op.dev_bus_addr = 0;

			//lock_vm_area(x->buffer_area);
			rc = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &op, 1);
			//unlock_vm_area(x->buffer_area);
			if (rc == -ENOSYS) {
				printk("Failure to unmap grant reference \n");
			}
		}

		kfree(x->buffer_handles);
		x->buffer_handles = NULL;
	}
	if (x->buffer_area) {
		free_vm_area(x->buffer_area);
		x->buffer_area = NULL;
	}
}

static void
client_unmap_descriptor_page (struct xen_sock *x) {
	struct descriptor_page *d;
	int                     rc = 0;

	d = x->descriptor_addr;

	if (x->descriptor_handle != -1) {
		struct gnttab_unmap_grant_ref op;

		memset(&op, 0, sizeof(op));
		op.host_addr = (unsigned long)x->descriptor_addr;
		op.handle = x->descriptor_handle;
		op.dev_bus_addr = 0;

		//lock_vm_area(x->descriptor_area);
		atomic_set(&d->sender_has_shutdown, 1);
		rc = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &op, 1);
		//unlock_vm_area(x->descriptor_area);
		if (rc == -ENOSYS) {
			printk("Failure to unmap grant reference for descriptor page\n");
		}

		x->descriptor_handle = -1;
	}
	if (x->descriptor_area) {
		free_vm_area(x->descriptor_area);
		x->descriptor_area = NULL;
	}
}

static void xen_watch_accept(struct xenbus_watch *xbw, const char *path, const char *token) {
		int rc = -EINVAL;
    const char *gref_str;
    int gref, domid;
    struct xensocket_xenbus_watch *x = (struct xensocket_xenbus_watch*) xbw;

		int path_len = strlen(path) + 1;

    TRACE_ENTRY;
    DPRINTK("xen_watch_service(%p, %p, %d)\n", xbw, path, path_len);

    if(path_len > 0) {
        /* check if
         * 1) node is a prefix of the path and
         * 2) if "path" is longer than "node/"
         */
        x->path_len = strlen(xbw->node);
        DPRINTK("path = %s\n", path);
        if(strncmp(path, xbw->node, x->path_len) == 0 && (strlen(path) - x->path_len > 1)) {
            x->gref = -2; // dbg
            gref_str = path + x->path_len + 1;
            if(sscanf(gref_str, "%d", &gref) > 0) {
                xenbus_transaction_start(&(x->xbt));
                rc = xenbus_scanf(x->xbt, xbw->node, gref_str, "%d", &domid);
                if(rc > 0) {
                    // success
                    x->gref = gref;
                    x->domid = domid;
                    // unregister myself:
                    unregister_xenbus_watch(xbw);
                    xenbus_rm(x->xbt, xbw->node, gref_str);
                    xenbus_transaction_end(x->xbt, 0);
                    // release sem:
                    up(&(x->sem));
                } else {
                    xenbus_transaction_end(x->xbt, 0);
                }
            }
        }
    }
    TRACE_EXIT;
}

static int
xen_accept(struct socket *sock, struct socket *newsock, int flags, bool kern)
{
	int    rc = -EINVAL;
	struct sock *sk = sock->sk;
	struct xen_sock *x = xen_sk(sk);
    struct sock *new_sk;
    struct xen_sock *new_x;
	char   dir[256];
    struct xensocket_xenbus_watch xsbw;
	//struct xenbus_transaction t;

	TRACE_ENTRY;
    DPRINTK("sock@%p\n", sock);
    DPRINTK("newsock@%p\n", newsock);

    sprintf(dir, "/xensocket/service/%s", x->service);
    xsbw.xbw.node = dir;
    xsbw.xbw.callback = xen_watch_accept;
    xsbw.gref = -1;

    // init as locked:
    sema_init(&(xsbw.sem), 0);

    // start watch:
    register_xenbus_watch((struct xenbus_watch*)&xsbw);

    // wait for sem release:
    DPRINTK("");
    //down(&(xsbw.sem));
    if(down_interruptible(&(xsbw.sem))) {
        // e.g. interrupted
        rc = -EINTR;
        DPRINTK("accept got interrupted\n");
        unregister_xenbus_watch((struct xenbus_watch*)&xsbw);
        goto err;
    }
    DPRINTK("");
    //unregister_xenbus_watch((struct xenbus_watch*)&xsbw);

    DPRINTK("xsbw.gref = %d, xsbw.domid = %d\n", xsbw.gref, xsbw.domid);

    // create child sk for newsock:
    xen_create(sock_net(sk), newsock, -1, 0);
    
    new_sk = newsock->sk;
    new_x = xen_sk(new_sk);

    strcpy(new_x->service, x->service);

    new_x->descriptor_gref = xsbw.gref;
    new_x->otherend_id = xsbw.domid;

	if (new_x->descriptor_gref < 0) {
		printk(KERN_CRIT "Gref could not be read!");
		goto err;
	}

	printk(KERN_CRIT "pfxen: mapping descriptor page...");
	if ((rc = client_map_descriptor_page(new_x)) != 0) {
		goto err;
	}
	printk(KERN_CRIT "pfxen: mapping event channel...");
	if ((rc = client_bind_event_channel(new_x)) != 0) {
		goto err_unmap_descriptor;
	}
	printk(KERN_CRIT "pfxen: mapping buffer pages...");
	if ((rc = client_map_buffer_pages(new_x)) != 0) {
		goto err_unmap_buffer;
	}

    newsock->state = SS_CONNECTED;

	TRACE_EXIT;
	return 0;

err_unmap_buffer:
	client_unmap_buffer_pages(new_x);

err_unmap_descriptor:
	client_unmap_descriptor_page(new_x);
	notify_remote_via_evtchn(new_x->evtchn_local_port);

err:
    TRACE_ERROR;
	return rc;
}

// accept requires this implementation
static int xen_getname(struct socket *sock, struct sockaddr *addr, int *sockaddr_len, int peer) {
	struct sock *sk = sock->sk;
	struct xen_sock *x = xen_sk(sk);
    struct sockaddr_xe *sxeaddr = (struct sockaddr_xe*) addr;

    TRACE_ENTRY;
    DPRINTK("peer = %d\n", peer);
    sxeaddr->sxe_family = AF_XEN;
    strcpy(sxeaddr->service, x->service);
    *sockaddr_len = sizeof(struct sockaddr_xe);
    TRACE_EXIT;
    return 0;
}

static int xen_listen (struct socket *sock, int backlog) {
    int domid;
    struct xenbus_transaction t;
	struct sock *sk = sock->sk;
	struct xen_sock *x = xen_sk(sk);

	TRACE_ENTRY;
    DPRINTK("sock@%p\n", sock);
    // xenbus transaction
    xenbus_transaction_start(&t);
    // get own domid:
    xenbus_scanf(t, "domid", "", "%d", &domid);
    xenbus_printf(t, "/xensocket/service", x->service, "%d", domid);
    xenbus_transaction_end(t, 0);

	TRACE_EXIT;
    return 0;
}

/************************************************************************
 * Functions to interface this module with the rest of the Linux streams
 * code.
 ************************************************************************/
 /*
static struct xenbus_watch xbwg = {
    .node = "/xensocket/service",
    .callback = xen_watch_service
};
*/

static int __init
xensocket_init (void) {
	int rc = -1;
	struct xenbus_transaction t;

	TRACE_ENTRY;

	rc = proto_register(&xen_proto, 1);
	printk(KERN_CRIT  "pfxen: protocol registered\n");
	if (rc != 0) {
		printk(KERN_CRIT "%s: Cannot create xen_sock SLAB cache!\n", __FUNCTION__);
		goto out;
	}

	printk(KERN_CRIT "pfxen: registering socket family...\n");
	sock_register(&xen_family_ops);
	printk(KERN_CRIT "pfxen: xen socket family registered\n");
	xenbus_transaction_start(&t);
    xenbus_scanf(t, "domid", "", "%d", &mydomid);
	xenbus_transaction_end(t, 0);
    printk(KERN_CRIT "pfxen: my domid = %d\n", mydomid);

    // this is just for testing xenbus watch!
    //register_xenbus_watch(&xbwg);

out:
	TRACE_EXIT;
	return rc;
}

static void __exit
xensocket_exit (void) {
	TRACE_ENTRY;

	sock_unregister(AF_XEN);
	proto_unregister(&xen_proto);

    // this is just for testing xenbus watch!
    //unregister_xenbus_watch(&xbwg);

	TRACE_EXIT;
}

module_init(xensocket_init);
module_exit(xensocket_exit);

MODULE_LICENSE("GPL");

