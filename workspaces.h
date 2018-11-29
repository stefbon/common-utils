/*
  2010, 2011, 2012, 2013, 2014, 2015, 2016 Stef Bon <stefbon@gmail.com>

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

#ifndef SB_COMMON_UTILS_WORKSPACE_H
#define SB_COMMON_UTILS_WORKSPACE_H

#include <pwd.h>
#include <grp.h>

#include "beventloop.h"
#include "entry-management.h"
#include "directory-management.h"
#include "workspace-interface.h"
#include "fuse-interface.h"
#include "fuse-fs.h"
#include "simple-list.h"
#include "utils.h"

#define WORKSPACE_RULE_POLICY_NONE		0
#define WORKSPACE_RULE_POLICY_SUFFICIENT	1
#define WORKSPACE_RULE_POLICY_REQUIRED		2

#define WORKSPACE_TYPE_DEVICES			1
#define WORKSPACE_TYPE_NETWORK			2
#define WORKSPACE_TYPE_FILE			4
#define WORKSPACE_TYPE_BACKUP			8

#define WORKSPACE_STATUS_OK			1
#define WORKSPACE_STATUS_UNMOUNTING		2
#define WORKSPACE_STATUS_UNMOUNTED		3

#define SERVICE_CTX_TYPE_DUMMY			0
#define SERVICE_CTX_TYPE_WORKSPACE		1
#define SERVICE_CTX_TYPE_SERVICE		2

#define SERVICE_CTX_FLAG_REFCOUNTNONZERO	1

#define FUSE_USER_STATUS_LEN			32

struct workspace_mount_s;

struct fuse_user_s {
    unsigned int				options;
    uid_t 					uid;
    char					status[32];
    pthread_mutex_t				mutex;
    struct list_header_s			workspaces;
    void					(* add_workspace)(struct fuse_user_s *u, struct workspace_mount_s *w);
    void					(* remove_workspace)(struct fuse_user_s *u, struct workspace_mount_s *w);
};

struct workspace_base_s {
    unsigned int 				flags;
    unsigned char 				type;
    char 					*mount_path_template;
    char 					*name;
    gid_t 					ingroup;
    unsigned char 				ingrouppolicy;
    struct workspace_base_s 			*next;
};

struct service_context_s {

    /* */
    unsigned char				flags;

    /* service or fuse mount */
    unsigned char				type;

    /* name, FUSE, SFTP, SMB, etc */
    char					name[32];

    /* unique number per context fs */
    unsigned int				fscount;

    /* unique number for detected service - corresponds with number of discover service */
    unsigned int				serviceid;

    /* the fs used for this service: pseudo, sftp, webdav, smb ... */
    struct service_fs_s				*fs;

    /* the inode the service is attached to */
    struct inode_s 				*inode;

    /* xdata part of eventloop */
    struct bevent_xdata_s 			xdata;

    /* workspace (=mountpoint) this service is part of */
    struct workspace_mount_s			*workspace;

    /* error */
    unsigned int				error;

    /* interface to service (connect, disconnect, umount ...) */
    struct context_interface_s			interface;

    /* refcount: used by other contexes */
    unsigned int				refcount;

    /* part of list */
    struct list_element_s			list;

    /* parent context */
    struct service_context_s			*parent;

};

struct service_fs_s {

    void (*lookup_existing) (struct service_context_s *context, struct fuse_request_s *request, struct entry_s *entry, struct pathinfo_s *pathinfo);
    void (*lookup_new) (struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, struct name_s *xname, struct pathinfo_s *pathinfo);

    void (*getattr) (struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, struct pathinfo_s *pathinfo);
    void (*setattr) (struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, struct pathinfo_s *pathinfo, struct stat *st, unsigned int set);

    void (*readlink) (struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, struct pathinfo_s *pathinfo);

    void (*mkdir) (struct service_context_s *context, struct fuse_request_s *request, struct entry_s *entry, struct pathinfo_s *pathinfo, struct stat *st);
    void (*mknod) (struct service_context_s *context, struct fuse_request_s *request, struct entry_s *entry, struct pathinfo_s *pathinfo, struct stat *st);
    void (*symlink) (struct service_context_s *context, struct fuse_request_s *request, struct entry_s *entry, struct pathinfo_s *pathinfo, const char *link);
    int  (*symlink_validate)(struct service_context_s *context, struct pathinfo_s *pathinfo, char *target, char **remote_target);

    void (*unlink) (struct service_context_s *context, struct fuse_request_s *request, struct entry_s **entry, struct pathinfo_s *pathinfo);
    void (*rmdir) (struct service_context_s *context, struct fuse_request_s *request, struct entry_s **entry, struct pathinfo_s *pathinfo);

    void (*rename) (struct service_context_s *context, struct fuse_request_s *request, struct entry_s **entry, struct pathinfo_s *pathinfo, struct entry_s **n_entry, struct pathinfo_s *n_pathinfo, unsigned int flags);

    void (*open) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct pathinfo_s *pathinfo, unsigned int flags);
    void (*read) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, size_t size, off_t off, unsigned int flags, uint64_t lock_owner);
    void (*write) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, const char *buff, size_t size, off_t off, unsigned int flags, uint64_t lock_owner);
    void (*flush) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, uint64_t lock_owner);
    void (*fsync) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned char datasync);
    void (*release) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned int flags, uint64_t lock_owner);
    void (*create) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct pathinfo_s *pathinfo, struct stat *st, unsigned int flags);

    void (*fgetattr) (struct fuse_openfile_s *openfile, struct fuse_request_s *request);
    void (*fsetattr) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct stat *st, unsigned int set);

    void (*getlock) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock);
    void (*setlock) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock);
    void (*setlockw) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock);

    void (*flock) (struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned char type);

    void (*opendir) (struct fuse_opendir_s *opendir, struct fuse_request_s *request, struct pathinfo_s *pathinfo, unsigned int flags);
    void (*readdir) (struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset);
    void (*readdirplus) (struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset);
    void (*releasedir) (struct fuse_opendir_s *opendir, struct fuse_request_s *request);
    void (*fsyncdir) (struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned char datasync);

    void (*setxattr) (struct service_context_s *context, struct fuse_request_s *request, struct pathinfo_s *pathinfo, struct inode_s *inode, const char *name, const char *value, size_t size, int flags);
    void (*getxattr) (struct service_context_s *context, struct fuse_request_s *request, struct pathinfo_s *pathinfo, struct inode_s *inode, const char *name, size_t size);
    void (*listxattr) (struct service_context_s *context, struct fuse_request_s *request, struct pathinfo_s *pathinfo, struct inode_s *inode, size_t size);
    void (*removexattr) (struct service_context_s *context, struct fuse_request_s *request, struct pathinfo_s *pathinfo, struct inode_s *inode, const char *name);

    void (*fsnotify)(struct service_context_s *context, struct fuse_request_s *request, struct pathinfo_s *pathinfo, uint64_t unique, uint32_t mask);

    void (*statfs)(struct service_context_s *context, struct fuse_request_s *request, struct pathinfo_s *pathinfo);

};

/* fuse mountpoint */

struct workspace_mount_s {
    struct fuse_user_s 				*user;
    struct workspace_base_s			*base;
    struct service_context_s			*context;
    struct inode_s 				rootinode;
    unsigned long long 				nrinodes;
    unsigned char				fscount;
    unsigned int				pathmax;
    pthread_mutex_t				mutex;
    struct pathinfo_s 				mountpoint;
    struct timespec				syncdate;
    unsigned int				status;
    struct list_header_s			contexes;
    void					(* free)(struct workspace_mount_s *mount);
    struct list_element_s			list;
};

/* fuse path relative to the root of a service context */

struct fuse_path_s {
    struct service_context_s 			*context;
    char					*pathstart;
    char					*path;
    unsigned int				len;
};

/* prototypes */

void increase_inodes_workspace(void *data);
void decrease_inodes_workspace(void *data);

void adjust_pathmax(struct workspace_mount_s *w, unsigned int len);
unsigned int get_pathmax(struct workspace_mount_s *w);
unsigned char get_workspace_fs_count(struct workspace_mount_s *workspace);
void set_workspace_fs_count(struct workspace_mount_s *workspace, unsigned char count);

int init_workspace_mount(struct workspace_mount_s *w, unsigned int *error);
int mount_workspace_mount(struct service_context_s *context, char *source, char *name, unsigned int *error);
void umount_workspace_mount(struct service_context_s *context);
void clear_workspace_mount(struct workspace_mount_s *workspace_mount);

void free_workspace_mount(struct workspace_mount_s *workspace);
int get_path_root(struct inode_s *inode, struct fuse_path_s *fpath);

struct workspace_mount_s *get_container_workspace(struct list_element_s *list);
void create_personal_workspace_mount(struct workspace_mount_s *w);

#endif
