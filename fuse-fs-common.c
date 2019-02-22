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
#include "utils.h"
#include "beventloop.h"
#include "options.h"

#include "fuse-dentry.h"
#include "fuse-directory.h"
#include "fuse-utils.h"
#include "fuse-fs.h"
#include "workspaces.h"
#include "workspace-context.h"
#include "path-caching.h"
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
		const char *subpath=path + len + 1;
		char buffer[strlen(subpath)+1];

		strcpy(buffer, subpath);
		entry=walk_fuse_fs(mount->rootinode.alias, buffer);

	    }

	}

    } else {
	const char *pos=path;

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
    struct timespec *attr_timeout=NULL;
    struct timespec *entry_timeout=NULL;

    logoutput("_fs_common_cached_lookup: ino %li name %.*s", inode->st.st_ino, inode->alias->name.len, inode->alias->name.name);

    context=get_root_context(context);
    attr_timeout=get_fuse_interface_attr_timeout(context->interface.ptr);
    entry_timeout=get_fuse_interface_entry_timeout(context->interface.ptr);

    inode->nlookup++;

    entry_out.nodeid=inode->st.st_ino;
    entry_out.generation=0; /* todo: add a generation field to reuse existing inodes */

    entry_out.entry_valid=entry_timeout->tv_sec;
    entry_out.entry_valid_nsec=entry_timeout->tv_nsec;

    entry_out.attr_valid=attr_timeout->tv_sec;
    entry_out.attr_valid_nsec=attr_timeout->tv_nsec;

    entry_out.attr.ino=inode->st.st_ino;
    entry_out.attr.size=inode->st.st_size;

    entry_out.attr.blksize=_DEFAULT_BLOCKSIZE;
    entry_out.attr.blocks=inode->st.st_size / _DEFAULT_BLOCKSIZE + (inode->st.st_size % _DEFAULT_BLOCKSIZE == 0) ? 0 : 1;

    entry_out.attr.atime=(uint64_t) inode->st.st_atim.tv_sec;
    entry_out.attr.atimensec=(uint32_t) inode->st.st_atim.tv_nsec;

    entry_out.attr.mtime=(uint64_t) inode->st.st_mtim.tv_sec;
    entry_out.attr.mtimensec=(uint32_t) inode->st.st_mtim.tv_nsec;
    entry_out.attr.ctime=(uint64_t) inode->st.st_ctim.tv_sec;
    entry_out.attr.ctimensec=(uint32_t) inode->st.st_ctim.tv_nsec;

    entry_out.attr.mode=inode->st.st_mode;
    entry_out.attr.nlink=inode->st.st_nlink;
    entry_out.attr.uid=inode->st.st_uid;
    entry_out.attr.gid=inode->st.st_gid;
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
    struct timespec *attr_timeout=NULL;
    struct timespec *entry_timeout=NULL;

    context=get_root_context(context);
    attr_timeout=get_fuse_interface_attr_timeout(context->interface.ptr);
    entry_timeout=get_fuse_interface_entry_timeout(context->interface.ptr);
    char buffer[size_entry_out + size_open_out];

    // inode->nlookup++;

    entry_out.nodeid=inode->st.st_ino;
    entry_out.generation=0; /* todo: add a generation field to reuse existing inodes */

    entry_out.entry_valid=entry_timeout->tv_sec;
    entry_out.entry_valid_nsec=entry_timeout->tv_nsec;

    entry_out.attr_valid=attr_timeout->tv_sec;
    entry_out.attr_valid_nsec=attr_timeout->tv_nsec;

    entry_out.attr.ino=inode->st.st_ino;
    entry_out.attr.size=inode->st.st_size;

    entry_out.attr.blksize=_DEFAULT_BLOCKSIZE;
    entry_out.attr.blocks=inode->st.st_size / _DEFAULT_BLOCKSIZE + (inode->st.st_size % _DEFAULT_BLOCKSIZE == 0) ? 0 : 1;

    entry_out.attr.atime=(uint64_t) inode->st.st_atim.tv_sec;
    entry_out.attr.atimensec=(uint32_t) inode->st.st_atim.tv_nsec;

    entry_out.attr.mtime=(uint64_t) inode->st.st_mtim.tv_sec;
    entry_out.attr.mtimensec=(uint32_t) inode->st.st_mtim.tv_nsec;

    entry_out.attr.ctime=(uint64_t) inode->st.st_ctim.tv_sec;
    entry_out.attr.ctimensec=(uint32_t) inode->st.st_ctim.tv_nsec;

    entry_out.attr.mode=inode->st.st_mode;
    entry_out.attr.nlink=inode->st.st_nlink;
    entry_out.attr.uid=inode->st.st_uid;
    entry_out.attr.gid=inode->st.st_gid;
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

void _fs_common_virtual_lookup(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *pinode, const char *name, unsigned int len)
{
    struct entry_s *parent=pinode->alias, *entry=NULL;
    struct name_s xname={NULL, 0, 0};
    unsigned int error=0;

    logoutput("_fs_common_virtual_lookup: name %.*s parent %li (thread %i)", len, name, (long) pinode->st.st_ino, (int) gettid());

    xname.name=(char *)name;
    xname.len=strlen(name);
    calculate_nameindex(&xname);

    entry=find_entry(parent, &xname, &error);

    if (entry) {
	struct inode_s *inode=entry->inode;
	struct inode_link_s *link=NULL;

	/* it's possible that the entry represents the root of a service
	    in that case do a lookup of the '.' on the root of the service using the service specific fs calls
	*/

	log_inode_information(inode, INODE_INFORMATION_NAME | INODE_INFORMATION_NLOOKUP | INODE_INFORMATION_MODE | INODE_INFORMATION_SIZE | INODE_INFORMATION_MTIM | INODE_INFORMATION_INODE_LINK | INODE_INFORMATION_FS_COUNT);

	logoutput("_fs_common_virtual_lookup: found entry %.*s ino %li nlookup %i", entry->name.len, entry->name.name, inode->st.st_ino, inode->nlookup);
	inode->nlookup++;
	fs_get_inode_link(inode, &link);

	if (link->type==INODE_LINK_TYPE_CONTEXT) {
	    struct service_context_s *service_context=(struct service_context_s *) link->link.ptr;
	    struct pathinfo_s pathinfo=PATHINFO_INIT;
	    unsigned int pathlen=0;
	    char path[3];
	    struct service_fs_s *fs=NULL;

	    logoutput("_fs_common_virtual_lookup: use context %s", service_context->name);

	    path[2]='\0';
	    path[1]='.';
	    path[0]='/';

	    pathinfo.len=2;
	    pathinfo.path=path;

	    fs=service_context->service.filesystem.fs;

	    (* fs->lookup_existing)(service_context, request, entry, &pathinfo);
	    return;

	}

	_fs_common_cached_lookup(context, request, inode);

    } else {

	logoutput("_fs_common_virtual_lookup: enoent lookup %i", (entry) ? entry->inode->nlookup : 0);
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
    struct timespec *attr_timeout=NULL;

    logoutput("_fs_common_getattr: context %s", context->name);

    context=get_root_context(context);
    attr_timeout=get_fuse_interface_attr_timeout(context->interface.ptr);

    attr_out.attr_valid=attr_timeout->tv_sec;
    attr_out.attr_valid_nsec=attr_timeout->tv_nsec;

    attr_out.attr.ino=inode->st.st_ino;
    attr_out.attr.size=inode->st.st_size;

    attr_out.attr.blksize=_DEFAULT_BLOCKSIZE;
    attr_out.attr.blocks=inode->st.st_size / _DEFAULT_BLOCKSIZE + (inode->st.st_size % _DEFAULT_BLOCKSIZE == 0) ? 0 : 1;

    attr_out.attr.atime=(uint64_t) inode->st.st_atim.tv_sec;
    attr_out.attr.atimensec=(uint32_t) inode->st.st_atim.tv_nsec;

    attr_out.attr.mtime=(uint64_t) inode->st.st_mtim.tv_sec;
    attr_out.attr.mtimensec=(uint32_t) inode->st.st_mtim.tv_nsec;

    attr_out.attr.ctime=(uint64_t) inode->st.st_ctim.tv_sec;
    attr_out.attr.ctimensec=(uint32_t) inode->st.st_ctim.tv_nsec;

    attr_out.attr.mode=inode->st.st_mode;
    attr_out.attr.nlink=inode->st.st_nlink;
    attr_out.attr.uid=inode->st.st_uid;
    attr_out.attr.gid=inode->st.st_gid;
    attr_out.attr.rdev=0; /* no special devices supported */

    attr_out.attr.padding=0;
    attr_out.dummy=0;

    reply_VFS_data(request, (char *) &attr_out, sizeof(attr_out));

}

void _fs_common_virtual_opendir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned int flags)
{
    unsigned int error=0;
    struct directory_s *directory=NULL;
    struct fuse_open_out open_out;

    logoutput("_fs_common_virtual_opendir: ino %li", opendir->inode->st.st_ino);
    directory=get_directory(opendir->inode);

    if (directory && directory->count>0) opendir->mode |= _FUSE_READDIR_MODE_NONEMPTY;
    open_out.fh=(uint64_t) opendir;
    open_out.open_flags=0;
    open_out.padding=0;
    reply_VFS_data(request, (char *) &open_out, sizeof(open_out));
    opendir->handle.ptr=(void *) directory->first;
    logoutput("_fs_common_virtual_opendir: ready");

}

void _fs_common_virtual_readdir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset)
{
    struct stat st;
    size_t pos=0, dirent_size=0;
    struct directory_s *directory=NULL;
    struct name_s xname={NULL, 0, 0};
    struct inode_s *inode=NULL;
    struct entry_s *entry=NULL;
    char buff[size];
    unsigned int error=0;
    struct simple_lock_s rlock;

    if (opendir->mode & _FUSE_READDIR_MODE_FINISH) {
	char dummy='\0';

	logoutput("_fs_common_virtual_readdir: finish");
	reply_VFS_data(request, &dummy, 0);
	return;

    }

    logoutput("_fs_common_virtual_readdir");
    directory=get_directory(opendir->inode);

    if (rlock_directory(directory, &rlock)==-1) {

	reply_VFS_error(request, EAGAIN);
	return;

    }

    memset(&st, 0, sizeof(struct stat));

    while (pos<size) {

	if (offset==0) {

	    inode=opendir->inode;

    	    /* the . entry */

    	    st.st_ino = inode->st.st_ino;
	    st.st_mode = S_IFDIR;
	    xname.name = (char *) dotname;
	    xname.len=1;

    	} else if (offset==1) {
    	    struct entry_s *parent=NULL;

	    inode=opendir->inode;

	    /* the .. entry */

	    parent=inode->alias;
	    if (parent->parent) inode=parent->parent->inode;

    	    st.st_ino = inode->st.st_ino;
	    st.st_mode = S_IFDIR;
	    xname.name = (char *) dotdotname;
	    xname.len=2;

    	} else {

	    entry=opendir->entry;

	    if (entry==NULL) {

		readdir:

		entry=(struct entry_s *) opendir->handle.ptr;

		if (entry) {

		    opendir->handle.ptr=(void *) entry->name_next;

		} else {

		    opendir->mode |= _FUSE_READDIR_MODE_FINISH;
		    break;

		}

	    }

	    inode=entry->inode;

	    if ((* opendir->skip_file)(opendir, inode)==0) {

		opendir->entry=NULL;
		goto readdir;

	    }

	    st.st_ino=inode->st.st_ino;
	    st.st_mode=inode->st.st_mode;
	    xname.name=entry->name.name;
	    xname.len=entry->name.len;

	}

	// get_inode_stat(inode, &st);
	dirent_size=add_direntry_buffer(request->interface->ptr, buff + pos, size - pos, offset + 1, &xname, &st, &error);

	if (error==ENOBUFS) {

	    opendir->entry=entry; /* keep it for the next batch */
	    break;

	}

	/* increase counter and clear the various fields */

	opendir->entry=NULL; /* forget current entry to force readdir */
	offset++;
	pos+=dirent_size;

    }

    unlock_directory(directory, &rlock);
    reply_VFS_data(request, buff, pos);

}

/* TODO not ready yet */

void _fs_common_virtual_readdirplus(struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset)
{
    struct stat st;
    size_t pos=0, dirent_size=0;
    struct directory_s *directory=NULL;
    struct name_s xname={NULL, 0, 0};
    struct inode_s *inode=NULL;
    struct entry_s *entry=NULL;
    char buff[size];
    unsigned int error=0;
    struct simple_lock_s rlock;

    if (opendir->mode & _FUSE_READDIR_MODE_FINISH) {
	char dummy='\0';

	logoutput("_fs_common_virtual_readdirplus: finish");
	reply_VFS_data(request, &dummy, 0);
	return;

    }

    logoutput("_fs_common_virtual_readdirplus");
    directory=get_directory(opendir->inode);

    if (rlock_directory(directory, &rlock)==-1) {

	reply_VFS_error(request, EAGAIN);
	return;

    }

    memset(&st, 0, sizeof(struct stat));

    while (pos<size) {

	if (offset==0) {

	    inode=opendir->inode;

    	    /* the . entry */

    	    st.st_ino = inode->st.st_ino;
	    st.st_mode = S_IFDIR;
	    xname.name = (char *) dotname;
	    xname.len=1;

    	} else if (offset==1) {
    	    struct entry_s *parent=NULL;

	    inode=opendir->inode;

	    /* the .. entry */

	    parent=inode->alias;
	    if (parent->parent) inode=parent->parent->inode;

    	    st.st_ino = inode->st.st_ino;
	    st.st_mode = S_IFDIR;
	    xname.name = (char *) dotdotname;
	    xname.len=2;

    	} else {

	    if (opendir->entry) {

		entry=opendir->entry;


	    } else {

		readdir:

		entry=(struct entry_s *) opendir->handle.ptr;

		if (entry) {

		    opendir->handle.ptr=(void *) entry->name_next;

		} else {

		    opendir->mode |= _FUSE_READDIR_MODE_FINISH;
		    break;

		}


	    }

	    inode=entry->inode;

	    st.st_ino=inode->st.st_ino;
	    st.st_mode=inode->st.st_mode;
	    xname.name=entry->name.name;
	    xname.len=entry->name.len;
	    inode->nlookup++;

	    entry=entry->name_next;

	}

	// get_inode_stat(inode, &st);
	dirent_size=add_direntry_plus_buffer(request->interface->ptr, buff + pos, size - pos, offset + 1, &xname, &st, &error);

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
    unlock_directory(directory, &rlock);

}

void _fs_common_virtual_releasedir(struct fuse_opendir_s *opendir, struct fuse_request_s *request)
{

    logoutput("_fs_common_virtual_releasedir");
    reply_VFS_error(request, 0);

}

void _fs_common_virtual_fsyncdir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned char datasync)
{
    reply_VFS_error(request, 0);
}

struct entry_s *_fs_common_create_entry(struct workspace_mount_s *workspace, struct entry_s *parent, struct name_s *xname, struct stat *st, unsigned int size, unsigned int flags, unsigned int *error)
{
    struct create_entry_s ce;
    unsigned int dummy=0;

    logoutput("_fs_common_create_entry");

    if (error==0) error=&dummy;
    init_create_entry(&ce, xname, parent, NULL, NULL, workspace->context, st, NULL);

    return create_entry_extended(&ce);
}

struct entry_s *_fs_common_create_entry_unlocked(struct workspace_mount_s *workspace, struct directory_s *directory, struct name_s *xname, struct stat *st, unsigned int size, unsigned int flags, unsigned int *error)
{
    struct create_entry_s ce;
    unsigned int dummy=0;

    logoutput("_fs_common_create_entry_unlocked");

    if (error==0) error=&dummy;
    init_create_entry(&ce, xname, NULL, directory, NULL, workspace->context, st, NULL);

    return create_entry_extended_batch(&ce);
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
	    struct directory_s *directory=get_directory(context->service.filesystem.inode);

	    if (directory) {
		char *pos=&target[mountpoint->len];
		unsigned int pathlen=get_pathmax(context->workspace);
		char path[pathlen + 1];
		struct fuse_path_s fpath;
		unsigned int len_s=0;
		struct pathcalls_s *pathcalls=NULL;

		/* get the path of the directory representing this context (relative to the mountpoint) */

		pathcalls=get_pathcalls(directory);
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
