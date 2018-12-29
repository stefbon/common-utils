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

#include "logging.h"
#include "main.h"
#include "pathinfo.h"
#include "beventloop.h"
#include "beventloop-xdata.h"

#include "utils.h"
#include "localsocket.h"
#include "udt-interface.h"
#include "network-utils.h"

/*
    DEFAULT unix socket ops
*/

static int default_accept(struct fs_connection_s *conn, struct sockaddr *addr, unsigned int *len)
{
    return accept(conn->fd, addr, len);
}
static int default_bind(struct fs_connection_s *conn, struct sockaddr *addr, int *len, int sock)
{
    return bind(conn->fd, addr, *len);
}
static int default_close(struct fs_connection_s *conn)
{
    return close(conn->fd);
}
static int default_connect(struct fs_connection_s *conn, struct sockaddr *addr, int *len)
{
    return connect(conn->fd, addr, *len);
}
static int default_getpeername(struct fs_connection_s *conn, struct sockaddr *addr, unsigned int *len)
{
    return getpeername(conn->fd, addr, len);
}
static int default_getsockname(struct fs_connection_s *conn, struct sockaddr *addr, unsigned int *len)
{
    return getsockname(conn->fd, addr, len);
}
static int default_getsockopt(struct fs_connection_s *conn, int level, int n, char *v, unsigned int *l)
{
    return getsockopt(conn->fd, level, n, v, l);
}
static int default_listen(struct fs_connection_s *conn, int len)
{
    return listen(conn->fd, len);
}
static int default_recv(struct fs_connection_s *conn, char *buffer, unsigned int size, unsigned int flags)
{
    return recv(conn->fd, buffer, size, flags);
}
static int default_send(struct fs_connection_s *conn, char *buffer, unsigned int size, unsigned int flags)
{
    return send(conn->fd, buffer, size, flags);
}
static int default_setsockopt(struct fs_connection_s *conn, int level, int n, char *v, unsigned int l)
{
    return setsockopt(conn->fd, level, n, v, l);
}
static int default_socket(int af, int type, int protocol)
{
    return socket(af, type, protocol);
}
static int default_start()
{
    return 0; /* default does not require initialization */
}
static int default_finish()
{
    return 0; /* default does not require cleanup etc */
}

static struct socket_ops_s default_sops = {
    .type				=	SOCKET_OPS_TYPE_DEFAULT,
    .accept				=	default_accept,
    .bind				=	default_bind,
    .close				=	default_close,
    .connect				=	default_connect,
    .getpeername			=	default_getpeername,
    .getsockname			=	default_getsockname,
    .getsockopt				=	default_getsockopt,
    .listen				=	default_listen,
    .recv				=	default_recv,
    .send				=	default_send,
    .setsockopt				=	default_setsockopt,
    .socket				=	default_socket,
    .start				=	default_start,
    .finish				=	default_finish,
};

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

struct fs_connection_s *accept_cb_dummy(uid_t uid, gid_t gid, pid_t pid, struct fs_connection_s *s_conn)
{
    return NULL;
}

static void event_cb_dummy(struct fs_connection_s *conn, void *data, uint32_t events)
{
}
void disconnect_cb_dummy(struct fs_connection_s *conn, unsigned char remote)
{
    struct fs_connection_s *s_conn=conn->ops.client.server;

    pthread_mutex_lock(&s_conn->ops.server.mutex);
    remove_list_element(&conn->list);
    pthread_mutex_unlock(&s_conn->ops.server.mutex);

    free(conn);

}
void init_cb_dummy(struct fs_connection_s *conn)
{
}

void init_connection(struct fs_connection_s *c, struct socket_ops_s *sops, unsigned char type, unsigned int family)
{

    if (sops==NULL) {

	if (type==FS_CONNECTION_TYPE_UDP) {

	    /* use special libray for UDP: UDT*/

	    setsocketopsudt(c);

	} else {

	    sops=&default_sops;

	}

    }

    memset(c, 0, sizeof(struct fs_connection_s));

    c->type=type;
    c->family=family;
    c->status=FS_CONNECTION_FLAG_INIT;
    c->error=0;
    c->expire=0;
    c->fd=0;
    c->xdata=NULL;
    c->data=NULL;
    init_list_element(&c->list, NULL);
    c->sops=sops;

    if (type==FS_CONNECTION_TYPE_LOCALSERVER) {

	c->ops.server.accept=accept_cb_dummy;
	init_list_header(&c->ops.server.header, SIMPLE_LIST_TYPE_EMPTY, NULL);
	pthread_mutex_init(&c->ops.server.mutex, NULL);

    } else if (type==FS_CONNECTION_TYPE_LOCALCLIENT) {

	c->ops.client.uid=(uid_t) -1;
	c->ops.client.event=event_cb_dummy;
	c->ops.client.disconnect=disconnect_cb_dummy;
	c->ops.client.init=init_cb_dummy;
	c->ops.client.server=NULL;

    }

}

static int accept_local_connection(int sfd, void *data, uint32_t events)
{
    struct fs_connection_s *s_conn=(struct fs_connection_s *) data;
    struct fs_connection_s *c_conn=NULL;
    struct sockaddr_un local;
    struct ucred cred;
    socklen_t s_len=0;
    int fd=0;

    s_len=sizeof(struct sockaddr_un);

    fd=(* s_conn->sops->accept)(s_conn, (struct sockaddr *) &local, &s_len);

    if (fd==-1) {

	logoutput("accept_local_connection: error %i accept (%s)", errno, strerror(errno));
	goto disconnect;

    }

    /* get credentials */

    s_len=sizeof(cred);

    if ((* s_conn->sops->getsockopt)(s_conn, SOL_SOCKET, SO_PEERCRED, (char *)&cred, &s_len)==-1) {

	logoutput("accept_local_connection: error %i geting socket credentials (%s)", errno, strerror(errno));
	goto disconnect;

    }

    c_conn=(* s_conn->ops.server.accept)(cred.uid, cred.gid, cred.pid, s_conn);

    if (! c_conn) {

	logoutput("accept_local_connection: connection denied for user %i:%i pid %i", (unsigned int) cred.uid, cred.gid, cred.pid);
	goto disconnect;

    }

    memmove(&c_conn->socket.local, &local, sizeof(struct sockaddr_un));

    c_conn->fd=(unsigned int) fd;
    c_conn->ops.client.server=s_conn;
    c_conn->sops=s_conn->sops; /* use the same socket ops */

    pthread_mutex_lock(&s_conn->ops.server.mutex);
    add_list_element_first(&s_conn->ops.server.header, &c_conn->list);
    pthread_mutex_unlock(&s_conn->ops.server.mutex);

    (* c_conn->ops.client.init)(c_conn);

    return 0;

    disconnect:

    if (fd>0) {

	close(fd);
	fd=0;

    }

    (* c_conn->ops.client.disconnect)(c_conn, 0);

    return -1;

}

int connect_remotesocket(struct fs_connection_s *conn, const struct sockaddr *addr, int *len)
{
    return (* conn->sops->connect)(conn, (struct sockaddr *) addr, len);
}

int create_local_serversocket(char *path, struct fs_connection_s *conn, struct beventloop_s *loop, struct fs_connection_s *(* accept_cb)(uid_t uid, gid_t gid, pid_t pid, struct fs_connection_s *s_conn), unsigned int *error)
{
    int result=-1;
    int fd=0;
    int len=0;

    if (!conn) {

	*error=EINVAL;
	goto out;

    } else if (conn->type != FS_CONNECTION_TYPE_LOCALSERVER) {

	*error=EINVAL;
	goto out;

    }

    if (! loop) loop=get_mainloop();
    if (! accept_cb) accept_cb=accept_cb_dummy;

    /* add socket */

    fd=(* conn->sops->socket)(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if (fd==-1) {

        *error=errno;
        goto out;

    }

    conn->fd=(unsigned int) fd;

    /* bind path/familiy and socket */

    memset(&conn->socket.local, 0, sizeof(struct sockaddr_un));
    conn->socket.local.sun_family=AF_UNIX;
    snprintf(conn->socket.local.sun_path, sizeof(conn->socket.local.sun_path), path);
    len=sizeof(struct sockaddr_un);

    if ((* conn->sops->bind)(conn, (struct sockaddr *) &conn->socket.local, &len, 0)==-1) {

        *error=errno;
	close(conn->fd);
	conn->fd=0;
        goto out;

    }

    /* listen */

    if ((* conn->sops->listen)(conn, LISTEN_BACKLOG)==-1 ) {

        *error=errno;
	close(conn->fd);
	conn->fd=0;

    } else {
	struct bevent_xdata_s *xdata=NULL;

	xdata=add_to_beventloop(conn->fd, EPOLLIN, accept_local_connection, (void *) conn, NULL, loop);

	if (xdata) {

    	    logoutput("create_server_socket: socket fd %i added to eventloop", conn->fd);
	    result=conn->fd;
	    conn->ops.server.accept=accept_cb;
	    conn->xdata=xdata;

	} else {

    	    logoutput("create_server_socket: error adding socket fd %i to eventloop.", conn->fd);

	    *error=EIO;
	    close(conn->fd);
	    conn->fd=0;

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
    struct list_element_s *list=(c_conn) ? c_conn->list.n : s_conn->ops.server.header.head;
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

int compare_network_connection(struct fs_connection_s *conn, char *address, unsigned int port)
{
    struct addrinfo h;
    struct addrinfo *list, *w;
    int result=-1;

    if (conn==NULL || address==NULL) return -1;

    memset(&h, 0, sizeof(struct addrinfo));

    if (conn->family==FS_CONNECTION_FAMILY_IPv4) {

	h.ai_family=AF_INET;
	h.ai_socktype = SOCK_STREAM;

    } else if (conn->family==FS_CONNECTION_FAMILY_IPv6) {

	h.ai_family=AF_INET6;
	h.ai_socktype = SOCK_STREAM;

    } else {

	h.ai_family=AF_UNSPEC;
	h.ai_socktype = 0;

    }

    h.ai_protocol=0;
    h.ai_flags=AI_CANONNAME;
    h.ai_canonname=NULL;
    h.ai_addrlen=0;
    h.ai_addr=NULL;
    h.ai_next=NULL;

    result=getaddrinfo(address, NULL, &h, &list);

    if (result>0) {

	logoutput("compare_connection: error %s", gai_strerror(result));
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