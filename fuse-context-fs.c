/*
  2010, 2011, 2012, 2103, 2014, 2015, 2016, 2017 Stef Bon <stefbon@gmail.com>

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

#define LOGGING
#include "logging.h"

#include "main.h"
#include "pathinfo.h"
#include "beventloop.h"
#include "utils.h"

#include "entry-management.h"
#include "directory-management.h"
#include "entry-utils.h"

#include "fuse-fs.h"
#include "workspaces.h"
#include "workspace-context.h"

#include "fuse-fs-common.h"
#include "path-caching.h"
#include "fuse-context-fs-common.h"
#include "fuse-fs-virtual.h"
#include "fuse-context-fs.h"

static struct fuse_fs_s service_dir_fs;
static struct fuse_fs_s service_nondir_fs;
static unsigned char done=0;
static pthread_mutex_t done_mutex=PTHREAD_MUTEX_INITIALIZER;

static void service_fs_forget(struct inode_s *inode)
{
}

/* LOOKUP */

static void service_fs_lookup(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len)
{
    unsigned int error=0;
    struct name_s xname={(char *)name, len, 0};
    struct entry_s *entry=NULL;
    struct pathcalls_s *pathcalls=NULL;
    unsigned int pathlen=get_pathmax(context->workspace) + xname.len + 1;
    char path[pathlen+1];
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct directory_s *directory=NULL;
    struct fuse_path_s fpath;

    logoutput("service_fs_lookup");

    init_fuse_path(&fpath, path, pathlen);

    pathcalls=get_pathcalls(pinode);
    logoutput("service_fs_lookup: A1");
    directory=get_directory(pinode);
    logoutput("service_fs_lookup: A2");

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	return;

    }

    pathinfo.len=add_name_path(&fpath, &xname);

    lock_pathcalls(pathcalls);
    pathinfo.len+=get_path_pathcalls(directory, &fpath);
    unlock_pathcalls(pathcalls);

    pathinfo.path=fpath.pathstart;

    calculate_nameindex(&xname);
    entry=find_entry(pinode->alias, &xname, &error);

    logoutput("LOOKUP %s (thread %i) %s", fpath.context->name, (int) gettid(), pathinfo.path);

    if (entry) {

	(* fpath.context->fs->lookup_existing)(fpath.context, request, entry, &pathinfo);

    } else {

	//if (strcmp(name, ".directory")==0) {

	//    reply_VFS_error(request, ENOENT);

	//} else {

	    (* fpath.context->fs->lookup_new)(fpath.context, request, pinode, &xname, &pathinfo);

	//}

    }

}

/* GETATTR */

static void service_fs_getattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct entry_s *entry=inode->alias;
    unsigned int pathlen=get_pathmax(context->workspace);
    char path[pathlen + 1];
    struct pathcalls_s *pathcalls=NULL;
    struct directory_s *directory=NULL;
    unsigned int error=0;
    struct fuse_path_s fpath;

    init_fuse_path(&fpath, path, pathlen);

    if (S_ISDIR(inode->st.st_mode)) {

	pathcalls=get_pathcalls(inode);
	directory=get_directory(inode);

    } else {
	struct entry_s *parent=entry->parent;

	pathinfo.len=add_name_path(&fpath, &entry->name);

	pathcalls=get_pathcalls(parent->inode);
	directory=get_directory(parent->inode);

    }

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	return;

    }

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    pathinfo.path=fpath.pathstart;
    context=fpath.context;

    logoutput("GETATTR %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);

    (* context->fs->getattr)(context, request, inode, &pathinfo);

}

/* SETATTR */

static void service_fs_setattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, struct stat *st, unsigned int set)
{
    struct entry_s *entry=inode->alias;
    struct entry_s *parent=entry->parent;
    unsigned int pathlen=get_pathmax(context->workspace);
    char path[pathlen + 1];
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct pathcalls_s *pathcalls=NULL;
    struct directory_s *directory=NULL;
    unsigned int error=0;
    struct fuse_path_s fpath;

    /* access */

    if (request->uid!=context->workspace->user->uid) {

	reply_VFS_error(request, EACCES);
	return;

    }

    init_fuse_path(&fpath, path, pathlen);

    if (S_ISDIR(inode->st.st_mode)) {

	pathcalls=get_pathcalls(inode);
	directory=get_directory(inode);

    } else {
	struct entry_s *parent=inode->alias->parent;

	pathinfo.len=add_name_path(&fpath, &entry->name);

	pathcalls=get_pathcalls(parent->inode);
	directory=get_directory(parent->inode);

    }

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	return;

    }

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    pathinfo.path=fpath.pathstart;
    context=fpath.context;

    logoutput("SETATTR %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);

    (* context->fs->setattr)(context, request, inode, &pathinfo, st, set);
}

/* MKDIR */

static void service_fs_mkdir(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len, mode_t mode, mode_t mask)
{
    unsigned int error=0;
    struct name_s xname={(char *)name, len-1, 0};
    mode_t dirtype=(mode & S_IFMT);
    mode_t dirperm=0;
    struct entry_s *entry=NULL;
    struct stat st;
    struct directory_s *directory=NULL;

    /* access */

    if (request->uid!=context->workspace->user->uid) {

	reply_VFS_error(request, EACCES);
	return;

    }

    dirperm=get_masked_permissions(context->interface.ptr, mode - dirtype, mask);

    memset(&st, 0, sizeof(struct stat));

    st.st_mode=S_IFDIR | dirperm;
    st.st_uid=request->uid;
    st.st_gid=request->gid;
    st.st_size=0;

    st.st_blksize=4096; /* get from local config/parameters */

    get_current_time(&st.st_atim);
    memcpy(&st.st_mtim, &st.st_atim, sizeof(struct timespec));
    memcpy(&st.st_ctim, &st.st_atim, sizeof(struct timespec));

    calculate_nameindex(&xname);
    directory=get_directory(pinode);
    entry=_fs_common_create_entry_unlocked(context->workspace, directory, &xname, &st, 0, 0, &error);

    /* entry created local and no error (no EEXIST!) */

    if (entry && error==0) {
	struct pathcalls_s *pathcalls=NULL;
	struct directory_s *directory=NULL;
	unsigned int pathlen=get_pathmax(context->workspace) + 1 + xname.len;
	char path[pathlen + 1];
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	char *pathstart=NULL;
	struct fuse_path_s fpath;

	init_fuse_path(&fpath, path, pathlen);

	pathcalls=get_pathcalls(pinode);
	directory=get_directory(pinode);

	if (! directory) {

	    reply_VFS_error(request, ENOMEM);
	    return;

	}

	pathinfo.len=add_name_path(&fpath, &xname);

	lock_pathcalls(pathcalls);
	pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
	unlock_pathcalls(pathcalls);

	pathinfo.path=fpath.pathstart;
	context=fpath.context;

	logoutput("MKDIR %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);

	(* context->fs->mkdir)(context, request, entry, &pathinfo, &st);

    } else {

	reply_VFS_error(request, error);

    }

}

/* MKNOD */

static void service_fs_mknod(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len, mode_t mode, dev_t rdev, mode_t mask)
{
    unsigned int error=0;
    struct name_s xname={(char *)name, len-1, 0};
    mode_t filetype=(mode & S_IFMT);
    mode_t fileperm=0;
    struct entry_s *entry=NULL;
    struct stat st;
    struct directory_s *directory=NULL;

    /* access */

    if (request->uid!=context->workspace->user->uid) {

	reply_VFS_error(request, EACCES);
	return;

    }

    fileperm=get_masked_permissions(context->interface.ptr, mode - filetype, mask);

    /* parameters */

    if (filetype==0 || ! S_ISREG(filetype)) {

	reply_VFS_error(request, EINVAL);
	return;

    }

    memset(&st, 0, sizeof(struct stat));

    st.st_mode=S_IFREG | fileperm;
    st.st_uid=request->uid;
    st.st_gid=request->gid;
    st.st_size=0;

    st.st_blksize=4096; /* get from local config/parameters */

    get_current_time(&st.st_atim);
    memcpy(&st.st_mtim, &st.st_atim, sizeof(struct timespec));
    memcpy(&st.st_ctim, &st.st_atim, sizeof(struct timespec));

    calculate_nameindex(&xname);
    directory=get_directory(pinode);
    entry=_fs_common_create_entry_unlocked(context->workspace, directory, &xname, &st, 0, 0, &error);

    /* entry created local and no error (no EEXIST!) */

    if (entry && error==0) {
	struct pathcalls_s *pathcalls=NULL;
	struct directory_s *directory=NULL;
	unsigned int pathlen=get_pathmax(context->workspace) + 1 + xname.len;
	char path[pathlen + 1];
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	char *pathstart=NULL;
	struct fuse_path_s fpath;

	init_fuse_path(&fpath, path, pathlen);

	pathcalls=get_pathcalls(pinode);
	directory=get_directory(pinode);

	if (! directory) {

	    reply_VFS_error(request, ENOMEM);
	    return;

	}

	pathinfo.len=add_name_path(&fpath, &xname);

	lock_pathcalls(pathcalls);
	pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
	unlock_pathcalls(pathcalls);

	pathinfo.path=fpath.pathstart;
	context=fpath.context;

	logoutput("MKNOD %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);

	(* context->fs->mkdir)(context, request, entry, &pathinfo, &st);

    } else {

	reply_VFS_error(request, error);

    }

}

/* SYMLINK */

static void service_fs_symlink(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len0, const char *target, unsigned int len1)
{
    unsigned int error=0;
    struct name_s xname={(char *)name, len0-1, 0};
    struct entry_s *entry=NULL;
    struct stat st;
    struct directory_s *directory=NULL;

    memset(&st, 0, sizeof(struct stat));

    st.st_mode=S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO;
    st.st_uid=request->uid;
    st.st_gid=request->gid;
    st.st_size=0;

    st.st_blksize=4096; /* get from local config/parameters */

    get_current_time(&st.st_atim);
    memcpy(&st.st_mtim, &st.st_atim, sizeof(struct timespec));
    memcpy(&st.st_ctim, &st.st_atim, sizeof(struct timespec));

    calculate_nameindex(&xname);
    directory=get_directory(pinode);
    entry=_fs_common_create_entry_unlocked(context->workspace, directory, &xname, &st, 0, 0, &error);

    /* entry created local and no error (no EEXIST!) */

    if (entry && error==0) {
	struct pathcalls_s *pathcalls=NULL;
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	unsigned int pathlen=get_pathmax(context->workspace) + xname.len + 1;
	char path[pathlen + 1];
	struct directory_s *directory=NULL;
	struct fuse_path_s fpath;
	char *remote_target=NULL;

	init_fuse_path(&fpath, path, pathlen);

	pathcalls=get_pathcalls(pinode);
	directory=get_directory(pinode);

	if (! directory) {

	    reply_VFS_error(request, ENOMEM);
	    return;

	}

	pathinfo.len=add_name_path(&fpath, &xname);

	lock_pathcalls(pathcalls);
	pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
	unlock_pathcalls(pathcalls);

	pathinfo.path=fpath.pathstart;
	context=fpath.context;

	logoutput("SYMLINK %s (thread %i) %s to %s", context->name, (int) gettid(), pathinfo.path, target);

	if ((* context->fs->symlink_validate)(context, &pathinfo, (char *)target, &remote_target)==0) {

	    (* context->fs->symlink)(context, request, entry, &pathinfo, remote_target);
	    free(remote_target);

	} else {

	    reply_VFS_error(request, EPERM);
	    if (entry) remove_inode(&context->interface, entry->inode);

	}

    } else {

	reply_VFS_error(request, error);

    }

}

/* REMOVE/UNLINK */

static void service_fs_unlink(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len)
{
    unsigned int error=0;
    struct name_s xname={(char *)name, len-1, 0};
    struct entry_s *entry=NULL;

    calculate_nameindex(&xname);
    entry=find_entry(pinode->alias, &xname, &error);

    if (entry) {
	struct pathcalls_s *pathcalls=NULL;
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	unsigned int pathlen=get_pathmax(context->workspace) + xname.len + 1;
	char path[pathlen + 1];
	struct directory_s *directory=NULL;
	struct fuse_path_s fpath;

	init_fuse_path(&fpath, path, pathlen);

	pathcalls=get_pathcalls(pinode);
	directory=get_directory(pinode);

	if (! directory) {

	    reply_VFS_error(request, ENOMEM);
	    return;

	}

	pathinfo.len=add_name_path(&fpath, &xname);

	lock_pathcalls(pathcalls);
	pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
	unlock_pathcalls(pathcalls);

	pathinfo.path=fpath.pathstart;
	context=fpath.context;

	logoutput("UNLINK %s (thread %i) %s", context->name, (int) gettid(), pathinfo.path);

	(* context->fs->unlink)(context, request, &entry, &pathinfo);

    } else {

	reply_VFS_error(request, ENOENT);
	return;

    }

}

/* RMDIR */

static void service_fs_rmdir(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len)
{
    unsigned int error=0;
    struct name_s xname={(char *)name, len-1, 0};
    struct entry_s *entry=NULL;

    calculate_nameindex(&xname);
    entry=find_entry(pinode->alias, &xname, &error);

    if (entry) {
	struct pathcalls_s *pathcalls=NULL;
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	unsigned int pathlen=get_pathmax(context->workspace) + xname.len + 1;
	char path[pathlen + 1];
	struct directory_s *directory=NULL, *sub_directory=NULL;
	struct fuse_path_s fpath;

	sub_directory=get_directory(entry->inode);

	if (sub_directory && sub_directory->count>0) {

	    reply_VFS_error(request, ENOTEMPTY);
	    return;

	}

	init_fuse_path(&fpath, path, pathlen);

	pathcalls=get_pathcalls(pinode);
	directory=get_directory(pinode);

	if (! directory) {

	    reply_VFS_error(request, ENOMEM);
	    return;

	}

	pathinfo.len=add_name_path(&fpath, &xname);

	lock_pathcalls(pathcalls);
	pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
	unlock_pathcalls(pathcalls);

	pathinfo.path=fpath.pathstart;
	context=fpath.context;

	logoutput("RMDIR %s (thread %i) %s", context->name, (int) gettid(), pathinfo.path);

	(* context->fs->rmdir)(context, request, &entry, &pathinfo);
	if (entry==NULL && sub_directory) destroy_directory(sub_directory);

    } else {

	reply_VFS_error(request, ENOENT);
	return;

    }

}

/* READLINK */

static void service_fs_readlink(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    struct entry_s *entry=inode->alias;
    struct entry_s *parent=entry->parent;
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct directory_s *directory=NULL;
    struct pathcalls_s *pathcalls=NULL;
    unsigned int pathlen=get_pathmax(context->workspace);
    char path[pathlen + 1];
    struct fuse_path_s fpath;
    unsigned int error=0;

    logoutput("service_fs_readlink: pathlen %i)", pathlen);

    init_fuse_path(&fpath, path, pathlen);

    pathcalls=get_pathcalls(parent->inode);
    directory=get_directory(parent->inode);

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	return;

    };

    pathinfo.len=add_name_path(&fpath, &entry->name);

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    pathinfo.path=fpath.pathstart;
    context=fpath.context;

    logoutput("READLINK %s (thread %i) %s", context->name, (int) gettid(), pathinfo.path);

    (* context->fs->readlink)(context, request, inode, &pathinfo);

}

/* OPEN */

static void service_fs_open(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned int flags)
{
    struct service_context_s *context=openfile->context;
    unsigned int error=0;
    struct entry_s *entry=openfile->inode->alias;
    struct entry_s *parent=entry->parent;
    struct directory_s *directory=NULL;
    struct pathcalls_s *pathcalls=NULL;
    unsigned int pathlen=get_pathmax(context->workspace);
    char path[pathlen + 1];
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct fuse_path_s fpath;

    init_fuse_path(&fpath, path, pathlen);

    pathcalls=get_pathcalls(parent->inode);
    directory=get_directory(parent->inode);

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	openfile->error=ENOMEM;
	return;

    }

    pathinfo.len=add_name_path(&fpath, &entry->name);

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    context=fpath.context;
    openfile->context=context;
    pathinfo.path=fpath.pathstart;

    logoutput("OPEN %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);

    (* context->fs->open)(openfile, request, &pathinfo, flags);

}

/* CREATE */

static void service_fs_create(struct fuse_openfile_s *openfile, struct fuse_request_s *request, const char *name, unsigned int len, unsigned int flags, mode_t mode, mode_t mask)
{
    struct service_context_s *context=openfile->context;
    unsigned int error=0;
    struct name_s xname={(char *)name, len-1, 0};
    struct directory_s *directory=NULL;
    struct pathcalls_s *pathcalls=NULL;
    mode_t filetype=(mode & S_IFMT);
    mode_t fileperm=0;
    struct entry_s *entry=NULL;
    struct entry_s *parent=NULL;
    struct stat st;

    fileperm=get_masked_permissions(context->interface.ptr, mode - filetype, mask);

    /* parameters */

    if (filetype==0 || ! S_ISREG(filetype)) {

	reply_VFS_error(request, EINVAL);
	openfile->error=EINVAL;
	return;

    }

    /* access */

    if (request->uid!=context->workspace->user->uid) {

	reply_VFS_error(request, EACCES);
	openfile->error=EACCES;
	return;

    }

    pathcalls=get_pathcalls(openfile->inode);
    directory=get_directory(openfile->inode);

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	openfile->error=ENOMEM;
	return;

    }

    parent=openfile->inode->alias;

    memset(&st, 0, sizeof(struct stat));

    st.st_mode=S_IFREG | fileperm;
    st.st_uid=request->uid;
    st.st_gid=request->gid;
    st.st_size=0;

    st.st_blksize=4096; /* get from local config/parameters */

    get_current_time(&st.st_atim);
    memcpy(&st.st_mtim, &st.st_atim, sizeof(struct timespec));
    memcpy(&st.st_ctim, &st.st_atim, sizeof(struct timespec));

    calculate_nameindex(&xname);
    entry=_fs_common_create_entry(context->workspace, parent, &xname, &st, 0, 0, &error);

    /* entry created local and no error (no EEXIST!) */

    if (entry && error==0) {
	unsigned int pathlen=get_pathmax(context->workspace) + 1 + xname.len;
	char path[pathlen + 1];
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	struct fuse_path_s fpath;

	init_fuse_path(&fpath, path, pathlen);
	pathinfo.len=add_name_path(&fpath, &entry->name);

	lock_pathcalls(pathcalls);
	pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
	unlock_pathcalls(pathcalls);

	context=fpath.context;
	openfile->context=context;
	pathinfo.path=fpath.pathstart;

	logoutput("CREATE %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);

	openfile->inode=entry->inode; /* now it's pointing to the right inode */

	(* context->fs->create)(openfile, request, &pathinfo, &st, flags);

	if (openfile->error>0) {

	    remove_inode(&context->interface, openfile->inode);
	    openfile->inode=NULL;

	}

    } else {

	reply_VFS_error(request, error);

    }

}

/* RENAME */

static void service_fs_rename(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, struct inode_s *n_inode, const char *n_name, unsigned int flags)
{
    reply_VFS_error(request, ENOSYS);
}

/* OPENDIR */

static void service_fs_opendir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned int flags)
{
    struct service_context_s *context=opendir->context;
    unsigned int error=0;
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct pathcalls_s *pathcalls=NULL;
    struct directory_s *directory=NULL;
    unsigned int pathlen=get_pathmax(context->workspace);
    char path[pathlen + 1];
    struct fuse_path_s fpath;

    init_fuse_path(&fpath, path, pathlen);

    pathcalls=get_pathcalls(opendir->inode);
    directory=get_directory(opendir->inode);

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	opendir->error=ENOMEM;
	return;

    }

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    context=fpath.context;
    opendir->context=context;
    pathinfo.path=fpath.pathstart;

    logoutput("OPENDIR %s (thread %i) %s", context->name, (int) gettid(), pathinfo.path);

    (* context->fs->opendir)(opendir, request, &pathinfo, flags);

    lock_pathcalls(pathcalls);
    create_pathcache(pathcalls, &fpath, PATHCACHE_TYPE_TEMP);
    unlock_pathcalls(pathcalls);

}

static void service_fs_setxattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, const char *value, size_t size, int flags)
{
    unsigned int error=0;
    struct entry_s *entry=inode->alias;
    struct entry_s *parent=entry->parent;
    struct directory_s *directory=NULL;
    struct pathcalls_s *pathcalls=NULL;
    unsigned int pathlen=get_pathmax(context->workspace);
    char path[pathlen + 1];
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct fuse_path_s fpath;

    init_fuse_path(&fpath, path, pathlen);

    pathcalls=get_pathcalls(parent->inode);
    directory=get_directory(parent->inode);

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	return;

    }

    pathinfo.len=add_name_path(&fpath, &entry->name);

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    context=fpath.context;
    pathinfo.path=fpath.pathstart;

    (* context->fs->setxattr)(context, request, &pathinfo, inode, name, value, size, flags);
}

static void service_fs_getxattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, size_t size)
{
    unsigned int error=0;
    struct entry_s *entry=inode->alias;
    struct entry_s *parent=entry->parent;
    struct directory_s *directory=NULL;
    struct pathcalls_s *pathcalls=NULL;
    unsigned int pathlen=get_pathmax(context->workspace);
    char path[pathlen + 1];
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct fuse_path_s fpath;

    init_fuse_path(&fpath, path, pathlen);

    pathcalls=get_pathcalls(parent->inode);
    directory=get_directory(parent->inode);

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	return;

    }

    pathinfo.len=add_name_path(&fpath, &entry->name);

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    context=fpath.context;
    pathinfo.path=fpath.pathstart;

    logoutput("service_fs_getxattr");

    (* context->fs->getxattr)(context, request, &pathinfo, inode, name, size);
}

static void service_fs_listxattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, size_t size)
{
    unsigned int error=0;
    struct entry_s *entry=inode->alias;
    struct entry_s *parent=entry->parent;
    struct directory_s *directory=NULL;
    struct pathcalls_s *pathcalls=NULL;
    unsigned int pathlen=get_pathmax(context->workspace);
    char path[pathlen + 1];
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct fuse_path_s fpath;

    init_fuse_path(&fpath, path, pathlen);

    pathcalls=get_pathcalls(parent->inode);
    directory=get_directory(parent->inode);

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	return;

    }

    pathinfo.len=add_name_path(&fpath, &entry->name);

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    context=fpath.context;
    pathinfo.path=fpath.pathstart;

    (* context->fs->listxattr)(context, request, &pathinfo, inode, size);

}

static void service_fs_removexattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name)
{
    unsigned int error=0;
    struct entry_s *entry=inode->alias;
    struct entry_s *parent=entry->parent;
    struct directory_s *directory=NULL;
    struct pathcalls_s *pathcalls=NULL;
    unsigned int pathlen=get_pathmax(context->workspace);
    char path[pathlen + 1];
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct fuse_path_s fpath;

    init_fuse_path(&fpath, path, pathlen);

    pathcalls=get_pathcalls(parent->inode);
    directory=get_directory(parent->inode);

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	return;

    }

    pathinfo.len=add_name_path(&fpath, &entry->name);

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    context=fpath.context;
    pathinfo.path=fpath.pathstart;

    (* context->fs->removexattr)(context, request, &pathinfo, inode, name);
}


/* FSNOTIFY */

static void service_fs_fsnotify(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, uint32_t mask)
{
    unsigned int error=0;
    struct entry_s *entry=inode->alias;
    struct directory_s *directory=NULL;
    struct pathcalls_s *pathcalls=NULL;
    unsigned int pathlen=get_pathmax(context->workspace);
    char path[pathlen + 1];
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct fuse_path_s fpath;

    init_fuse_path(&fpath, path, pathlen);

    pathcalls=get_pathcalls(inode);
    directory=get_directory(inode);

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	return;

    }

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    pathinfo.path=fpath.pathstart;
    context=fpath.context;

    logoutput("FSNOTIFY %s (thread %i) %s mask %i", context->name, (int) gettid(), pathinfo.path, mask);

    (* context->fs->fsnotify)(context, request, &pathinfo, inode->st.st_ino, mask);

}

/* STATFS */

static void service_fs_statfs(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    unsigned int error=0;
    struct entry_s *entry=inode->alias;
    struct entry_s *parent=entry->parent;
    struct directory_s *directory=NULL;
    struct pathcalls_s *pathcalls=NULL;
    unsigned int pathlen=get_pathmax(context->workspace) + entry->name.len + 1;
    char path[pathlen + 1];
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct fuse_path_s fpath;

    init_fuse_path(&fpath, path, pathlen);

    if (S_ISDIR(inode->st.st_mode)) {

	pathcalls=get_pathcalls(inode);
	directory=get_directory(inode);

    } else {

	pathinfo.len=add_name_path(&fpath, &entry->name);

	pathcalls=get_pathcalls(parent->inode);
	directory=get_directory(parent->inode);

    }

    if (! directory) {

	reply_VFS_error(request, ENOMEM);
	return;

    }

    lock_pathcalls(pathcalls);
    pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
    unlock_pathcalls(pathcalls);

    pathinfo.path=fpath.pathstart;
    context=fpath.context;

    logoutput("STATFS %s (thread %i) %s", context->name, (int) gettid(), pathinfo.path);

    (* context->fs->statfs)(context, request, &pathinfo);

}

static void init_service_fs()
{
    struct fuse_fs_s *fs=NULL;

    /* NON DIRECTORY FS */

    fs=&service_nondir_fs;

    memset(fs, 0, sizeof(struct fuse_fs_s));

    fs->flags=FS_SERVICE_FLAG_NONDIR;
    set_virtual_fs(fs);

    fs->forget=service_fs_forget;
    fs->getattr=service_fs_getattr;
    fs->setattr=service_fs_setattr;

    fs->type.nondir.readlink=service_fs_readlink;

    fs->type.nondir.open=service_fs_open;
    fs->type.nondir.read=service_fs_read;
    fs->type.nondir.write=service_fs_write;
    fs->type.nondir.flush=service_fs_flush;
    fs->type.nondir.fsync=service_fs_fsync;
    fs->type.nondir.release=service_fs_release;

    fs->type.nondir.fgetattr=service_fs_fgetattr;
    fs->type.nondir.fsetattr=service_fs_fsetattr;

    fs->type.nondir.getlock=service_fs_getlock;
    fs->type.nondir.setlock=service_fs_setlock;
    fs->type.nondir.setlockw=service_fs_setlockw;

    fs->type.nondir.flock=service_fs_flock;

    fs->getxattr=service_fs_getxattr;
    fs->setxattr=service_fs_setxattr;
    fs->listxattr=service_fs_listxattr;
    fs->removexattr=service_fs_removexattr;

    fs->statfs=service_fs_statfs;

    /* DIRECTORY FS */

    fs=&service_dir_fs;

    memset(fs, 0, sizeof(struct fuse_fs_s));

    fs->flags=FS_SERVICE_FLAG_DIR;
    set_virtual_fs(fs);

    fs->forget=service_fs_forget;
    fs->getattr=service_fs_getattr;
    fs->setattr=service_fs_setattr;

    fs->type.dir.use_fs=use_service_fs;
    fs->type.dir.lookup=service_fs_lookup;

    fs->type.dir.create=service_fs_create;
    fs->type.dir.mkdir=service_fs_mkdir;
    fs->type.dir.mknod=service_fs_mknod;
    fs->type.dir.symlink=service_fs_symlink;

    fs->type.dir.unlink=service_fs_unlink;
    fs->type.dir.rmdir=service_fs_rmdir;

    fs->type.dir.rename=service_fs_rename;

    fs->type.dir.opendir=service_fs_opendir;
    fs->type.dir.readdir=service_fs_readdir;
    fs->type.dir.readdirplus=service_fs_readdirplus;
    fs->type.dir.releasedir=service_fs_releasedir;
    fs->type.dir.fsyncdir=service_fs_fsyncdir;

    fs->type.dir.fsnotify=service_fs_fsnotify;

    fs->getxattr=service_fs_getxattr;
    fs->setxattr=service_fs_setxattr;
    fs->listxattr=service_fs_listxattr;
    fs->removexattr=service_fs_removexattr;

    fs->statfs=service_fs_statfs;

}

void use_service_fs(struct service_context_s *context, struct inode_s *inode)
{

    pthread_mutex_lock(&done_mutex);

    if (done==0) {
	init_service_fs();
	done=1;
    }

    pthread_mutex_unlock(&done_mutex);

    if (S_ISDIR(inode->st.st_mode)) {

	inode->fs=&service_dir_fs;

    } else {

	inode->fs=&service_nondir_fs;

    }

}
