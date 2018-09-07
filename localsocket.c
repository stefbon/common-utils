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
#include <dirent.h>
#include <pthread.h>

#include "logging.h"
#include "main.h"
#include "pathinfo.h"
#include "beventloop.h"
#include "beventloop-xdata.h"

#include "utils.h"
#include "localsocket.h"

static int default_accept(struct fs_connection_s *conn, struct sockaddr *addr, int *len)
{
    return accept(conn->fd, addr, len);
}
static int default_bind(struct fs_connection_s *conn, struct sockaddr *addr, int *len, int sock)
{
    return bind(conn->fd, addr, len);
}
static int default_close(struct fs_connection_s *conn)
{
    return close(conn->fd);
}
static int default_connect(struct fs_connection_s *conn, struct sockaddr *addr, int *len)
{
    return connect(conn->fd, addr, len);
}
static int default_getpeername(struct fs_connection_s *conn, struct sockaddr *addr, int *len)
{
    return getpeername(conn->fd, addr, len);
}
static int default_gethostname(struct fs_connection_s *conn, struct sockaddr *addr, int *len)
{
    return gethostname(conn->fd, addr, len);
}
static int default_getsockopt(struct fs_connection_s *conn, int level, char *n, char *v, int *l)
{
    return gethostname(conn->fd, level, n, v, l);
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
static int default_setsockopt(struct fs_connection_s *conn, int level, char *n, char v, int l)
{
    return gethostname(conn->fd, level, n, v, l);
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

static socket_ops_s default_sops = {
    .type				=	SOCKET_OPS_TYPE_UNIX,
    .accept				=	default_accept,
    .bind				=	default_bind,
    .close				=	default_close,
    .connect				=	default_connect,
    .getpeername			=	default_getpeername,
    .gethostname			=	default_gethostname,
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
    remove_list_element(&s_conn->ops.server.head, &s_conn->ops.server.tail, &conn->list);
    pthread_mutex_unlock(&s_conn->ops.server.mutex);

    free(conn);

}
void init_cb_dummy(struct fs_connection_s *conn)
{
}

void init_connection(struct fs_connection_s *conn, struct socket_opts_s *sops, unsigned char type)
{

    if (sops==NULL) sops=&default_sops;

    memset(conn, 0, sizeof(struct fs_connection_s));
    conn->type=type;
    conn->fd=0;
    init_xdata(&conn->xdata);
    conn->data=NULL;
    conn->list.next=NULL;
    conn->list.prev=NULL;
    conn->sops=sops;

    if (type==FS_CONNECTION_TYPE_LOCALSERVER) {

	conn->ops.server.accept=accept_cb_dummy;
	conn->ops.server.head=NULL;
	conn->ops.server.tail=NULL;
	pthread_mutex_init(&conn->ops.server.mutex, NULL);

    } else if (type==FS_CONNECTION_TYPE_LOCALCLIENT) {

	conn->ops.client.uid=(uid_t) -1;
	conn->ops.client.event=event_cb_dummy;
	conn->ops.client.disconnect=disconnect_cb_dummy;
	conn->ops.client.init=init_cb_dummy;
	conn->ops.client.server=NULL;

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

    fd=(* s_conn->accept)(sfd, (struct sockaddr *) &local, &s_len);
    // fd=accept4(sfd, (struct sockaddr *) &local, &s_len, 0);

    if (fd==-1) {

	logoutput("accept_local_connection: error %i accept (%s)", errno, strerror(errno));
	goto disconnect;

    }

    /* get credentials */

    s_len=sizeof(cred);

    if ((* s_conn->getsockopt)(s_conn, SOL_SOCKET, SO_PEERCRED, &cred, &s_len)==-1) {

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
    add_list_element_first(&s_conn->ops.server.head, &s_conn->ops.server.tail, &c_conn->list);
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

int connect_remotesocket(struct fs_connection_s *conn, const struct sockaddr *addr, int len)
{
    return (* conn->connect)(conn, addr, len);
}

int create_local_serversocket(char *path, struct fs_connection_s *conn, struct beventloop_s *loop, struct fs_connection_s *(* accept_cb)(uid_t uid, gid_t gid, pid_t pid, struct fs_connection_s *s_conn), unsigned int *error)
{
    int result=-1;
    int fd=0;

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

    fd=(* conn->socket)(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if (fd==-1) {

        *error=errno;
        goto out;

    }

    conn->fd=(unsigned int) fd;

    /* bind path/familiy and socket */

    memset(&conn->socket.local, 0, sizeof(struct sockaddr_un));
    conn->socket.local.sun_family=AF_UNIX;
    snprintf(conn->socket.local.sun_path, sizeof(conn->socket.local.sun_path), path);

    if ((* conn->bind)(conn->fd, (struct sockaddr *) &conn->socket.local, sizeof(struct sockaddr_un))==-1) {

        *error=errno;
	close(conn->fd);
	conn->fd=0;
        goto out;

    }

    /* listen */

    if ((* conn->listen)(conn->fd, LISTEN_BACKLOG)==-1 ) {

        *error=errno;
	close(conn->fd);
	conn->fd=0;

    } else {
	struct bevent_xdata_s *xdata=NULL;

	xdata=add_to_beventloop(conn->fd, EPOLLIN, accept_local_connection, (void *) conn, &conn->xdata, loop);

	if ( ! xdata) {

    	    logoutput("create_server_socket: error adding socket fd %i to eventloop.", conn->fd);

	    *error=EIO;
	    close(conn->fd);
	    conn->fd=0;

	} else {

    	    logoutput("create_server_socket: socket fd %i added to eventloop", conn->fd);
	    result=conn->fd;
	    conn->ops.server.accept=accept_cb;

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
    struct list_element_s *list=(c_conn) ? c_conn->list.next : s_conn->ops.server.head;
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
