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

#ifndef SB_COMMON_UTILS_LOCALSOCKET_H
#define SB_COMMON_UTILS_LOCALSOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <linux/netlink.h>

#include "beventloop.h"
#include "simple-list.h"
#include "pathinfo.h"

#define FS_CONNECTION_TYPE_LOCALSERVER					1
#define FS_CONNECTION_TYPE_LOCALCLIENT					2
#define FS_CONNECTION_TYPE_NETLINK					3
#define FS_CONNECTION_TYPE_TCP						4
#define FS_CONNECTION_TYPE_UDP						5

#define FS_CONNECTION_FAMILY_IPv4					1
#define FS_CONNECTION_FAMILY_IPv6					2

#define LISTEN_BACKLOG 50

#define SOCKET_OPS_TYPE_DEFAULT						1
#define SOCKET_OPS_TYPE_UDT						2

#define FS_CONNECTION_FLAG_INIT						1
#define FS_CONNECTION_FLAG_CONNECTING					2
#define FS_CONNECTION_FLAG_CONNECTED					4
#define FS_CONNECTION_FLAG_EVENTLOOP					8
#define FS_CONNECTION_FLAG_DISCONNECTING				16
#define FS_CONNECTION_FLAG_DISCONNECTED					32

struct fs_connection_s;

typedef void (* event_cb)(struct fs_connection_s *conn, void *data, uint32_t events);
typedef struct fs_connection_s *(* accept_cb)(uid_t uid, gid_t gid, pid_t pid, struct fs_connection_s *conn);
typedef void (* disconnect_cb)(struct fs_connection_s *conn, unsigned char remote);
typedef void (* init_cb)(struct fs_connection_s *conn);

struct socket_ops_s {
    unsigned char			type;
    int					(* accept)(struct fs_connection_s *conn, struct sockaddr *addr, unsigned int *len);
    int					(* bind)(struct fs_connection_s *conn, struct sockaddr *addr, int *len, int sock);
    int					(* close)(struct fs_connection_s *conn);
    int					(* connect)(struct fs_connection_s *conn, struct sockaddr *addr, int *len);
    int					(* getpeername)(struct fs_connection_s *conn, struct sockaddr *addr, unsigned int *len);
    int					(* getsockname)(struct fs_connection_s *conn, struct sockaddr *addr, unsigned int *len);
    int					(* getsockopt)(struct fs_connection_s *conn, int level, int optname, char *optval, unsigned int *optlen);
    int					(* setsockopt)(struct fs_connection_s *conn, int level, int optname, char *optval, unsigned int optlen);
    int					(* listen)(struct fs_connection_s *conn, int len);
    int					(* recv)(struct fs_connection_s *conn, char *buffer, unsigned int size, unsigned int flags);
    int					(* send)(struct fs_connection_s *conn, char *buffer, unsigned int size, unsigned int flags);
    int					(* socket)(int af, int type, int protocol);
    int					(* start)();
    int					(* finish)();
};

struct fs_connection_s {
    unsigned char 			type;
    unsigned char			family;
    unsigned char			status;
    unsigned int			error;
    unsigned int 			fd;
    unsigned int			expire;
    struct bevent_xdata_s		*xdata;
    void 				*data;
    struct list_element_s		list;
    struct socket_ops_s			*sops;
    union {
	struct sockaddr_un 		local;
	struct sockaddr_nl 		netlink;
	struct sockaddr_in		inet;
	struct sockaddr_in6		inet6;
    } socket;
    union {
	struct server_ops_s {
	    accept_cb			accept;
	    struct list_header_s	header;
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

void init_connection(struct fs_connection_s *connection, struct socket_ops_s *sops, unsigned char type, unsigned int flags);
int create_local_serversocket(char *path, struct fs_connection_s *conn, struct beventloop_s *loop, struct fs_connection_s *(* accept_cb)(uid_t uid, gid_t gid, pid_t pid, struct fs_connection_s *s_conn), unsigned int *error);
int connect_remotesocket(struct fs_connection_s *conn, const struct sockaddr *addr, int *len);

struct fs_connection_s *get_containing_connection(struct list_element_s	*list);
struct fs_connection_s *get_next_connection(struct fs_connection_s *s_conn, struct fs_connection_s *c_conn);

int lock_connection_list(struct fs_connection_s *s_conn);
int unlock_connection_list(struct fs_connection_s *s_conn);

int compare_network_connection(struct fs_connection_s *conn, char *address, unsigned int port);

#endif
