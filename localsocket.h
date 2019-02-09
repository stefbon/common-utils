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

#include "beventloop.h"
#include "simple-list.h"
#include "pathinfo.h"
#include "network-utils.h"

#define FS_CONNECTION_ROLE_SERVER					1
#define FS_CONNECTION_ROLE_CLIENT					2

#define LISTEN_BACKLOG 50

#define FS_CONNECTION_TYPE_LOCAL					1
#define FS_CONNECTION_TYPE_TCP4						2
#define FS_CONNECTION_TYPE_TCP6						3
#define FS_CONNECTION_TYPE_UDP4						4
#define FS_CONNECTION_TYPE_UDP6						5
#define FS_CONNECTION_TYPE_FUSE						6

#define SOCKET_OPS_TYPE_ZERO						0
#define SOCKET_OPS_TYPE_DEFAULT						1

#define FUSE_OPS_TYPE_ZERO						0
#define FUSE_OPS_TYPE_DEFAULT						1

#define FS_CONNECTION_FLAG_INIT						1
#define FS_CONNECTION_FLAG_CONNECTING					2
#define FS_CONNECTION_FLAG_CONNECTED					4
#define FS_CONNECTION_FLAG_EVENTLOOP					8
#define FS_CONNECTION_FLAG_DISCONNECTING				16
#define FS_CONNECTION_FLAG_DISCONNECTED					32

#define FS_CONNECTION_COMPARE_HOST					1

struct fs_connection_s;

typedef void (* event_cb)(struct fs_connection_s *conn, void *data, uint32_t events);
typedef struct fs_connection_s *(* accept_local_cb)(uid_t uid, gid_t gid, pid_t pid, struct fs_connection_s *conn);
typedef struct fs_connection_s *(* accept_network_cb)(struct host_address_s *host, struct fs_connection_s *conn);
typedef void (* disconnect_cb)(struct fs_connection_s *conn, unsigned char remote);
typedef void (* init_cb)(struct fs_connection_s *conn, unsigned int fd);

struct io_socket_s {
    unsigned char				type;
    struct socket_ops_s				*sops;
    struct bevent_xdata_s			xdata;
    union {
	struct sockaddr_un 			local;
	struct sockaddr_in			inet;
	struct sockaddr_in6			inet6;
    } sockaddr;
};


struct socket_ops_s {
    unsigned char				type;
    int						(* accept)(unsigned int fd, struct sockaddr *addr, unsigned int *len);
    int						(* bind)(unsigned int fd, struct sockaddr *addr, int *len, int sock);
    int						(* close)(unsigned int fd);
    int						(* connect)(unsigned int fd, struct sockaddr *addr, int *len);
    int						(* getpeername)(unsigned int fd, struct sockaddr *addr, unsigned int *len);
    int						(* getsockname)(unsigned int fd, struct sockaddr *addr, unsigned int *len);
    int						(* getsockopt)(unsigned int fd, int level, int optname, char *optval, unsigned int *optlen);
    int						(* setsockopt)(unsigned int fd, int level, int optname, char *optval, unsigned int optlen);
    int						(* listen)(unsigned int fd, int len);
    int						(* socket)(int af, int type, int protocol);
    int						(* recv)(struct io_socket_s *s, char *buffer, unsigned int size, unsigned int flags);
    int						(* send)(struct io_socket_s *s, char *buffer, unsigned int size, unsigned int flags);
    int						(* start)();
    int						(* finish)();
};

struct io_fuse_s {
    struct fuse_ops_s				*fops;
    struct bevent_xdata_s			xdata;
};

struct fuse_ops_s {
    unsigned char				type;
    int						(* open)(char *path, unsigned int flags);
    int						(* close)(unsigned int fd);
    ssize_t					(* writev)(struct io_fuse_s *s, struct iovec *iov, int count);
    int						(* read)(struct io_fuse_s *s, void *buffer, size_t size);
};

struct fs_connection_s {
    unsigned char 				type;
    unsigned char				role;
    unsigned char				status;
    unsigned int				error;
    unsigned int				expire;
    void 					*data;
    struct list_element_s			list;
    union io_target_s {
	struct io_socket_s			socket;
	struct io_fuse_s			fuse;
    } io;
    union {
	struct server_ops_s {
	    union {
		accept_local_cb			local;
		accept_network_cb		network;
	    } accept;
	    struct list_header_s		header;
	    pthread_mutex_t			mutex;
	} server;
	struct client_ops_s {
	    union {
		struct local_client_s {
		    uid_t			uid;
		    pid_t			pid;
		} local;
		struct fuse_client_s {
		    uid_t			uid;
		    gid_t			gid;
		} fuse;
		struct host_address_s 		host;
	    } id;
	    disconnect_cb			disconnect;
	    event_cb 				event;
	    init_cb				init;
	    struct fs_connection_s		*server;
	} client;
    } ops;
};

/* Prototypes */

void set_io_socket_ops_zero(struct io_socket_s *s);
void set_io_socket_ops_default(struct io_socket_s *s);
void set_io_fuse_ops_zero(struct io_fuse_s *s);
void set_io_fuse_ops_default(struct io_fuse_s *s);

int create_socket_path(struct pathinfo_s *pathinfo);
int check_socket_path(struct pathinfo_s *pathinfo, unsigned int already);

void init_connection(struct fs_connection_s *connection, unsigned char type, unsigned char role);
int create_local_serversocket(char *path, struct fs_connection_s *conn, struct beventloop_s *loop, struct fs_connection_s *(* accept_cb)(uid_t uid, gid_t gid, pid_t pid, struct fs_connection_s *s_conn), unsigned int *error);
int create_network_serversocket(struct fs_connection_s *conn, struct beventloop_s *loop, struct fs_connection_s *(* accept_cb)(struct host_address_s *h, struct fs_connection_s *s), unsigned int *error);

int connect_socket(struct fs_connection_s *conn, const struct sockaddr *addr, int *len);
int close_socket(struct fs_connection_s *conn);

struct fs_connection_s *get_containing_connection(struct list_element_s	*list);
struct fs_connection_s *get_next_connection(struct fs_connection_s *s_conn, struct fs_connection_s *c_conn);

int lock_connection_list(struct fs_connection_s *s_conn);
int unlock_connection_list(struct fs_connection_s *s_conn);

int compare_network_address(struct fs_connection_s *conn, char *address, unsigned int port);
int compare_network_connection(struct fs_connection_s *a, struct fs_connection_s *b, unsigned int flags);

int get_connection_info(struct fs_connection_s *a, const char *what);

char *get_connection_ipv4(struct fs_connection_s *a, unsigned int fd, unsigned char what, unsigned int *error);
char *get_connection_ipv6(struct fs_connection_s *a, unsigned int fd, unsigned char what, unsigned int *error);
char *get_connection_hostname(struct fs_connection_s *a, unsigned int fd, unsigned char what, unsigned int *error);

#endif
