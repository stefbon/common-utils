/*
  2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017 Stef Bon <stefbon@gmail.com>

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

#ifndef FS_LOCALSOCKET_H
#define FS_LOCALSOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <linux/netlink.h>

#define FS_CONNECTION_TYPE_LOCALSERVER					1
#define FS_CONNECTION_TYPE_LOCALCLIENT					2
#define FS_CONNECTION_TYPE_NETLINK					3

#define LISTEN_BACKLOG 50

struct fs_connection_s;

typedef void (* event_cb)(struct fs_connection_s *conn, void *data, uint32_t events);
typedef struct fs_connection_s *(* accept_cb)(uid_t uid, gid_t gid, pid_t pid, struct fs_connection_s *conn);
typedef void (* disconnect_cb)(struct fs_connection_s *conn, unsigned char remote);
typedef void (* init_cb)(struct fs_connection_s *conn);

struct fs_connection_s {
    unsigned char 			type;
    unsigned int 			fd;
    struct bevent_xdata_s		xdata;
    void 				*data;
    struct list_element_s		list;
    union {
	struct sockaddr_un 		local;
	struct sockaddr_nl 		netlink;
    } socket;
    union {
	struct server_ops_s {
	    accept_cb			accept;
	    struct list_element_s	*head;
	    struct list_element_s	*tail;
	    pthread_mutex_t		mutex;
	} server;
	struct client_ops_s {
	    uid_t			uid;
	    pid_t			pid;
	    disconnect_cb		disconnect;
	    event_cb 			event;
	    init_cb			init;
	    struct fs_connection_s	*server;
	} client;
    } ops;
};

/* Prototypes */

int create_socket_path(struct pathinfo_s *pathinfo);
int check_socket_path(struct pathinfo_s *pathinfo, unsigned int already);

void init_connection(struct fs_connection_s *connection, unsigned char type);
int create_local_serversocket(char *path, struct fs_connection_s *conn, struct beventloop_s *loop, struct fs_connection_s *(* accept_cb)(uid_t uid, gid_t gid, pid_t pid, void *ptr), unsigned int *error);

struct fs_connection_s *get_containing_connection(struct list_element_s	*list);
struct fs_connection_s *get_next_connection(struct fs_connection_s *s_conn, struct fs_connection_s *c_conn);

int lock_connection_list(struct fs_connection_s *s_conn);
int unlock_connection_list(struct fs_connection_s *s_conn);

#endif
