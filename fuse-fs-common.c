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
#include <stdint.h>
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
#include <sys/fsuid.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "logging.h"

#include "pathinfo.h"
#include "beventloop.h"

#include "entry-management.h"
#include "directory-management.h"
#include "entry-utils.h"

#include "utils.h"
#include "options.h"

#include "fuse-fs.h"
#include "workspaces.h"
#include "workspace-context.h"

#include "fuse-fs-common.h"

#define UINT32_T_MAX		0xFFFFFFFF

const char *rootpath="/";
const char *dotdotname="..";
const char *dotname=".";

/*
    check an user has access to a path
    do this by changing the fs uid/gid to those of the user,
    and test access using the access call
    note that this is not encouraged by the manpage of access (man 2 access)
    cause it leaves a security hole
*/

int check_access_path(uid_t uid, gid_t gid, struct pathinfo_s *target, unsigned int *error)
{
    uid_t uid_keep=setfsuid(uid);
    gid_t gid_keep=setfsgid(gid);
    struct stat st;
    int accessok=-1;

    logoutput_info("check access path: %s", target->path);

    if (stat(target->path, &st)==0 && S_ISDIR(st.st_mode) && access(target->path, R_OK | X_OK)==0) {

	accessok=0;

    } else {

	*error=errno;

    }

    uid_keep=setfsuid(uid_keep);
    gid_keep=setfsgid(gid_keep);

    return accessok;

}

struct entry_s *get_target_symlink(struct workspace_mount_s *mount, struct entry_s *parent, const char *path)
{
    struct entry_s *entry=NULL;

    if (strncmp(path, "/", 1)==0) {
	unsigned int len=mount->mountpoint.len;

	if (strlen(path) > len) {

	    logoutput("get_target_symlink: test absolute path %s", path);

	    if (strncmp(mount->mountpoint.path, path, len)==0 && *(path+len)=='/') {
		char *subpath=path + len + 1;
		char buffer[strlen(subpath)+1];

		strcpy(buffer, subpath);

		entry=walk_fuse_fs(mount->rootinode.alias, buffer);

	    }

	}

    } else {
	char *pos=path;

	/*
	    relative
	    first strip the leading ..
	*/

	while(strncmp(pos, "../", 3)==0 && parent) {

	    pos=pos+3;
	    parent=parent->parent;

	}

	if (parent) {
	    unsigned int len=strlen(pos);

	    if (len>0) {
		char buffer[len+1];

		strcpy(buffer, pos);

		entry=walk_fuse_fs(parent, buffer);

	    }

	}

    }

    return entry;

}

/*
    provides stat to lookup when entry already exists (is cached)
*/

void _fs_common_cached_lookup(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    struct fuse_entry_out entry_out;
    struct timespec *attr_timeout=get_fuse_interface_attr_timeout(context->interface.ptr);
    struct timespec *entry_timeout=get_fuse_interface_entry_timeout(context->interface.ptr);

    inode->nlookup++;

    entry_out.nodeid=inode->ino;
    entry_out.generation=0; /* todo: add a generation field to reuse existing inodes */

    entry_out.entry_valid=entry_timeout->tv_sec;
    entry_out.entry_valid_nsec=entry_timeout->tv_nsec;

    entry_out.attr_valid=attr_timeout->tv_sec;
    entry_out.attr_valid_nsec=attr_timeout->tv_nsec;

    entry_out.attr.ino=inode->ino;
    entry_out.attr.size=inode->size;

    entry_out.attr.blksize=_DEFAULT_BLOCKSIZE;
    entry_out.attr.blocks=inode->size / _DEFAULT_BLOCKSIZE + (inode->size % _DEFAULT_BLOCKSIZE == 0) ? 0 : 1;

    entry_out.attr.atime=(uint64_t) inode->atim.tv_sec;
    entry_out.attr.atimensec=(uint32_t) inode->atim.tv_nsec;

    entry_out.attr.mtime=(uint64_t) inode->mtim.tv_sec;
    entry_out.attr.mtimensec=(uint32_t) inode->mtim.tv_nsec;

    entry_out.attr.ctime=(uint64_t) inode->ctim.tv_sec;
    entry_out.attr.ctimensec=(uint32_t) inode->ctim.tv_nsec;

    entry_out.attr.mode=inode->mode;
    entry_out.attr.nlink=inode->nlink;
    entry_out.attr.uid=inode->uid;
    entry_out.attr.gid=inode->gid;
    entry_out.attr.rdev=0; /* no special devices supported */

    entry_out.attr.padding=0;

    reply_VFS_data(request, (char *) &entry_out, sizeof(entry_out));

}

void _fs_common_cached_create(struct service_context_s *context, struct fuse_request_s *request, struct fuse_openfile_s *openfile)
{
    struct inode_s *inode=openfile->inode;
    struct fuse_entry_out entry_out;
    struct fuse_open_out open_out;
    unsigned int size_entry_out=sizeof(struct fuse_entry_out);
    unsigned int size_open_out=sizeof(struct fuse_open_out);
    struct timespec *attr_timeout=get_fuse_interface_attr_timeout(context->interface.ptr);
    struct timespec *entry_timeout=get_fuse_interface_entry_timeout(context->interface.ptr);
    char buffer[size_entry_out + size_open_out];

    // inode->nlookup++;

    entry_out.nodeid=inode->ino;
    entry_out.generation=0; /* todo: add a generation field to reuse existing inodes */

    entry_out.entry_valid=entry_timeout->tv_sec;
    entry_out.entry_valid_nsec=entry_timeout->tv_nsec;

    entry_out.attr_valid=attr_timeout->tv_sec;
    entry_out.attr_valid_nsec=attr_timeout->tv_nsec;

    entry_out.attr.ino=inode->ino;
    entry_out.attr.size=inode->size;

    entry_out.attr.blksize=_DEFAULT_BLOCKSIZE;
    entry_out.attr.blocks=inode->size / _DEFAULT_BLOCKSIZE + (inode->size % _DEFAULT_BLOCKSIZE == 0) ? 0 : 1;

    entry_out.attr.atime=(uint64_t) inode->atim.tv_sec;
    entry_out.attr.atimensec=(uint32_t) inode->atim.tv_nsec;

    entry_out.attr.mtime=(uint64_t) inode->mtim.tv_sec;
    entry_out.attr.mtimensec=(uint32_t) inode->mtim.tv_nsec;

    entry_out.attr.ctime=(uint64_t) inode->ctim.tv_sec;
    entry_out.attr.ctimensec=(uint32_t) inode->ctim.tv_nsec;

    entry_out.attr.mode=inode->mode;
    entry_out.attr.nlink=inode->nlink;
    entry_out.attr.uid=inode->uid;
    entry_out.attr.gid=inode->gid;
    entry_out.attr.rdev=0; /* no special devices supported */

    entry_out.attr.padding=0;

    open_out.fh=(uint64_t) openfile;
    open_out.open_flags=FOPEN_KEEP_CACHE;
    open_out.padding=0;

    /* put entry_out and open_out in one buffer */

    memcpy(buffer, &entry_out, size_entry_out);
    memcpy(buffer+size_entry_out, &open_out, size_open_out);

    reply_VFS_data(request, buffer, size_entry_out + size_open_out);

}

void _fs_common_virtual_lookup(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, const char *name, unsigned int len)
{
    struct entry_s *parent=inode->alias, *entry=NULL;
    struct name_s xname={NULL, 0, 0};
    unsigned int error=0;

    logoutput("_fs_common_virtual_lookup: name %s, parent %li:%s (thread %i)", name, (long) inode->ino, parent->name.name, (int) gettid());

    xname.name=name;
    xname.len=len-1;
    calculate_nameindex(&xname);

    entry=find_entry(parent, &xname, &error);

    if (entry && (entry->inode->nlookup>0)) {

	/* it's possible that the entry represents the root of a service
	    in that case do a lookup of the '.' on the root of the service using the service specific fs calls
	*/

	if ((* entry->inode->fs->get_count)()==1) {
	    union datalink_u *link=get_datalink(entry->inode);
	    struct service_context_s *service_context=(struct service_context_s *) link->data;

	    if (service_context) {
		struct pathinfo_s pathinfo=PATHINFO_INIT;
		unsigned int pathlen=2;
		char path[3];

		path[2]='\0';
		path[1]='.';
		path[0]='/';

		pathinfo.len=2;
		pathinfo.path=path;

		(* service_context->fs->lookup_existing)(service_context, request, entry, &pathinfo);
		return;

	    }

	}

	_fs_common_cached_lookup(context, request, entry->inode);

    } else {

	reply_VFS_error(request, ENOENT);

    }

}

/*
    common function to get the attributes
    NOTO: dealing with a read-only filesystem, so do not care about refreshing, just get out of the cache
*/

void _fs_common_getattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    struct fuse_attr_out attr_out;
    struct timespec *attr_timeout=get_fuse_interface_attr_timeout(context->interface.ptr);

    attr_out.attr_valid=attr_timeout->tv_sec;
    attr_out.attr_valid_nsec=attr_timeout->tv_nsec;

    attr_out.attr.ino=inode->ino;
    attr_out.attr.size=inode->size;

    attr_out.attr.blksize=_DEFAULT_BLOCKSIZE;
    attr_out.attr.blocks=inode->size / _DEFAULT_BLOCKSIZE + (inode->size % _DEFAULT_BLOCKSIZE == 0) ? 0 : 1;

    attr_out.attr.atime=(uint64_t) inode->atim.tv_sec;
    attr_out.attr.atimensec=(uint32_t) inode->atim.tv_nsec;

    attr_out.attr.mtime=(uint64_t) inode->mtim.tv_sec;
    attr_out.attr.mtimensec=(uint32_t) inode->mtim.tv_nsec;

    attr_out.attr.ctime=(uint64_t) inode->ctim.tv_sec;
    attr_out.attr.ctimensec=(uint32_t) inode->ctim.tv_nsec;

    attr_out.attr.mode=inode->mode;
    attr_out.attr.nlink=inode->nlink;
    attr_out.attr.uid=inode->uid;
    attr_out.attr.gid=inode->gid;
    attr_out.attr.rdev=0; /* no special devices supported */

    attr_out.attr.padding=0;

    attr_out.dummy=0;

    reply_VFS_data(request, (char *) &attr_out, sizeof(attr_out));

}

void _fs_common_virtual_opendir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned int flags)
{
    unsigned int error=0;
    struct directory_s *directory=get_directory(opendir->inode);
    struct fuse_open_out open_out;

    logoutput("OPENDIR virtual (thread %i)", (int) gettid());

    if (directory) {

	if (directory->count>0) opendir->mode |= _FUSE_READDIR_MODE_NONEMPTY;

    }

    open_out.fh=(uint64_t) opendir;
    open_out.open_flags=0;
    open_out.padding=0;

    reply_VFS_data(request, (char *) &open_out, sizeof(open_out));

}

void _fs_common_virtual_readdir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset)
{

    if (opendir->mode & _FUSE_READDIR_MODE_FINISH) {
	char dummy='\0';

	logoutput("READDIR virtual (thread %i) finish", (int) gettid());
	reply_VFS_data(request, &dummy, 0);

    } else {
	struct stat st;
	size_t pos=0, dirent_size=0;
	struct directory_s *directory=NULL;
	struct name_s xname={NULL, 0, 0};
	struct inode_s *inode=NULL;
	struct entry_s *entry=NULL;
	char buff[size];
	unsigned int error=0;
	struct simple_lock_s rlock;

	logoutput("READDIR virtual (thread %i)", (int) gettid());

	if (rlock_directory(opendir->inode, &rlock)==0) {

	    directory=get_directory(opendir->inode);
	    if (offset==0) opendir->handle.ptr=(void *) directory->first;

	} else {

	    reply_VFS_error(request, EAGAIN);
	    return;

	}

	memset(&st, 0, sizeof(struct stat));

	while (pos<size) {

	    if (offset==0) {

		inode=opendir->inode;

    		/* the . entry */

    		st.st_ino = inode->ino;
		st.st_mode = S_IFDIR;
		xname.name = (char *) dotname;
		xname.len=1;

    	    } else if (offset==1) {
    		struct entry_s *parent=NULL;

		inode=opendir->inode;

		/* the .. entry */

		parent=inode->alias;
		if (parent->parent) inode=parent->parent->inode;

    		st.st_ino = inode->ino;
		st.st_mode = S_IFDIR;
		xname.name = (char *) dotdotname;
		xname.len=2;

    	    } else {

		if (! opendir->entry) {

		    readdir:

		    entry=(struct entry_s *) opendir->handle.ptr;

		    if (entry) {

			opendir->handle.ptr=(void *) entry->name_next;

		    } else {

			opendir->mode |= _FUSE_READDIR_MODE_FINISH;
			break;

		    }

		} else {

		    entry=opendir->entry;

		}

		inode=entry->inode;

		st.st_ino=inode->ino;
		st.st_mode=inode->mode;
		xname.name=entry->name.name;
		xname.len=entry->name.len;

	    }

	    dirent_size=add_direntry_buffer(buff + pos, size - pos, offset + 1, &xname, &st, &error);

	    if (error==ENOBUFS) {

		opendir->entry=entry; /* keep it for the next batch */
		break;

	    }

	    /* increase counter and clear the various fields */

	    opendir->entry=NULL; /* forget current entry to force readdir */
	    offset++;
	    pos+=dirent_size;

	}

	reply_VFS_data(request, buff, pos);
	unlock_directory(opendir->inode, &rlock);

    }

}

/* TODO not ready yet */

void _fs_common_virtual_readdirplus(struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset)
{

    if (opendir->mode & _FUSE_READDIR_MODE_FINISH) {
	char dummy='\0';

	logoutput("READDIRPLUS virtual (thread %i) finish", (int) gettid());
	reply_VFS_data(request, &dummy, 0);

    } else {
	struct stat st;
	size_t pos=0, dirent_size=0;
	struct directory_s *directory=NULL;
	struct name_s xname={NULL, 0, 0};
	struct inode_s *inode=NULL;
	struct entry_s *entry=NULL;
	char buff[size];
	unsigned int error=0;
	struct simple_lock_s rlock;

	logoutput("READDIRPLUS virtual (thread %i)", (int) gettid());

	if (rlock_directory(opendir->inode, &rlock)==0) {

	    directory=get_directory(opendir->inode);
	    if (offset==0) opendir->handle.ptr=(void *) directory->first;

	} else {

	    reply_VFS_error(request, EAGAIN);
	    return;

	}

	memset(&st, 0, sizeof(struct stat));

	while (pos<size) {

	    if (offset==0) {

		inode=opendir->inode;

    		/* the . entry */

    		st.st_ino = inode->ino;
		st.st_mode = S_IFDIR;
		xname.name = (char *) dotname;
		xname.len=1;

    	    } else if (offset==1) {
    		struct entry_s *parent=NULL;

		inode=opendir->inode;

		/* the .. entry */

		parent=inode->alias;
		if (parent->parent) inode=parent->parent->inode;

    		st.st_ino = inode->ino;
		st.st_mode = S_IFDIR;
		xname.name = (char *) dotdotname;
		xname.len=2;

    	    } else {

		if (! opendir->entry) {

		    readdir:

		    entry=(struct entry_s *) opendir->handle.ptr;

		    if (entry) {

			opendir->handle.ptr=(void *) entry->name_next;

		    } else {

			opendir->mode |= _FUSE_READDIR_MODE_FINISH;
			break;

		    }

		} else {

		    entry=opendir->entry;

		}

		inode=entry->inode;

		st.st_ino=inode->ino;
		st.st_mode=inode->mode;
		xname.name=entry->name.name;
		xname.len=entry->name.len;

	    }

	    dirent_size=add_direntry_buffer(buff + pos, size - pos, offset + 1, &xname, &st, &error);

	    if (error==ENOBUFS) {

		opendir->entry=entry; /* keep it for the next batch */
		break;

	    }

	    /* increase counter and clear the various fields */

	    opendir->entry=NULL; /* forget current entry to force readdir */
	    offset++;
	    pos+=dirent_size;

	}

	reply_VFS_data(request, buff, pos);
	unlock_directory(opendir->inode, &rlock);

    }

}

void _fs_common_virtual_releasedir(struct fuse_opendir_s *opendir, struct fuse_request_s *request)
{

    logoutput("RELEASEDIR virtual (thread %i)", (int) gettid());
    reply_VFS_error(request, 0);

}

void _fs_common_virtual_fsyncdir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned char datasync)
{
    reply_VFS_error(request, 0);
}

/*
    common functions to create
*/

struct _fs_common_create_struct {
    struct workspace_mount_s			*workspace;
    struct stat 				*st;
    unsigned char				mayexist;
    unsigned int 				*error;
};

static void _fs_common_create_cb_created(struct entry_s *entry, void *data)
{
    struct _fs_common_create_struct *_create_common=(struct _fs_common_create_struct *) data;
    struct inode_s *inode=entry->inode;

    logoutput("_fs_common_create_cb_created: %s", entry->name.name);

    fill_inode_stat(inode, _create_common->st);

    memcpy(&inode->ctim, &_create_common->st->st_ctim, sizeof(struct timespec));
    memcpy(&inode->mtim, &_create_common->st->st_mtim, sizeof(struct timespec));
    memcpy(&inode->atim, &_create_common->st->st_atim, sizeof(struct timespec));
    inode->mode=_create_common->st->st_mode;
    inode->nlookup=1;
    inode->nlink=1;
    inode->size=_create_common->st->st_size;

    add_inode_context(_create_common->workspace->context, inode);

    if (S_ISDIR(_create_common->st->st_mode)) {

	inode->nlink=2;

	if (entry->parent) {

	    /* adjust the parent inode:
		- a directory is added: link count is changed: ctim
	    */

	    entry->parent->inode->nlink++;
	    memcpy(&entry->parent->inode->ctim, &inode->mtim, sizeof(struct timespec));

	}

    } else {

	if (entry->parent) {

	    /* adjust the parent inode:
		- a file is added: mtim
	    */

	    entry->parent->inode->nlink++;
	    memcpy(&entry->parent->inode->mtim, &inode->mtim, sizeof(struct timespec));

	}

    }

}

static void _fs_common_create_cb_found(struct entry_s *entry, void *data)
{
    struct _fs_common_create_struct *_create_common=(struct _fs_common_create_struct *) data;

    if (_create_common->mayexist==1) {
	struct inode_s *inode=entry->inode;

	fill_inode_stat(inode, _create_common->st);
	*_create_common->error=0;

    } else {

	*_create_common->error=EEXIST;

    }

}

static void _fs_common_create_cb_error(struct entry_s *parent, struct name_s *xname, void *data, unsigned int error)
{
    struct _fs_common_create_struct *_create_common=(struct _fs_common_create_struct *) data;

    logoutput("_fs_common_create_cb_error: error %i:%s creating %s", error, strerror(error), xname->name);

    *_create_common->error=error;

}

struct entry_s *_fs_common_create_entry(struct workspace_mount_s *workspace, struct directory_s *directory, struct name_s *xname, struct stat *st, unsigned char mayexist, unsigned int *error)
{
    struct _fs_common_create_struct _create_common;

    _create_common.workspace=workspace;
    _create_common.st=st;
    _create_common.mayexist=mayexist;
    _create_common.error=error;

    return create_entry_extended_batch(directory, xname, _fs_common_create_cb_created, _fs_common_create_cb_found, _fs_common_create_cb_error, (void *) &_create_common);

}

struct entry_s *_fs_common_create_entry_unlocked(struct workspace_mount_s *workspace, struct entry_s *parent, struct name_s *xname, struct stat *st, unsigned char mayexist, unsigned int *error)
{
    struct _fs_common_create_struct _create_common;

    _create_common.workspace=workspace;
    _create_common.st=st;
    _create_common.mayexist=mayexist;
    _create_common.error=error;

    return create_entry_extended(parent, xname, _fs_common_create_cb_created, _fs_common_create_cb_found, _fs_common_create_cb_error, (void *) &_create_common);

}

void _fs_common_statfs(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, uint64_t blocks, uint64_t bfree, uint64_t bavail, uint32_t bsize)
{
    struct fuse_statfs_out statfs_out;

    /*
	howto determine these values ??
	it's not possible to determine those for all the backends/subfilesystems
    */

    statfs_out.st.blocks=blocks;
    statfs_out.st.bfree=bfree;
    statfs_out.st.bavail=bavail;
    statfs_out.st.bsize=bsize;

    statfs_out.st.files=(uint64_t) context->workspace->nrinodes;
    statfs_out.st.ffree=(uint64_t) (UINT32_T_MAX - statfs_out.st.files);

    statfs_out.st.namelen=255; /* a sane default */
    statfs_out.st.frsize=bsize; /* use the same as block size */

    statfs_out.st.padding=0;

    reply_VFS_data(request, (char *) &statfs_out, sizeof(struct fuse_statfs_out));

}

/*
    test a (absolute) symlink is subdirectory of the context directory
*/

int symlink_generic_validate(struct service_context_s *context, char *target)
{
    unsigned int len=strlen(target);
    struct pathinfo_s *mountpoint=&context->workspace->mountpoint;

    if (len>mountpoint->len) {

	if (strncmp(target, mountpoint->path, mountpoint->len)==0 && target[mountpoint->len]=='/') {
	    struct pathcalls_s *pathcalls=NULL;
	    struct directory_s *directory=NULL;

	    pathcalls=get_pathcalls(context->inode);
	    directory=get_directory(context->inode);

	    if (directory) {
		char *pos=&target[mountpoint->len];
		unsigned int pathlen=get_pathmax(context->workspace);
		char path[pathlen + 1];
		struct fuse_path_s fpath;
		unsigned int len_s=0;

		/* get the path of the directory representing this context (relative to the mountpoint) */

		init_fuse_path(&fpath, path, pathlen);

		lock_pathcalls(pathcalls);
		len_s+=(* pathcalls->get_path)(directory, &fpath);
		unlock_pathcalls(pathcalls);

		if (len > mountpoint->len + len_s) {

		    if (strncmp(pos, fpath.pathstart, len_s)==0 && target[mountpoint->len + len_s]=='/') {

			/* absolute symlink is pointing to object in this context */

			return (mountpoint->len + len_s);

		    }

		}

	    }

	}

    }

    return 0;

}
