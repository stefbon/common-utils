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

#ifndef SB_COMMON_UTILS_FUSE_INTERFACE_H
#define SB_COMMON_UTILS_FUSE_INTERFACE_H

#include "linux/fuse.h"

#define FUSEDATA_FLAG_INTERRUPTED		1
#define FUSEDATA_FLAG_RESPONSE			2
#define FUSEDATA_FLAG_ERROR			4

struct fuse_request_s {
    struct context_interface_s			*interface;
    uint32_t					opcode;
    unsigned int				flags;
    unsigned char				(* is_interrupted)(struct fuse_request_s *request);
    unsigned int				error;
    uint64_t					unique;
    uint64_t					ino;
    uint32_t					uid;
    uint32_t					gid;
    uint32_t					pid;
    unsigned int				size;
    char 					buffer[];
};

void notify_VFS_delete(void *ptr, uint64_t pino, uint64_t ino, char *name, unsigned int len);
void notify_VFS_create(void *ptr, uint64_t pino, char *name);
void notify_VFS_change(void *ptr, uint64_t ino, uint32_t mask);

void notify_VFS_fsnotify(void *ptr, uint64_t  ino, uint32_t mask);
void notify_VFS_fsnotify_child(void *ptr, uint64_t ino, uint32_t mask, struct name_s *xname);

int init_fuse_interface(struct context_interface_s *interface);
void close_fuse_interface(struct context_interface_s *interface);
void free_fuse_interface(struct context_interface_s *interface);
void register_fuse_function(void *ptr, uint32_t opcode, void (* func) (struct fuse_request_s *request));

void disable_masking_userspace(void *ptr);
mode_t get_masked_permissions(void *ptr, mode_t perm, mode_t mask);

unsigned char set_request_interrupted(void *ptr, uint64_t unique);
unsigned char signal_request_response(void *ptr, uint64_t unique);
unsigned char signal_request_error(void *ptr, uint64_t unique, unsigned int error);
unsigned char wait_service_response(void *ptr, struct fuse_request_s *request, struct timespec *timeout);

unsigned char fuse_request_interrupted(struct fuse_request_s *request);

pthread_mutex_t *get_fuse_pthread_mutex(struct context_interface_s *interface);
pthread_cond_t *get_fuse_pthread_cond(struct context_interface_s *interface);
uint64_t *get_fuse_interrupted_id(struct context_interface_s *interface);

void reply_VFS_data(struct fuse_request_s *r, char *buffer, size_t size);
void reply_VFS_error(struct fuse_request_s *r, unsigned int error);
void reply_VFS_nosys(struct fuse_request_s *r);
void reply_VFS_xattr(struct fuse_request_s *r, size_t size);

struct timespec *get_fuse_interface_attr_timeout(void *ptr);
struct timespec *get_fuse_interface_entry_timeout(void *ptr);
struct timespec *get_fuse_interface_negative_timeout(void *ptr);

size_t add_direntry_buffer(void *ptr, char *buffer, size_t size, off_t offset, struct name_s *xname, struct stat *st, unsigned int *error);
size_t add_direntry_plus_buffer(void *ptr, char *buffer, size_t size, off_t offset, struct name_s *xname, struct stat *st, unsigned int *error);

#endif
