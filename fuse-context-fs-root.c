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
#include <fcntl.h>
#include <linux/fs.h>

#define LOGGING

#include "main.h"
#include "logging.h"
#include "pathinfo.h"
#include "beventloop.h"
#include "utils.h"

#include "fuse-dentry.h"
#include "fuse-directory.h"
#include "fuse-utils.h"
#include "fuse-fs.h"
#include "workspaces.h"
#include "workspace-context.h"
#include "workspace-interface.h"

#include "fuse-fs-common.h"
#include "fuse-fs-virtual.h"
#include "path-caching.h"
#include "fuse-context-fs-common.h"
#include "fuse-fs-special.h"

extern void use_service_fs(struct service_context_s *context, struct inode_s *inode);

static struct fuse_fs_s service_root_fs;
static unsigned char done=0;
static pthread_mutex_t done_mutex=PTHREAD_MUTEX_INITIALIZER;

static void service_fs_forget(struct inode_s *inode)
{
    struct inode_link_s *link=NULL;

    fs_get_inode_link(inode, &link);

    if (link->type==INODE_LINK_TYPE_CONTEXT) { /* must be true */
	struct service_context_s *context=(struct service_context_s *) link->link.ptr;
	struct workspace_mount_s *workspace=context->workspace;
	struct fuse_user_s *user=workspace->user;

	logoutput("FORGET context %s", context->name);

	(* context->interface.signal_interface)(&context->interface, "disconnect");
	(* context->interface.signal_interface)(&context->interface, "close");
	(* context->interface.signal_interface)(&context->interface, "free");
	remove_list_element(&context->list);
	if (context->parent) context->parent->refcount--;
	free_service_context(context);

	link->type=0;
	link->link.ptr=NULL;

    }

}

static struct service_context_s *get_context_root_inode(struct inode_s *inode)
{
    struct inode_link_s *link=NULL;
    fs_get_inode_link(inode, &link);
    return (link->type==INODE_LINK_TYPE_CONTEXT) ? ((struct service_context_s *) (link->link.ptr)) : NULL;
}

/* LOOKUP */

static void service_fs_lookup(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len)
{
    unsigned int error=0;
    struct name_s xname={(char *)name, len, 0};
    struct entry_s *entry=NULL;
    unsigned int pathlen=xname.len + 1;
    char path[pathlen + 1];
    char *pathstart=NULL;
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    struct directory_s *directory=NULL;

    pathstart=path+pathlen;
    *pathstart='\0';

    pathstart-=xname.len;
    memcpy(pathstart, xname.name, xname.len);
    pathstart--;
    *pathstart='/';
    pathinfo.len=xname.len + 1;

    directory=get_directory(pinode, &error);

    context=get_context_root_inode(pinode);
    pathinfo.path=pathstart;
    calculate_nameindex(&xname);

    entry=find_entry(directory, &xname, &error);

    if (entry) {

	logoutput("service_fs_lookup: root context %s (thread %i) %s (entry found)", context->name, (int) gettid(), pathinfo.path);

	if (check_entry_special(entry->inode)==0) {

	    _fs_common_cached_lookup(context, request, entry->inode);

	} else {
	    struct service_fs_s *fs=context->service.filesystem.fs;

	    (* fs->lookup_existing)(context, request, entry, &pathinfo);

	}

    } else {
	struct service_fs_s *fs=context->service.filesystem.fs;

	logoutput("service_fs_lookup: root context %s (thread %i) %s (entry not found)", context->name, (int) gettid(), pathinfo.path);

	(* fs->lookup_new)(context, request, pinode, &xname, &pathinfo);

    }

}

/* GETATTR */

static void service_fs_getattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    unsigned int pathlen=2;
    char path[pathlen + 1];
    char *pathstart=NULL;
    union datalink_u *link=NULL;

    logoutput("service_fs_getattr: context root %s (thread %i)", context->name, (int) gettid());

    pathstart=path+pathlen;
    *pathstart='\0';

    pathstart--;
    *pathstart='.';
    pathstart--;
    *pathstart='/';
    pathinfo.len=2;

    context=get_context_root_inode(inode);
    pathinfo.path=pathstart;

    if (context) {
	struct service_fs_s *fs=context->service.filesystem.fs;

	logoutput("service_fs_getattr: context root %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);
	(* fs->getattr)(context, request, inode, &pathinfo);
	return;

    }

    reply_VFS_error(request, EIO);


}

static void service_fs_setattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, struct stat *st, unsigned int set)
{
    struct entry_s *entry=inode->alias;
    struct entry_s *parent=entry->parent;
    unsigned int pathlen=2;
    char path[pathlen + 1];
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    unsigned int error=0;
    char *pathstart=NULL;
    union datalink_u *link=NULL;

    /* access */

    if (request->uid!=context->workspace->user->uid) {

	reply_VFS_error(request, EACCES);
	return;

    }

    pathstart=path+pathlen;
    *pathstart='\0';

    pathstart--;
    *pathstart='.';
    pathstart--;
    *pathstart='/';
    pathinfo.len=2;

    context=get_context_root_inode(inode);
    pathinfo.path=pathstart;

    if (context) {
	struct service_fs_s *fs=context->service.filesystem.fs;

	logoutput("service_fs_setattr: context root %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);
	(* fs->setattr)(context, request, inode, &pathinfo, st, set);
	return;

    }

    reply_VFS_error(request, EIO);
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
    directory=get_directory(pinode, &error);
    entry=_fs_common_create_entry_unlocked(context->workspace, directory, &xname, &st, 0, 0, &error);

    /* entry created local and no error (no EEXIST!) */

    if (entry && error==0) {
	unsigned int pathlen=1 + xname.len;
	char path[pathlen + 1];
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	char *pathstart=NULL;
	union datalink_u *link=NULL;

	pathstart=path+pathlen;
	*pathstart='\0';

	pathstart-=xname.len;
	memcpy(pathstart, xname.name, xname.len);
	pathstart--;
	*pathstart='/';
	pathinfo.len=xname.len + 1;

	context=get_context_root_inode(pinode);
	pathinfo.path=pathstart;

	if (context) {
	    struct service_fs_s *fs=context->service.filesystem.fs;

	    logoutput("service_fs_mkdir: context root %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);
	    (* fs->mkdir)(context, request, entry, &pathinfo, &st);
	    return;

	}

	error=EIO;
	remove_entry(directory, entry, &error);
	destroy_entry(entry);

    }

    reply_VFS_error(request, error);

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
    directory=get_directory(pinode, &error);
    entry=_fs_common_create_entry_unlocked(context->workspace, directory, &xname, &st, 0, 0, &error);

    /* entry created local and no error (no EEXIST!) */

    if (entry && error==0) {
	struct pathcalls_s *pathcalls=NULL;
	struct directory_s *directory=NULL;
	unsigned int pathlen=1 + xname.len;
	char path[pathlen + 1];
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	char *pathstart=NULL;
	union datalink_u *link=NULL;

	pathstart=path+pathlen;
	*pathstart='\0';

	pathstart-=xname.len;
	memcpy(pathstart, xname.name, xname.len);
	pathstart--;
	*pathstart='/';
	pathinfo.len=xname.len + 1;

	context=get_context_root_inode(pinode);
	pathinfo.path=pathstart;

	if (context) {
	    struct service_fs_s *fs=context->service.filesystem.fs;

	    logoutput("service_fs_mknod: context root %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);
	    (* fs->mknod)(context, request, entry, &pathinfo, &st);
	    return;

	}

	remove_entry(directory, entry, &error);
	destroy_entry(entry);

	error=EIO;

    }

    reply_VFS_error(request, error);

}

/* SYMLINK */

static void service_fs_symlink(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len0, const char *target, unsigned int len1)
{
    unsigned int error=0;
    struct name_s xname={(char *)name, len0, 0};
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
    directory=get_directory(pinode, &error);
    entry=_fs_common_create_entry_unlocked(context->workspace, directory, &xname, &st, 0, 0, &error);

    /* entry created local and no error (no EEXIST!) */

    if (entry && error==0) {
	struct pathcalls_s *pathcalls=NULL;
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	unsigned int pathlen=get_pathmax(context->workspace) + xname.len + 1;
	char path[pathlen + 1];
	char *pathstart=NULL;
	char *remote_target=NULL;
	union datalink_u *link=NULL;

	pathstart=path+pathlen;
	*pathstart='\0';

	pathstart-=xname.len;
	memcpy(pathstart, xname.name, xname.len);
	pathstart--;
	*pathstart='/';
	pathinfo.len=xname.len + 1;

	context=get_context_root_inode(pinode);
	pathinfo.path=pathstart;

	if (context) {
	    struct service_fs_s *fs=context->service.filesystem.fs;
	    struct inode_s *inode=entry->inode;

	    logoutput("service_fs_symlink: context root %s (thread %i) %s to %s", context->name, (int) gettid(), pathstart, target);

	    if ((* fs->symlink_validate)(context, &pathinfo, (char *)target, &remote_target)==0) {

		(* fs->symlink)(context, request, entry, &pathinfo, remote_target);
		free(remote_target);
		return;

	    }

	    error=EPERM;
	    queue_inode_2forget(context->unique, inode->st.st_ino, 0, 0);

	}

	remove_entry(directory, entry, &error);
	destroy_entry(entry);

    }

    reply_VFS_error(request, error);

}


/* REMOVE/UNLINK */

static void service_fs_unlink(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len)
{
    unsigned int error=ENOENT;
    struct name_s xname={(char *)name, len, 0};
    struct entry_s *entry=NULL;
    struct directory_s *directory=NULL;

    calculate_nameindex(&xname);
    directory=get_directory(pinode, &error);
    entry=find_entry(directory, &xname, &error);

    if (entry) {
	struct pathcalls_s *pathcalls=NULL;
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	unsigned int pathlen=xname.len + 1;
	char path[pathlen + 1];
	char *pathstart=NULL;
	union datalink_u *link=NULL;

	pathstart=path+pathlen;
	*pathstart='\0';

	pathstart-=xname.len;
	memcpy(pathstart, xname.name, xname.len);
	pathstart--;
	*pathstart='/';
	pathinfo.len=xname.len + 1;

	context=get_context_root_inode(pinode);
	pathinfo.path=pathstart;

	if (context) {
	    struct service_fs_s *fs=context->service.filesystem.fs;

	    logoutput("service_fs_unlink: context root %s (thread %i) %s", context->name, (int) gettid(), pathstart);
	    (* fs->unlink)(context, request, &entry, &pathinfo);
	    return;

	}

	error=EIO;

    }

    reply_VFS_error(request, error);

}

/* RMDIR */

static void service_fs_rmdir(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len)
{
    unsigned int error=ENOENT;
    struct name_s xname={(char *)name, len, 0};
    struct entry_s *entry=NULL;
    struct directory_s *directory=NULL;

    calculate_nameindex(&xname);
    directory=get_directory(pinode, &error);
    entry=find_entry(directory, &xname, &error);

    if (entry) {
	struct pathcalls_s *pathcalls=NULL;
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	unsigned int pathlen=xname.len + 1;
	char path[pathlen + 1];
	struct directory_s *sub_directory=NULL;
	char *pathstart=NULL;
	union datalink_u *link=NULL;

	sub_directory=get_directory(entry->inode, &error);

	if (sub_directory && sub_directory->count>0) {

	    /* directory is not empty on this host (?) */

	    reply_VFS_error(request, ENOTEMPTY);
	    return;

	}

	pathstart=path+pathlen;
	*pathstart='\0';

	pathstart-=xname.len;
	memcpy(pathstart, xname.name, xname.len);
	pathstart--;
	*pathstart='/';
	pathinfo.len=xname.len + 1;

	context=get_context_root_inode(pinode);
	pathinfo.path=pathstart;

	if (context) {
	    struct service_fs_s *fs=context->service.filesystem.fs;

	    logoutput("service_fs_rmdir: context root %s (thread %i) %s", context->name, (int) gettid(), pathinfo.path);

	    (* fs->rmdir)(context, request, &entry, &pathinfo);
	    if (entry==NULL && sub_directory) destroy_directory(sub_directory);
	    return;

	}

	error=EIO;

    }

    reply_VFS_error(request, error);

}

/* RENAME */

static void service_fs_rename(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, struct inode_s *n_inode, const char *n_name, unsigned int flags)
{
    reply_VFS_error(request, ENOSYS);
}

static void service_fs_rename_keep(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, struct inode_s *n_inode, const char *n_name, unsigned int flags)
{
    unsigned int error=ENOENT;
    struct name_s xname={(char *)name, strlen(name), 0};
    struct entry_s *entry=NULL;
    struct directory_s *directory=NULL;

    if (flags & RENAME_WHITEOUT) {

	reply_VFS_error(request, EINVAL);
	return;

    }

    calculate_nameindex(&xname);
    directory=get_directory(inode, &error);
    entry=find_entry(directory, &xname, &error);

    if (entry) {
	struct pathcalls_s *pathcalls=NULL;
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	unsigned int pathlen=xname.len + 1;
	char path[pathlen + 1];
	struct directory_s *sub_directory=NULL, *n_directory=NULL;
	char *pathstart=NULL;
	struct name_s n_xname={(char *)n_name, strlen(n_name), 0};
	struct entry_s *n_entry=NULL;
	union datalink_u *link=NULL;

	pathstart=path+pathlen;
	*pathstart='\0';

	pathstart-=xname.len;
	memcpy(pathstart, xname.name, xname.len);
	pathstart--;
	*pathstart='/';
	pathinfo.len=xname.len + 1;

	context=get_context_root_inode(inode);
	pathinfo.path=pathstart;

	calculate_nameindex(&n_xname);
	n_directory=get_directory(n_inode, &error);
	n_entry=find_entry(n_directory, &n_xname, &error);

	if (n_entry && (flags & RENAME_NOREPLACE)) {

	    reply_VFS_error(request, EEXIST);
	    return;

	} else if (! n_entry && (flags & RENAME_EXCHANGE)) {

	    reply_VFS_error(request, ENOENT);
	    return;

	} else {
	    struct pathcalls_s *pathcalls=NULL;
	    unsigned int n_pathlen=get_pathmax(context->workspace);
	    char n_path[n_pathlen + 1];
	    struct pathinfo_s n_pathinfo=PATHINFO_INIT;
	    struct fuse_path_s fpath;
	    struct service_fs_s *fs=NULL;

	    init_fuse_path(&fpath, n_path, n_pathlen);

	    if (S_ISDIR(n_entry->inode->st.st_mode)) {

		directory=get_directory(n_entry->inode, &error);

	    } else {

		pathinfo.len=add_name_path(&fpath, &n_entry->name);
		directory=get_directory(n_entry->parent->inode, &error);

	    }

	    if (! directory) {

		reply_VFS_error(request, ENOMEM);
		return;

	    }

	    pathcalls=get_pathcalls(directory);
	    lock_pathcalls(pathcalls);
	    n_pathinfo.len+=(* pathcalls->get_path)(directory, &fpath);
	    unlock_pathcalls(pathcalls);

	    if (fpath.context != context) {

		reply_VFS_error(request, EXDEV);
		return;

	    }

	    context=fpath.context;
	    fs=context->service.filesystem.fs;
	    n_pathinfo.path=fpath.pathstart;
	    logoutput("service_fs_rename_keep: context root %s (thread %i): %s to %s", context->name, (int) gettid(), pathinfo.path, n_pathinfo.path);

	    (* context->service.filesystem.fs->rename)(context, request, &entry, &pathinfo, &n_entry, &n_pathinfo, flags);

	}

    } else {

	reply_VFS_error(request, ENOENT);

    }

}

/* LINK */

static void service_fs_link(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, struct inode_s *l_inode, const char *l_name)
{
    reply_VFS_error(request, ENOSYS);
}

/* CREATE */

static void service_fs_create(struct fuse_openfile_s *openfile, struct fuse_request_s *request, const char *name, unsigned int len, unsigned int flags, mode_t mode, mode_t mask)
{
    struct service_context_s *context=openfile->context;
    unsigned int error=0;
    struct name_s xname={(char *)name, len-1, 0};
    mode_t filetype=(mode & S_IFMT);
    mode_t fileperm=0;
    struct entry_s *entry=NULL;
    struct stat st;
    struct directory_s *directory=NULL;

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
    directory=get_directory(openfile->inode, &error); /* at this moment this is the parent, the entry has to be created */
    entry=_fs_common_create_entry_unlocked(context->workspace, directory, &xname, &st, 0, 0, &error);

    /* entry created local and no error (no EEXIST!) */

    if (entry && error==0) {
	unsigned int pathlen=1 + xname.len;
	char path[pathlen + 1];
	struct pathinfo_s pathinfo=PATHINFO_INIT;
	char *pathstart=NULL;
	union datalink_u *link=NULL;

	pathstart=path+pathlen;
	*pathstart='\0';

	pathstart-=xname.len;
	memcpy(pathstart, xname.name, xname.len);
	pathstart--;
	*pathstart='/';
	pathinfo.len=xname.len + 1;

	context=get_context_root_inode(openfile->inode);
	pathinfo.path=pathstart;

	if (context) {
	    struct service_fs_s *fs=context->service.filesystem.fs;

	    logoutput("service_fs_create: context root %s (thread %i): %s", context->name, (int) gettid(), pathinfo.path);
	    openfile->context=context;
	    openfile->inode=entry->inode; /* now it's pointing to the right inode */

	    (* fs->create)(openfile, request, &pathinfo, &st, flags);

	    if (openfile->error>0) {
		struct inode_s *inode=openfile->inode;

		queue_inode_2forget(context->unique, inode->st.st_ino, 0, 0);
		openfile->inode=NULL;

	    }

	    return;

	}

	error=EIO;

    }

    reply_VFS_error(request, error);

}

/* OPENDIR */

static void service_fs_opendir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned int flags)
{
    struct service_context_s *context=NULL;
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    unsigned int pathlen=2;
    char path[pathlen + 1];
    char *pathstart=NULL;
    union datalink_u *link=NULL;

    logoutput("service_fs_opendir: context root %s ino %li", opendir->context->name, opendir->inode->st.st_ino);

    pathstart=path+pathlen;
    *pathstart='\0';

    pathstart--;
    *pathstart='.';
    pathstart--;
    *pathstart='/';
    pathinfo.len=2;
    pathinfo.path=pathstart;

    context=get_context_root_inode(opendir->inode);

    if (context) {
	struct service_fs_s *fs=context->service.filesystem.fs;

	opendir->context=context;
	logoutput("OPENDIR root %s (thread %i) %s", context->name, (int) gettid(), pathinfo.path);
	(* fs->opendir)(opendir, request, &pathinfo, flags);
	return;

    }

    reply_VFS_error(request, EIO);

}

/* FSNOTIFY */

void service_fs_fsnotify(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, uint32_t mask)
{
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    unsigned int pathlen=2;
    char path[pathlen + 1];
    char *pathstart=NULL;
    union datalink_u *link=NULL;

    pathstart=path+pathlen;
    *pathstart='\0';

    pathstart--;
    *pathstart='.';
    pathstart--;
    *pathstart='/';
    pathinfo.len=2;

    context=get_context_root_inode(inode);
    pathinfo.path=pathstart;

    if (context) {
	struct service_fs_s *fs=context->service.filesystem.fs;

	logoutput("service_fs_fsnotify: context root %s (thread %i) %s mask %i", context->name, (int) gettid(), pathinfo.path, mask);
	(* fs->fsnotify)(context, request, &pathinfo, inode->st.st_ino, mask);

    }

}

/* STATFS */

static void service_fs_statfs(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    unsigned int pathlen=1;
    char path[pathlen + 1];
    char *pathstart=NULL;
    union datalink_u *link=NULL;

    pathstart=path+pathlen;
    *pathstart='\0';

    pathstart--;
    //*pathstart='.';
    //pathstart--;
    *pathstart='/';
    pathinfo.len=1;

    context=get_context_root_inode(inode);
    pathinfo.path=pathstart;

    if (context) {
	struct service_fs_s *fs=context->service.filesystem.fs;

	logoutput("service_fs_statfs: context root %s (thread %i) %s", context->name, (int) gettid(), pathinfo.path);
	(* fs->statfs)(context, request, &pathinfo);
	return;

    }

    reply_VFS_error(request, EIO);

}

/*
    set the context fs calls for the root of the service
    note:
    - open, lock and fattr calls are not used
    - path resolution is simple, its / or /%name% for lookup
*/

static void init_service_root_fs()
{
    struct fuse_fs_s *fs=&service_root_fs;

    memset(fs, 0, sizeof(struct fuse_fs_s));

    fs->flags=FS_SERVICE_FLAG_ROOT | FS_SERVICE_FLAG_DIR;
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

    // fs->type.dir.fsnotify=service_fs_fsnotify;

    fs->statfs=service_fs_statfs;

}

void use_service_root_fs(struct inode_s *inode)
{

    pthread_mutex_lock(&done_mutex);

    if (done==0) {
	init_service_root_fs();
	done=1;
    }

    pthread_mutex_unlock(&done_mutex);

    if (S_ISDIR(inode->st.st_mode)) {

	inode->fs=&service_root_fs;

    }

}
