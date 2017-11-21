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

#ifndef FUSE_NETINFO_H
#define FUSE_NETINFO_H

#include "simple-hash.h"

#define FUSE_NETINFO_TYPE_FSNOTIFY		1
#define FUSE_NETINFO_TYPE_BLOCK			2
#define FUSE_NETINFO_TYPE_FLOCK			3
#define FUSE_NETINFO_TYPE_LEASE			4

#define FUSE_FSNOTIFY_FLAG_VFS			1
#define FUSE_FSNOTIFY_FLAG_USERSPACE		2
#define FUSE_FSNOTIFY_FLAG_REMOVE		4
#define FUSE_FSNOTIFY_FLAG_INUSE		8

#define FUSE_NETINFO_VALID_USER			1
#define FUSE_NETINFO_INDEX_USER			0
#define FUSE_NETINFO_VALID_HOST			2
#define FUSE_NETINFO_INDEX_HOST			1
#define FUSE_NETINFO_VALID_FILE			4
#define FUSE_NETINFO_INDEX_FILE			2

struct fuse_string_s {
    char				*ptr;
    uint32_t				len;
};

struct fuse_netevent_s {
    uint32_t				opcode;
    union {
	struct fsnotify_info_s {
	    uint32_t			valid;
	    uint64_t			unique;
	    unsigned int 		flags;
	    uint32_t			mask;
	    struct fuse_string_s	user;
	    struct fuse_string_s	host;
	    struct fuse_string_s	file;
	} fsnotify;
    } info;
};

struct fuse_watch_s {
    struct workspace_mount_s		*workspace;
    unsigned int			flags;
    uint32_t				mask;
    unsigned int			refcount;
};

struct fuse_ownerwatch_s {
    struct fs_connection_s		*owner;
    uint64_t				unique;
    uint32_t				mask;
    unsigned int			valid;
    unsigned int			flags;
    signed char				(* report_event)(struct fuse_ownerwatch_s, struct fuse_netevent_s *event);
    struct fuse_watch_s			*watch;
};

/* prototypes */

void remove_fuse_fsnotify_ownerwatch(struct fs_connection_s *owner, uint64_t unique);
uint64_t create_fuse_ownerwatch(struct workspace_mount_s *workspace, uint64_t unique, uint32_t mask, unsigned int valid, struct fs_connection_s *owner);
signed char send_fuse_fsnotify_event(uint64_t unique, struct fuse_netevent_s *event);
void remove_fuse_fsnotify_ownerwatch(struct fs_connection_s *owner, uint64_t unique);

#endif
