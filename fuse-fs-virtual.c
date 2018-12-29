/*
  2010, 2011, 2012, 2103, 2014, 2015, 2016 Stef Bon <stefbon@gmail.com>

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
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <err.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>
#include <inttypes.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/vfs.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "logging.h"
#include "pathinfo.h"
#include "utils.h"
#include "beventloop.h"
#include "fuse-dentry.h"
#include "fuse-directory.h"
#include "fuse-utils.h"
#include "fuse-fs.h"
#include "workspaces.h"
#include "fuse-fs-virtual.h"
#include "fuse-fs-common.h"

#define UINT32_T_MAX		0xFFFFFFFF

static struct fuse_fs_s virtual_dir_fs;
static struct fuse_fs_s virtual_nondir_fs;
static unsigned char done=0;
static pthread_mutex_t done_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t datalink_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct statfs default_statfs;

// void use_virtual_fs(struct service_context_s *context, struct inode_s *inode);

static int _lock_datalink(struct inode_s *inode)
{
    return pthread_mutex_lock(&datalink_mutex);
}

static int _unlock_datalink(struct inode_s *inode)
{
    return pthread_mutex_unlock(&datalink_mutex);
}
static void _fs_forget(struct inode_s *inode)
{
}

static void _fs_get_inode_link_dir(struct inode_s *inode, struct inode_link_s **link)
{
    struct directory_s *directory=NULL;

    logoutput("_fs_get_inode_link_dir: ino %li", inode->st.st_ino);

    pthread_mutex_lock(&datalink_mutex);
    directory=(struct directory_s *) inode->link.link.ptr;
    (* directory->dops->get_inode_link)(directory, inode, link);
    pthread_mutex_unlock(&datalink_mutex);
}

static void _fs_get_inode_link_nondir(struct inode_s *inode, struct inode_link_s **link)
{

    logoutput("_fs_get_inode_link_nondir");

    pthread_mutex_lock(&datalink_mutex);
    *link=&inode->link;
    pthread_mutex_unlock(&datalink_mutex);
}

static void _fs_lookup(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, unsigned int len)
{
    _fs_common_virtual_lookup(context, request, inode, name, len);
}

static void _fs_getattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    _fs_common_getattr(context, request, inode);
}

static void _fs_setattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, struct stat *st, unsigned int set)
{
    _fs_common_getattr(context, request, inode);
}

static void _fs_readlink(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    reply_VFS_error(request, EINVAL);
}

static void _fs_mkdir(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, unsigned int len, mode_t mode, mode_t umask)
{
    reply_VFS_error(request, EACCES);
}

static void _fs_mknod(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, unsigned int len, mode_t mode, dev_t rdev, mode_t umask)
{
    reply_VFS_error(request, EACCES);
}

static void _fs_symlink(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, unsigned int len0, const char *target, unsigned int len1)
{
    reply_VFS_error(request, EACCES);
}

static void _fs_rmdir(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, unsigned int len)
{
    reply_VFS_error(request, EACCES);
}

static void _fs_unlink(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, unsigned int len)
{
    reply_VFS_error(request, EACCES);
}

static void _fs_rename(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, struct inode_s *inode_new, const char *newname, unsigned int flags)
{
    reply_VFS_error(request, EACCES);
}

static void _fs_open(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned int flags)
{
    openfile->error=EACCES;
    reply_VFS_error(request, EACCES);
}

static void _fs_read(struct fuse_openfile_s *openfile, struct fuse_request_s *request, size_t size, off_t offset, unsigned int flags, uint64_t lock_owner)
{
    reply_VFS_error(request, EACCES);
}

static void _fs_write(struct fuse_openfile_s *openfile, struct fuse_request_s *request, const char *buffer, size_t size, off_t offset, unsigned int flags, uint64_t lock_owner)
{
    reply_VFS_error(request, EACCES);
}

static void _fs_flush(struct fuse_openfile_s *openfile, struct fuse_request_s *request, uint64_t lock_owner)
{
    reply_VFS_error(request, 0);
}

static void _fs_fsync(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned char sync)
{
    reply_VFS_error(request, 0);
}

static void _fs_release(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned int flags, uint64_t lock_owner)
{
    reply_VFS_error(request, 0);
}

static void _fs_getlock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock)
{
    reply_VFS_error(request, 0);
}
static void _fs_setlock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock)
{
    reply_VFS_error(request, 0);
}

static void _fs_setlockw(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock)
{
    reply_VFS_error(request, 0);
}

static void _fs_flock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned char type)
{
    reply_VFS_error(request, 0);
}

static void _fs_create(struct fuse_openfile_s *openfile, struct fuse_request_s *request, const char *name, unsigned int len, unsigned int flags, mode_t mode, mode_t mask)
{
    openfile->error=EACCES;
    reply_VFS_error(request, EACCES);
}

static void _fs_fgetattr(struct fuse_openfile_s *openfile, struct fuse_request_s *request)
{
    _fs_common_getattr(openfile->context, request, openfile->inode);
}

static void _fs_fsetattr(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct stat *st, int fuse_set)
{
    _fs_common_getattr(openfile->context, request, openfile->inode);
}

static void _fs_opendir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned int flags)
{
    _fs_common_virtual_opendir(opendir, request, flags);
}

static void _fs_readdir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset)
{
    _fs_common_virtual_readdir(opendir, request, size, offset);
}

static void _fs_readdirplus(struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset)
{
    _fs_common_virtual_readdirplus(opendir, request, size, offset);
}

static void _fs_fsyncdir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned char datasync)
{
    reply_VFS_error(request, 0);
}

static void _fs_releasedir(struct fuse_opendir_s *opendir, struct fuse_request_s *request)
{
    _fs_common_virtual_releasedir(opendir, request);
}

static void _fs_fsnotify(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, uint32_t mask)
{
    logoutput("_fs_fsnotify: watch on %li mask %i", (unsigned long) inode->st.st_ino, mask);
}

static void _fs_setxattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, const char *value, size_t size, int flags)
{

    if (inode==&context->workspace->rootinode) logoutput("_fs_setxattr: root inode");
    reply_VFS_error(request, ENODATA);
}

static void _fs_getxattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, size_t size)
{
    if (inode==&context->workspace->rootinode) logoutput("_fs_getxattr: root inode");
    reply_VFS_error(request, ENODATA);
}

static void _fs_listxattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, size_t size)
{
    if (inode==&context->workspace->rootinode) logoutput("_fs_listxattr: root inode");
    reply_VFS_error(request, ENODATA);
}

static void _fs_removexattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name)
{
    if (inode==&context->workspace->rootinode) logoutput("_fs_removexattr: root inode");
    reply_VFS_error(request, ENODATA);
}

static void _fs_statfs(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    _fs_common_statfs(context, request, inode, default_statfs.f_blocks, default_statfs.f_bfree, default_statfs.f_bavail, default_statfs.f_bsize);
}

static void init_virtual_fs();

void use_virtual_fs(struct service_context_s *context, struct inode_s *inode)
{

    logoutput("use_virtual_fs");

    pthread_mutex_lock(&done_mutex);

    if (done==0) {
	init_virtual_fs();
	done=1;
    }

    pthread_mutex_unlock(&done_mutex);

    if (S_ISDIR(inode->st.st_mode)) {

	inode->fs=&virtual_dir_fs;

    } else {

	inode->fs=&virtual_nondir_fs;

    }

}

static void _set_virtual_fs(struct fuse_fs_s *fs)
{

    fs->lock_datalink=_lock_datalink;
    fs->unlock_datalink=_unlock_datalink;

    fs->forget=_fs_forget;
    fs->getattr=_fs_getattr;
    fs->setattr=_fs_setattr;

    if ((fs->flags & FS_SERVICE_FLAG_DIR) || (fs->flags & FS_SERVICE_FLAG_ROOT)) {

	fs->get_inode_link=_fs_get_inode_link_dir;
	fs->type.dir.use_fs=use_virtual_fs;
	fs->type.dir.lookup=_fs_lookup;

	fs->type.dir.create=_fs_create;
	fs->type.dir.mkdir=_fs_mkdir;
	fs->type.dir.mknod=_fs_mknod;
	fs->type.dir.symlink=_fs_symlink;

	fs->type.dir.unlink=_fs_unlink;
	fs->type.dir.rmdir=_fs_rmdir;

	fs->type.dir.rename=_fs_rename;

	fs->type.dir.opendir=_fs_opendir;
	fs->type.dir.readdir=_fs_readdir;
	fs->type.dir.readdirplus=_fs_readdirplus;
	fs->type.dir.releasedir=_fs_releasedir;
	fs->type.dir.fsyncdir=_fs_fsyncdir;

	fs->type.dir.fsnotify=_fs_fsnotify;

    } else {

	fs->get_inode_link=_fs_get_inode_link_nondir;

	fs->type.nondir.readlink=_fs_readlink;

	fs->type.nondir.open=_fs_open;
	fs->type.nondir.read=_fs_read;
	fs->type.nondir.write=_fs_write;
	fs->type.nondir.flush=_fs_flush;
	fs->type.nondir.fsync=_fs_fsync;
	fs->type.nondir.release=_fs_release;

	fs->type.nondir.fgetattr=_fs_fgetattr;
	fs->type.nondir.fsetattr=_fs_fsetattr;

	fs->type.nondir.getlock=_fs_getlock;
	fs->type.nondir.setlock=_fs_setlock;
	fs->type.nondir.setlockw=_fs_setlockw;

	fs->type.nondir.flock=_fs_flock;

    }

    fs->setxattr=_fs_setxattr;
    fs->getxattr=_fs_getxattr;
    fs->listxattr=_fs_listxattr;
    fs->removexattr=_fs_removexattr;

    fs->statfs=_fs_statfs;

}

static void init_virtual_fs()
{
    struct fuse_fs_s *fs=NULL;

    if (statfs("/", &default_statfs)==-1) {

	logoutput_warning("init_virtual_fs: cannot stat root filesystem at /... taking default fs values");

	default_statfs.f_blocks=1000000;
	default_statfs.f_bfree=1000000;
	default_statfs.f_bavail=default_statfs.f_bfree;
	default_statfs.f_bsize=4096;

    }

    fs=&virtual_dir_fs;

    memset(fs, 0, sizeof(struct fuse_fs_s));
    fs->flags=FS_SERVICE_FLAG_DIR | FS_SERVICE_FLAG_VIRTUAL;
    _set_virtual_fs(fs);

    fs=&virtual_nondir_fs;

    memset(fs, 0, sizeof(struct fuse_fs_s));
    fs->flags=FS_SERVICE_FLAG_NONDIR | FS_SERVICE_FLAG_VIRTUAL;
    _set_virtual_fs(fs);

}


void set_virtual_fs(struct fuse_fs_s *fs)
{
    pthread_mutex_lock(&done_mutex);

    if (done==0) {
	init_virtual_fs();
	done=1;
    }

    pthread_mutex_unlock(&done_mutex);

    _set_virtual_fs(fs);
}
