/*
 
  2010, 2011, 2012, 2013, 2014, 2015 Stef Bon <stefbon@gmail.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "global-defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <inttypes.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netdb.h>

#include <pthread.h>
#include <fcntl.h>
#include <sys/uio.h>

#include "logging.h"
#include "main.h"
#include "pathinfo.h"
#include "beventloop.h"
#include "beventloop-xdata.h"

#include "utils.h"
#include "localsocket.h"
#include "network-utils.h"

/*
    ZERO unix socket ops */

static int socket_zero_accept(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    return -1;
}
static int socket_zero_bind(unsigned int fd, struct sockaddr *addr, int *len, int sock)
{
    return -1;
}
static int socket_zero_close(unsigned int fd)
{
    return -1;
}
static int socket_zero_connect(unsigned int fd, struct sockaddr *addr, int *len)
{
    return -1;
}
static int socket_zero_getpeername(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    return -1;
}
static int socket_zero_getsockname(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    return -1;
}
static int socket_zero_getsockopt(unsigned int fd, int level, int n, char *v, unsigned int *l)
{
    return -1;
}
static int socket_zero_setsockopt(unsigned int fd, int level, int n, char *v, unsigned int l)
{
    return -1;
}
static int socket_zero_listen(unsigned int fd, int blog)
{
    return -1;
}
static int socket_zero_socket(int af, int type, int protocol)
{
    return -1;
}
static int socket_zero_send(struct io_socket_s *s, char *buffer, unsigned int size, unsigned int flags)
{
    return -1;
}
static int socket_zero_recv(struct io_socket_s *s, char *buffer, unsigned int size, unsigned int flags)
{
    return -1;
}
static int socket_zero_start()
{
    return 0;
}
static int socket_zero_finish()
{
    return 0;
}
static struct socket_ops_s zero_sops = {
    .type				=	SOCKET_OPS_TYPE_ZERO,
    .accept				=	socket_zero_accept,
    .bind				=	socket_zero_bind,
    .close				=	socket_zero_close,
    .connect				=	socket_zero_connect,
    .getpeername			=	socket_zero_getpeername,
    .getsockname			=	socket_zero_getsockname,
    .getsockopt				=	socket_zero_getsockopt,
    .listen				=	socket_zero_listen,
    .setsockopt				=	socket_zero_setsockopt,
    .socket				=	socket_zero_socket,
    .send				=	socket_zero_send,
    .recv				=	socket_zero_recv,
    .start				=	socket_zero_start,
    .finish				=	socket_zero_finish,
};
void set_io_socket_ops_zero(struct io_socket_s *s)
{
    s->sops=&zero_sops;
}

/*
    DEFAULT unix socket ops	*/

static int socket_default_accept(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    return accept(fd, addr, len);
}
static int socket_default_bind(unsigned int fd, struct sockaddr *addr, int *len, int sock)
{
    return bind(fd, addr, *len);
}
static int socket_default_close(unsigned int fd)
{
    return close(fd);
}
static int socket_default_connect(unsigned int fd, struct sockaddr *addr, int *len)
{
    return connect(fd, addr, *len);
}
static int socket_default_getpeername(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    return getpeername(fd, addr, len);
}
static int socket_default_getsockname(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    return getsockname(fd, addr, len);
}
static int socket_default_getsockopt(unsigned int fd, int level, int n, char *v, unsigned int *l)
{
    return getsockopt(fd, level, n, v, l);
}
static int socket_default_listen(unsigned int fd, int len)
{
    return listen(fd, len);
}
static int socket_default_setsockopt(unsigned int fd, int level, int n, char *v, unsigned int l)
{
    return setsockopt(fd, level, n, v, l);
}
static int socket_default_socket(int af, int type, int protocol)
{
    return socket(af, type, protocol);
}
static int socket_default_recv(struct io_socket_s *s, char *buffer, unsigned int size, unsigned int flags)
{
    return recv(s->xdata.fd, buffer, size, flags);
}
static int socket_default_send(struct io_socket_s *s, char *buffer, unsigned int size, unsigned int flags)
{
    return send(s->xdata.fd, buffer, size, flags);
}
static int socket_default_start()
{
    return 0; /* default does not require initialization */
}
static int socket_default_finish()
{
    return 0; /* default does not require cleanup etc */
}
static struct socket_ops_s default_sops = {
    .type				=	SOCKET_OPS_TYPE_DEFAULT,
    .accept				=	socket_default_accept,
    .bind				=	socket_default_bind,
    .close				=	socket_default_close,
    .connect				=	socket_default_connect,
    .getpeername			=	socket_default_getpeername,
    .getsockname			=	socket_default_getsockname,
    .getsockopt				=	socket_default_getsockopt,
    .listen				=	socket_default_listen,
    .setsockopt				=	socket_default_setsockopt,
    .socket				=	socket_default_socket,
    .send				=	socket_default_send,
    .recv				=	socket_default_recv,
    .start				=	socket_default_start,
    .finish				=	socket_default_finish,
};

void set_io_socket_ops_default(struct io_socket_s *s)
{
    s->sops=&default_sops;
}

/* ZERO fuse ops */

static int zero_fuse_open(char *path, unsigned int flags)
{
    return -1;
}
static int zero_fuse_close(unsigned int fd)
{
    return -1;
}
static ssize_t zero_fuse_writev(struct io_fuse_s *s, struct iovec *iov, int count)
{
    return -1;
}
static int zero_fuse_read(struct io_fuse_s *s, void *buffer, size_t size)
{
    return -1;
}
static struct fuse_ops_s zero_fops = {
    .type				=	FUSE_OPS_TYPE_ZERO,
    .open				=	zero_fuse_open,
    .close				=	zero_fuse_close,
    .writev				=	zero_fuse_writev,
    .read				=	zero_fuse_read,
};

void set_io_fuse_ops_zero(struct io_fuse_s *s)
{
    s->fops=&zero_fops;
}

/* DEFAULT fuse ops */

static int default_fuse_open(char *path, unsigned int flags)
{
    return open(path, flags);
}
static int default_fuse_close(unsigned int fd)
{
    return close(fd);
}
static ssize_t default_fuse_writev(struct io_fuse_s *s, struct iovec *iov, int count)
{
    return writev(s->xdata.fd, iov, count);
}
static int default_fuse_read(struct io_fuse_s *s, void *buffer, size_t size)
{
    return read(s->xdata.fd, buffer, size);
}
static struct fuse_ops_s default_fops = {
    .type				=	FUSE_OPS_TYPE_DEFAULT,
    .open				=	default_fuse_open,
    .close				=	default_fuse_close,
    .writev				=	default_fuse_writev,
    .read				=	default_fuse_read,
};

void set_io_fuse_ops_default(struct io_fuse_s *s)
{
    s->fops=&default_fops;
}

int create_socket_path(struct pathinfo_s *pathinfo)
{
    char path[pathinfo->len + 1];
    char *slash=NULL;

    memcpy(path, pathinfo->path, pathinfo->len + 1);
    unslash(path);

    /* create the parent path */

    slash=strchr(path, '/');

    while (slash) {

	*slash='\0';

	if (strlen(path)==0) goto next;

	if (mkdir(path, S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)==-1) {

	    if (errno != EEXIST) {

		logoutput("create_socket_path: error %i%s creating %s", errno, strerror(errno), path);
		return -1;

	    }

	}

	next:

	*slash='/';
	slash=strchr(slash+1, '/');

    }

    return 0;

}

int check_socket_path(struct pathinfo_s *pathinfo, unsigned int alreadyrunning)
{
    struct stat st;

    if (stat(pathinfo->path, &st)==0) {

	/* path to socket does exists */

	if (S_ISSOCK(st.st_mode)) {

	    if (alreadyrunning==0) {

		logoutput("check_socket_path: socket %s does already exist but no other process found, remove it", pathinfo->path);

		unlink(pathinfo->path);
		return 0;

	    } else {

		logoutput("check_socket_path: socket %s does already exist (running with pid %i), cannot continue", pathinfo->path, alreadyrunning);

	    }

	} else {

	    logoutput("check_socket_path: %s does already already exist (but is not a socket?!), cannot continue", pathinfo->path);

	}

	return -1;

    }

    return 0;

}

struct fs_connection_s *accept_local_cb_dummy(uid_t uid, gid_t gid, pid_t pid, struct fs_connection_s *s_conn)
{
    return NULL;
}

static struct fs_connection_s *accept_network_cb_dummy(struct host_address_s *host, struct fs_connection_s *c)
{
    return NULL;
}

static void event_cb_dummy(struct fs_connection_s *conn, void *data, uint32_t events)
{
}
void disconnect_cb_dummy(struct fs_connection_s *conn, unsigned char remote)
{
    if (conn->role==FS_CONNECTION_ROLE_CLIENT) {
	struct fs_connection_s *s_conn=conn->ops.client.server;

	pthread_mutex_lock(&s_conn->ops.server.mutex);
	remove_list_element(&conn->list);
	pthread_mutex_unlock(&s_conn->ops.server.mutex);

    }

    free(conn);

}
void init_cb_dummy(struct fs_connection_s *conn, unsigned int fd)
{
}

void init_connection(struct fs_connection_s *c, unsigned char type, unsigned char role)
{

    if (type != FS_CONNECTION_TYPE_LOCAL && type != FS_CONNECTION_TYPE_TCP4 && type != FS_CONNECTION_TYPE_TCP6 && type != FS_CONNECTION_TYPE_UDP4 && type != FS_CONNECTION_TYPE_UDP6 && type != FS_CONNECTION_TYPE_FUSE) {

	logoutput("init_connection: type %i not supported", type);
	return;

    }

    if (role != FS_CONNECTION_ROLE_SERVER && role != FS_CONNECTION_ROLE_CLIENT) {

	logoutput("init_connection: role %i not supported", role);
	return;

    }


    memset(c, 0, sizeof(struct fs_connection_s));

    c->type=type;
    c->role=role;
    c->status=FS_CONNECTION_FLAG_INIT;
    c->error=0;
    c->expire=0;
    c->data=NULL;
    init_list_element(&c->list, NULL);

    if (type == FS_CONNECTION_TYPE_FUSE) {

	init_xdata(&c->io.fuse.xdata);
	set_io_fuse_ops_default(&c->io.fuse);

    } else {

	init_xdata(&c->io.socket.xdata);
	set_io_socket_ops_default(&c->io.socket);

    }

    if (role==FS_CONNECTION_ROLE_SERVER) {

	if (type==FS_CONNECTION_TYPE_LOCAL) {

	    c->ops.server.accept.local=accept_local_cb_dummy;

	} else if (type==FS_CONNECTION_TYPE_TCP4 || type==FS_CONNECTION_TYPE_TCP6 || type==FS_CONNECTION_TYPE_UDP4 || type==FS_CONNECTION_TYPE_UDP6) {

	    c->ops.server.accept.network=accept_network_cb_dummy;

	}

	init_list_header(&c->ops.server.header, SIMPLE_LIST_TYPE_EMPTY, NULL);
	pthread_mutex_init(&c->ops.server.mutex, NULL);

    } else if (role==FS_CONNECTION_ROLE_CLIENT) {

	if (type==FS_CONNECTION_TYPE_LOCAL) {

	    c->ops.client.id.local.uid=(uid_t) -1;
	    c->ops.client.id.local.pid=0; /* not possible value for client in userspace */

	} else if (type==FS_CONNECTION_TYPE_FUSE) {

	    c->ops.client.id.fuse.uid=getuid();;
	    c->ops.client.id.fuse.gid=getgid();

	} else if (type==FS_CONNECTION_TYPE_TCP4 || type==FS_CONNECTION_TYPE_TCP6 || type==FS_CONNECTION_TYPE_UDP4 || type==FS_CONNECTION_TYPE_UDP6) {

	    init_host_address(&c->ops.client.id.host);

	}

	c->ops.client.event=event_cb_dummy;
	c->ops.client.disconnect=disconnect_cb_dummy;
	c->ops.client.init=init_cb_dummy;
	c->ops.client.server=NULL;

    }

}

static int accept_local_connection(int sfd, void *data, uint32_t events)
{
    struct fs_connection_s *s_conn=(struct fs_connection_s *) data;
    struct socket_ops_s *sops=s_conn->io.socket.sops;
    struct fs_connection_s *c_conn=NULL;
    struct sockaddr_un local;
    struct ucred cred;
    socklen_t s_len=0;
    int fd=-1;

    s_len=sizeof(struct sockaddr_un);
    fd=(* sops->accept)(s_conn->io.socket.xdata.fd, (struct sockaddr *) &local, &s_len);

    if (fd==-1) {

	logoutput("accept_local_connection: error %i accept (%s)", errno, strerror(errno));
	goto disconnect;

    }

    /* get credentials */

    s_len=sizeof(cred);
    if ((* sops->getsockopt)(fd, SOL_SOCKET, SO_PEERCRED, (char *)&cred, &s_len)==-1) {

	logoutput("accept_local_connection: error %i geting socket credentials (%s)", errno, strerror(errno));
	goto disconnect;

    }

    c_conn=(* s_conn->ops.server.accept.local)(cred.uid, cred.gid, cred.pid, s_conn);
    if (! c_conn) {

	logoutput("accept_local_connection: connection denied for user %i:%i pid %i", (unsigned int) cred.uid, cred.gid, cred.pid);
	goto disconnect;

    }

    memmove(&c_conn->io.socket.sockaddr.local, &local, sizeof(struct sockaddr_un));
    c_conn->ops.client.server=s_conn;
    c_conn->io.socket.sops=sops; /* use the same socket ops */

    pthread_mutex_lock(&s_conn->ops.server.mutex);
    add_list_element_first(&s_conn->ops.server.header, &c_conn->list);
    pthread_mutex_unlock(&s_conn->ops.server.mutex);

    (* c_conn->ops.client.init)(c_conn, fd);
    return 0;

    disconnect:

    if (fd>0) close(fd);
    if (c_conn) free(c_conn);
    return -1;

}

static int accept_network_connection(int sfd, void *data, uint32_t events)
{
    struct fs_connection_s *s_conn=(struct fs_connection_s *) data;
    struct socket_ops_s *sops=s_conn->io.socket.sops;
    struct fs_connection_s *c_conn=NULL;
    struct sockaddr saddr, *ptr=NULL;
    struct host_address_s host;
    socklen_t slen=0;
    int fd=-1;
    int ipv6=0;
    char *hostname=NULL;
    unsigned int error=0;

    logoutput("accept_network_connection: socket fd = %i", sfd);

    ipv6=get_connection_info(s_conn, "ipv6");

    slen=(ipv6==0) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
    fd=(* sops->accept)(s_conn->io.socket.xdata.fd, &saddr, &slen);
    if (fd==-1) goto disconnect;

    init_host_address(&host);

    hostname=get_connection_hostname(s_conn, (unsigned int) fd, 1, &error);

    if (hostname) {

	strncpy(host.hostname, hostname, sizeof(host.hostname));
	free(hostname);
	host.flags|=HOST_ADDRESS_FLAG_HOSTNAME;

    }

    if (ipv6==0) {
	char *ip=get_connection_ipv6(s_conn, (unsigned int) fd, 1, &error);

	if (ip) {

	    strncpy(host.ip.ip.v6, ip, sizeof(host.ip.ip.v6));
	    free(ip);
	    host.flags|=HOST_ADDRESS_FLAG_IP;

	}

    } else {
	char *ip=get_connection_ipv4(s_conn, (unsigned int) fd, 1, &error);

	if (ip) {

	    strncpy(host.ip.ip.v4, ip, sizeof(host.ip.ip.v4));
	    free(ip);
	    host.flags|=HOST_ADDRESS_FLAG_IP;

	}

    }

    if ((host.flags & (HOST_ADDRESS_FLAG_IP | HOST_ADDRESS_FLAG_HOSTNAME))==0) goto disconnect; /* not enough info */

    c_conn=(* s_conn->ops.server.accept.network)(&host, s_conn);
    if (! c_conn) goto disconnect;

    ptr= ((ipv6==0) ? (struct sockaddr *)&c_conn->io.socket.sockaddr.inet6 : (struct sockaddr *)&c_conn->io.socket.sockaddr.inet);
    memmove(ptr, &saddr, slen);

    c_conn->ops.client.server=s_conn;
    c_conn->io.socket.sops=sops; /* use the same server socket ops */

    pthread_mutex_lock(&s_conn->ops.server.mutex);
    add_list_element_first(&s_conn->ops.server.header, &c_conn->list);
    pthread_mutex_unlock(&s_conn->ops.server.mutex);

    (* c_conn->ops.client.init)(c_conn, fd);
    return 0;

    disconnect:

    if (fd>0) close(fd);
    if (c_conn) free(c_conn);
    return -1;

}

int connect_socket(struct fs_connection_s *conn, const struct sockaddr *addr, int *len)
{
    return (* conn->io.socket.sops->connect)(conn->io.socket.xdata.fd, (struct sockaddr *) addr, len);
}

int close_socket(struct fs_connection_s *conn)
{
    return (* conn->io.socket.sops->close)(conn->io.socket.xdata.fd);
}

int create_local_serversocket(char *path, struct fs_connection_s *conn, struct beventloop_s *loop, struct fs_connection_s *(* accept_cb)(uid_t uid, gid_t gid, pid_t pid, struct fs_connection_s *s_conn), unsigned int *error)
{
    struct socket_ops_s *sops=conn->io.socket.sops;
    int result=-1;
    int fd=0;
    int len=0;

    if (!conn) {

	*error=EINVAL;
	goto out;

    } else if (get_connection_info(conn, "local")==-1 || get_connection_info(conn, "server")==-1) {

	logoutput("create_local_serversocket: not a local server");

	*error=EINVAL;
	goto out;

    }

    if (! loop) loop=get_mainloop();
    if (! accept_cb) accept_cb=accept_local_cb_dummy;

    /* add socket */

    fd=(* sops->socket)(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if (fd==-1) {

        *error=errno;
        goto out;

    }

    /* bind path/familiy and socket */

    memset(&conn->io.socket.sockaddr.local, 0, sizeof(struct sockaddr_un));
    conn->io.socket.sockaddr.local.sun_family=AF_UNIX;
    snprintf(conn->io.socket.sockaddr.local.sun_path, sizeof(conn->io.socket.sockaddr.local.sun_path), path);
    len=sizeof(struct sockaddr_un);

    if ((* sops->bind)(fd, (struct sockaddr *) &conn->io.socket.sockaddr.local, &len, 0)==-1) {

        *error=errno;
	(* sops->close)(fd);
        goto out;

    }

    /* listen */

    if ((* sops->listen)(fd, LISTEN_BACKLOG)==-1 ) {

        *error=errno;
	(* sops->close)(fd);

    } else {

	if (add_to_beventloop(fd, EPOLLIN, accept_local_connection, (void *) conn, &conn->io.socket.xdata, loop)) {

    	    logoutput("create_server_socket: socket fd %i added to eventloop", fd);
	    result=fd;
	    conn->ops.server.accept.local=accept_cb;

	} else {

    	    logoutput("create_server_socket: error adding socket fd %i to eventloop.", fd);
	    *error=EIO;
	    (* sops->close)(fd);

	}

    }

    out:

    return result;

}

int create_network_serversocket(struct fs_connection_s *conn, struct beventloop_s *loop, struct fs_connection_s *(* accept_cb)(struct host_address_s *h, struct fs_connection_s *s), unsigned int *error)
{
    struct socket_ops_s *sops=conn->io.socket.sops;
    int result=-1;
    int fd=-1;
    int len=0;
    unsigned int domain=AF_INET;
    unsigned int type=SOCK_STREAM | SOCK_NONBLOCK;
    struct sockaddr *saddr=NULL;
    int ipv6=0;

    if (!conn) {

	*error=EINVAL;
	goto out;

    } else if (get_connection_info(conn, "network")==-1 || get_connection_info(conn, "server")==-1) {

	*error=EINVAL;
	goto out;

    }

    if (! loop) loop=get_mainloop();
    if (! accept_cb) accept_cb=accept_network_cb_dummy;

    /* add socket */

    ipv6=get_connection_info(conn, "ipv6");
    domain=(ipv6==0) ? AF_INET6 : AF_INET;

    if (get_connection_info(conn, "udp")==0) {

	fd=(* sops->socket)(domain, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);

    } else if (get_connection_info(conn, "tcp")==0) {

	fd=(* sops->socket)(domain, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_UDP);

    }

    if (fd==-1) {

        *error=errno;
        goto out;

    }

    /* bind path/familiy and socket */

    if (ipv6==0) {

	memset(&conn->io.socket.sockaddr.inet6, 0, sizeof(struct sockaddr_in6));
	conn->io.socket.sockaddr.inet6.sin6_family=AF_INET6;
	len=sizeof(struct sockaddr_in6);
	saddr=(struct sockaddr *) &conn->io.socket.sockaddr.inet6;

    } else {

	memset(&conn->io.socket.sockaddr.inet, 0, sizeof(struct sockaddr_in));
	conn->io.socket.sockaddr.inet.sin_family=AF_INET;
	len=sizeof(struct sockaddr_in);
	saddr=(struct sockaddr *) &conn->io.socket.sockaddr.inet;

    }

    if ((* sops->bind)(fd, saddr, &len, 0)==-1) {

        *error=errno;
	(* sops->close)(fd);
        goto out;

    }

    /* listen */

    if ((* sops->listen)(fd, LISTEN_BACKLOG)==-1 ) {

        *error=errno;
	(* sops->close)(fd);

    } else {

	if (add_to_beventloop(fd, EPOLLIN, accept_network_connection, (void *) conn, &conn->io.socket.xdata, loop)) {

    	    logoutput("create_network_serversocket: socket fd %i added to eventloop", fd);
	    result=fd;
	    conn->ops.server.accept.network=accept_cb;

	} else {

    	    logoutput("create_network_serversocket: error adding socket fd %i to eventloop.", fd);
	    *error=EIO;
	    (* sops->close)(fd);

	}

    }

    out:

    return result;

}

struct fs_connection_s *get_containing_connection(struct list_element_s *list)
{
    return (struct fs_connection_s *) ( ((char *) list) - offsetof(struct fs_connection_s, list));
}
struct fs_connection_s *get_next_connection(struct fs_connection_s *s_conn, struct fs_connection_s *c_conn)
{
    struct list_element_s *list=(c_conn) ? get_next_element(&c_conn->list) : get_list_head(&s_conn->ops.server.header, 0);
    return (list) ? get_containing_connection(list) : NULL;
}
int lock_connection_list(struct fs_connection_s *s_conn)
{
    return pthread_mutex_lock(&s_conn->ops.server.mutex);
}
int unlock_connection_list(struct fs_connection_s *s_conn)
{
    return pthread_mutex_unlock(&s_conn->ops.server.mutex);
}

int compare_network_address(struct fs_connection_s *conn, char *address, unsigned int port)
{
    struct addrinfo h;
    struct addrinfo *list, *w;
    int result=-1;
    struct sockaddr *saddr=NULL;

    if (conn==NULL || address==NULL) return -1;

    memset(&h, 0, sizeof(struct addrinfo));

    h.ai_protocol=0;
    h.ai_flags=AI_CANONNAME;
    h.ai_family=AF_UNSPEC;
    h.ai_socktype = 0;
    h.ai_canonname=NULL;
    h.ai_addrlen=0;
    h.ai_addr=(struct sockaddr *) &conn->io.socket;
    h.ai_next=NULL;

    if (get_connection_info(conn, "ipv4")==0) {

	h.ai_family=AF_INET;
	h.ai_addr = (struct sockaddr *) &conn->io.socket.sockaddr.inet;
	h.ai_addrlen = sizeof(struct sockaddr_in);

    } else if (get_connection_info(conn, "ipv6")==0) {

	h.ai_family=AF_INET6;
	h.ai_addr = (struct sockaddr *) &conn->io.socket.sockaddr.inet6;
    	h.ai_addrlen = sizeof(struct sockaddr_in6);

    }

    if (get_connection_info(conn, "tcp")==0) {

	h.ai_socktype = SOCK_STREAM;

    } else if (get_connection_info(conn, "udp")==0) {

	h.ai_socktype = SOCK_DGRAM;

    }

    result=getaddrinfo(address, NULL, &h, &list);

    if (result>0) {

	logoutput("compare_network_address: error %s", gai_strerror(result));
	return -1;

    }

    result=-1;
    for (w=list; w != NULL; w = w->ai_next) {
	char host[NI_MAXHOST+1];

	/* try first hostname */

	memset(host, '\0', NI_MAXHOST+1);

	if (getnameinfo((struct sockaddr *) w->ai_addr, w->ai_addrlen, host, NI_MAXHOST, NULL, 0, 0)==0) {

	    if (strcmp(address, host)==0) {

		result=0;
		goto out;

	    }

	}

	if (check_family_ip_address(address, "ipv4")==1 && w->ai_addr->sa_family==AF_INET) {
	    struct sockaddr_in *s=(struct sockaddr_in *) w->ai_addr;
	    char *tmp=inet_ntoa(s->sin_addr);

	    if (strcmp(tmp, address)==0) {

		free(tmp);
		result=0;
		goto out;

	    }

	    free(tmp);

	}

	/* what to do with IPv6 ?*/

    }

    out:

    free(list);
    return result;

}

static int _compare_network_conn_ipv4(struct fs_connection_s *a, struct fs_connection_s *b)
{
    char hosta[NI_MAXHOST+1];
    char hostb[NI_MAXHOST+1];
    struct sockaddr *sa, *sb;
    int result=-1;

    memset(hosta, '\0', NI_MAXHOST+1);
    memset(hostb, '\0', NI_MAXHOST+1);

    sa=(struct sockaddr *) &a->io.socket.sockaddr.inet;
    sb=(struct sockaddr *) &b->io.socket.sockaddr.inet;

    if (getnameinfo(sa, sizeof(struct sockaddr_in), hosta, NI_MAXHOST, NULL, 0, 0)==0 &&
	getnameinfo(sb, sizeof(struct sockaddr_in), hostb, NI_MAXHOST, NULL, 0, 0)==0) {

	if (strcmp(hosta, hostb)==0) result=0;

    }

    return result;

}
static int _compare_network_conn_ipv6(struct fs_connection_s *a, struct fs_connection_s *b)
{
    char hosta[NI_MAXHOST+1];
    char hostb[NI_MAXHOST+1];
    struct sockaddr *sa, *sb;
    int result=-1;

    memset(hosta, '\0', NI_MAXHOST+1);
    memset(hostb, '\0', NI_MAXHOST+1);

    sa=(struct sockaddr *) &a->io.socket.sockaddr.inet6;
    sb=(struct sockaddr *) &b->io.socket.sockaddr.inet6;

    if (getnameinfo(sa, sizeof(struct sockaddr_in6), hosta, NI_MAXHOST, NULL, 0, 0)==0 &&
	getnameinfo(sb, sizeof(struct sockaddr_in6), hostb, NI_MAXHOST, NULL, 0, 0)==0) {

	if (strcmp(hosta, hostb)==0) result=0;

    }

    return result;

}
static int _compare_network_conn_ipv4ipv6(struct fs_connection_s *a, struct fs_connection_s *b)
{
    char hosta[NI_MAXHOST+1];
    char hostb[NI_MAXHOST+1];
    struct sockaddr *sa, *sb;
    int result=-1;

    memset(hosta, '\0', NI_MAXHOST+1);
    memset(hostb, '\0', NI_MAXHOST+1);

    sa=(struct sockaddr *) &a->io.socket.sockaddr.inet;
    sb=(struct sockaddr *) &b->io.socket.sockaddr.inet6;

    if (getnameinfo(sa, sizeof(struct sockaddr_in), hosta, NI_MAXHOST, NULL, 0, 0)==0 &&
	getnameinfo(sb, sizeof(struct sockaddr_in6), hostb, NI_MAXHOST, NULL, 0, 0)==0) {

	if (strcmp(hosta, hostb)==0) result=0;

    }

    return result;

}
int compare_network_connection(struct fs_connection_s *a, struct fs_connection_s *b, unsigned int flags)
{

    if (flags & FS_CONNECTION_COMPARE_HOST) {

	if (get_connection_info(a, "ipv4")==0 && get_connection_info(b, "ipv4")==0) {

	    return _compare_network_conn_ipv4(a, b);

	} else if (get_connection_info(a, "ipv6")==0 && get_connection_info(b, "ipv6")==0) {

	    return _compare_network_conn_ipv6(a, b);

	} else if (get_connection_info(a, "ipv4")==0 && get_connection_info(b, "ipv6")==0) {

	    return _compare_network_conn_ipv4ipv6(a, b);

	} else if (get_connection_info(a, "ipv6")==0 && get_connection_info(b, "ipv4")==0) {

	    return _compare_network_conn_ipv4ipv6(b, a);

	}

    }

    return -1;

}

int get_connection_info(struct fs_connection_s *a, const char *what)
{

    if (strcmp(what, "ipv4")==0) {

	return (a->type==FS_CONNECTION_TYPE_TCP4 || a->type==FS_CONNECTION_TYPE_UDP4) ? 0 : -1;

    } else if (strcmp(what, "ipv6")==0) {

	return (a->type==FS_CONNECTION_TYPE_TCP6 || a->type==FS_CONNECTION_TYPE_UDP6) ? 0 : -1;

    } else if (strcmp(what, "network")==0) {

	return (a->type==FS_CONNECTION_TYPE_TCP4 || a->type==FS_CONNECTION_TYPE_UDP4 || a->type==FS_CONNECTION_TYPE_TCP6 || a->type==FS_CONNECTION_TYPE_UDP6) ? 0 : -1;

    } else if (strcmp(what, "tcp")==0) {

	return (a->type==FS_CONNECTION_TYPE_TCP4 || a->type==FS_CONNECTION_TYPE_TCP6) ? 0 : -1;

    } else if (strcmp(what, "udp")==0) {

	return (a->type==FS_CONNECTION_TYPE_UDP4 || a->type==FS_CONNECTION_TYPE_UDP6) ? 0 : -1;

    } else if (strcmp(what, "local")==0) {

	return (a->type==FS_CONNECTION_TYPE_LOCAL ) ? 0 : -1;

    } else if (strcmp(what, "client")==0) {

	return (a->role==FS_CONNECTION_ROLE_CLIENT ) ? 0 : -1;

    } else if (strcmp(what, "server")==0) {

	return (a->role==FS_CONNECTION_ROLE_SERVER ) ? 0 : -1;

    }

    return -1;

}

/* get ipv4 as char * of connection
    - what = 0 -> local
    - what = 1 -> remote */

char *get_connection_ipv4(struct fs_connection_s *conn, unsigned int fd, unsigned char what, unsigned int *error)
{
    struct socket_ops_s *sops=conn->io.socket.sops;
    struct sockaddr_in addr;
    socklen_t len=sizeof(struct sockaddr_in);
    char *ipv4=NULL;
    char buffer[INET_ADDRSTRLEN + 1];

    if (fd==0) {

	*error=EINVAL;
	return NULL;

    }

    memset(buffer, '\0', INET_ADDRSTRLEN + 1);

    if (what==0) {

	if ((* sops->getsockname)(fd, (struct sockaddr *) &addr, &len)==-1) {

	    *error=errno;
	    return NULL;

	}

    } else {

	if ((* sops->getpeername)(fd, (struct sockaddr *) &addr, &len)==-1) {

	    *error=errno;
	    return NULL;

	}

    }

    if (inet_ntop(AF_INET, &addr.sin_addr, buffer, sizeof(buffer))) {

	ipv4=strdup(buffer);
	if (ipv4==NULL) *error=ENOMEM;

    }

    return ipv4;

}

/* get ipv6 as char * of connection
    - what = 0 -> local
    - what = 1 -> remote */

char *get_connection_ipv6(struct fs_connection_s *conn, unsigned int fd, unsigned char what, unsigned int *error)
{
    struct socket_ops_s *sops=conn->io.socket.sops;
    struct sockaddr_in6 addr;
    socklen_t len=sizeof(struct sockaddr_in6);
    char *ipv6=NULL;
    char buffer[INET6_ADDRSTRLEN + 1];

    if (fd==0) {

	*error=EINVAL;
	return NULL;

    }

    memset(buffer, '\0', INET6_ADDRSTRLEN + 1);

    if (what==0) {

	if ((* sops->getsockname)(fd, (struct sockaddr *) &addr, &len)==-1) {

	    *error=errno;
	    return NULL;

	}

    } else {

	if ((* sops->getpeername)(fd, (struct sockaddr *) &addr, &len)==-1) {

	    *error=errno;
	    return NULL;

	}

    }

    if (inet_ntop(AF_INET6, &addr.sin6_addr, buffer, sizeof(buffer))) {

	ipv6=strdup(buffer);
	if (ipv6==NULL) *error=ENOMEM;

    }

    return ipv6;

}

/* get hostname of connection
    - what = 0 -> local
    - what = 1 -> remote
*/

char *get_connection_hostname(struct fs_connection_s *conn, unsigned int fd, unsigned char what, unsigned int *error)
{
    struct socket_ops_s *sops=conn->io.socket.sops;
    struct sockaddr addr;
    socklen_t len = sizeof(struct sockaddr);
    int result = 0;
    char tmp[NI_MAXHOST];
    unsigned int count = 0;

    if (fd==0) {

	*error=EINVAL;
	return NULL;

    }

    if (what==0) {

	logoutput("get_connection_hostname: fd=%i local name", fd);

	if ((* sops->getsockname)(fd, &addr, &len)==-1) {

	    *error=errno;
	    return NULL;

	}

    } else {

	logoutput("get_connection_hostname: fd=%i remote name", fd);

	if ((* sops->getpeername)(fd, &addr, &len)==-1) {

	    *error=errno;
	    return NULL;

	}

    }

    gethostname:

    memset(tmp, '\0', NI_MAXHOST);
    result=getnameinfo(&addr, len, tmp, NI_MAXHOST, NULL, 0, NI_NAMEREQD);
    count++;

    if (result==0) {
	char *hostname=NULL;

	hostname=strdup(tmp);
	if (hostname==NULL) *error=ENOMEM;
	return hostname;

    } else {

	logoutput("get_connection_hostname: error %i:%s", result, gai_strerror(result));

	if (result==EAI_MEMORY) {

	    *error=ENOMEM;

	} else if (result==EAI_NONAME) {

	    *error=ENOENT;

	} else if (result==EAI_SYSTEM) {

	    *error=errno;

	} else if (result==EAI_OVERFLOW) {

	    *error=ENAMETOOLONG;

	} else if (result==EAI_AGAIN) {

	    if (count<10) goto gethostname;
	    *error=EIO;

	} else {

	    *error=EIO;

	}

    }

    return NULL;

}
