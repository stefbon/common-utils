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
#include <sys/statfs.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "logging.h"
#include "main.h"

#include "pathinfo.h"
#include "entry-management.h"
#include "directory-management.h"
#include "entry-utils.h"

#include "utils.h"
#include "fuse-interface.h"

#include "fuse-fs.h"
#include "workspaces.h"
#include "fuse-fs-virtual.h"
#include "fuse-fs-common.h"

#define UINT32_T_MAX		0xFFFFFFFF

static struct fuse_fs_s fs;
static char *desktopentryname=".directory";
static pthread_mutex_t desktopmutex=PTHREAD_MUTEX_INITIALIZER;
static struct statfs statfs_keep;

#define _SPECIAL_TYPE_OVERLAY			1

struct special_file_s {
    unsigned int			type;
    struct entry_s			*parent;
    struct special_file_s		*next;
    struct special_file_s		*prev;
    char				*name;
    struct inode_s			inode;
    union {
	char				*target;
    } backend;
};

static struct special_file_s *desktopentrytable[64];

static void _fs_special_getattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    logoutput("_fs_special_getattr");
    _fs_common_getattr(context, request, inode);
}

static void _fs_special_setattr(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode, struct stat *st, unsigned int set)
{
    logoutput("_fs_special_setattr");

    if (set & (FATTR_SIZE | FATTR_UID | FATTR_GID | FATTR_MODE | FATTR_MTIME | FATTR_CTIME)) {

	reply_VFS_error(request, EPERM);
	return;

    }

    if (set & FATTR_ATIME) {

	if (set & FATTR_ATIME_NOW) get_current_time(&st->st_atim);

	inode->atim.tv_sec=st->st_atim.tv_sec;
	inode->atim.tv_nsec=st->st_atim.tv_nsec;

    }

    _fs_common_getattr(context, request, inode);

}

static void _fs_special_open(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned int flags)
{
    unsigned int error=EIO;
    struct inode_s *inode=openfile->inode;
    struct special_file_s *special=(struct special_file_s *) ( ((char *) inode) - offsetof(struct special_file_s, inode));

    logoutput("_fs_special_open");

    if (special) {

	if (special->type==_SPECIAL_TYPE_OVERLAY) {
	    int fd=0;

	    fd=open(special->backend.target, flags);

	    if (fd>0) {
		struct fuse_open_out open_out;

		openfile->handle.fd=fd;

		open_out.fh=(uint64_t) openfile;
		open_out.open_flags=FOPEN_KEEP_CACHE;
		open_out.padding=0;
		reply_VFS_data(request, (char *) &open_out, sizeof(open_out));
		return;

	    } else {

		error=errno;

	    }

	}

    }

    openfile->error=error;
    reply_VFS_error(request, error);

}

void _fs_special_read(struct fuse_openfile_s *openfile, struct fuse_request_s *request, size_t size, off_t off, unsigned int flags, uint64_t lock_owner)
{
    unsigned int error=EIO;
    unsigned char buffer[size];
    ssize_t read=0;

    read=pread(openfile->handle.fd, (void *) buffer, size, off);

    logoutput("_fs_special_read: read %i", read);

    if (read==-1) {

	error=errno;

    } else {

	reply_VFS_data(request, buffer, read);
	return;

    }

    reply_VFS_error(request, error);

}

void _fs_special_write(struct fuse_openfile_s *openfile, struct fuse_request_s *request, const char *buffer, size_t size, off_t off, unsigned int flags, uint64_t lock_owner)
{
    logoutput("_fs_special_write");
    reply_VFS_error(request, EPERM);
}

void _fs_special_flush(struct fuse_openfile_s *openfile, struct fuse_request_s *request, uint64_t lock_owner)
{
    logoutput("_fs_special_flush");
    reply_VFS_error(request, 0);
}

void _fs_special_fsync(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned char sync)
{
    logoutput("_fs_special_fsync");
    reply_VFS_error(request, 0);
}

void _fs_special_release(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned int flags, uint64_t lock_owner)
{
    logoutput("_fs_special_release");
    close(openfile->handle.fd);
    reply_VFS_error(request, 0);
}

void _fs_special_fsetattr(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct stat *st, int fuse_set)
{
    logoutput("_fs_special_fsetattr");
    _fs_special_setattr(openfile->context, request, openfile->inode, st, fuse_set);
}

void _fs_special_fgetattr(struct fuse_openfile_s *openfile, struct fuse_request_s *request)
{
    logoutput("_fs_special_fgetattr");
    _fs_special_getattr(openfile->context, request, openfile->inode);
}

static void _fs_special_statfs(struct service_context_s *context, struct fuse_request_s *request, struct inode_s *inode)
{
    struct fuse_statfs_out statfs_out;

    logoutput("_fs_special_statfs");

    memset(&statfs_out, 0, sizeof(struct fuse_statfs_out));

    statfs_out.st.blocks=statfs_keep.f_blocks;
    statfs_out.st.bfree=statfs_keep.f_bfree;
    statfs_out.st.bavail=statfs_keep.f_bavail;
    statfs_out.st.bsize=statfs_keep.f_bsize;

    statfs_out.st.files=(uint64_t) context->workspace->nrinodes;
    statfs_out.st.ffree=(uint64_t) (UINT32_T_MAX - statfs_out.st.files);

    statfs_out.st.namelen=255;
    statfs_out.st.frsize=statfs_out.st.bsize;

    statfs_out.st.padding=0;

    reply_VFS_data(request, (char *) &statfs_out, sizeof(struct fuse_statfs_out));

}

void init_special_fs()
{
    unsigned int error=0;

    statfs("/", &statfs_keep);

    set_virtual_fs(&fs);

    fs.getattr=_fs_special_getattr;
    fs.setattr=_fs_special_setattr;
    fs.type.nondir.open=_fs_special_open;
    fs.type.nondir.read=_fs_special_read;
    fs.type.nondir.write=_fs_special_write;
    fs.type.nondir.flush=_fs_special_flush;
    fs.type.nondir.fsync=_fs_special_fsync;
    fs.type.nondir.release=_fs_special_release;
    fs.type.nondir.fsetattr=_fs_special_fsetattr;
    fs.type.nondir.fgetattr=_fs_special_fgetattr;
    fs.statfs=_fs_special_statfs;

    for (unsigned int i=0; i<64; i++) desktopentrytable[i]=NULL;

}

void set_fs_special(struct inode_s *inode)
{

    if (! S_ISDIR(inode->mode)) {

	inode->fs=&fs;

    }

}

struct special_file_s *lookup_desktopentry(uint64_t ino)
{
    unsigned int hash=ino % 64;
    struct special_file_s *special=desktopentrytable[hash];

    while(special) {

	if (special->parent->inode->ino==ino) break;
	special=special->next;

    }

    return special;
}

void add_desktopentry(struct special_file_s *special)
{
    unsigned int hash=special->parent->inode->ino % 64;

    if (desktopentrytable[hash]) {

	special->next=desktopentrytable[hash];
	desktopentrytable[hash]->prev=special;

    }

    desktopentrytable[hash]=special;

}

void create_desktopentry_file(char *path, struct entry_s *parent, struct workspace_mount_s *workspace)
{
    struct stat st;

    logoutput("create_desktopentry_file: path %s", path);

    if (lstat(path, &st)==0 && S_ISREG(st.st_mode)) {
	struct special_file_s *special=NULL;

	pthread_mutex_lock(&desktopmutex);

	special=lookup_desktopentry(parent->inode->ino);

	if (! special) {
	    char *target=NULL;

	    special=malloc(sizeof(struct special_file_s));
	    target=strdup(path);

	    if (special && target) {
		struct inode_s *inode=&special->inode;

		init_inode(inode);

		special->name=desktopentryname;
		special->parent=parent;
		special->next=NULL;
		special->prev=NULL;

		inode->mode=st.st_mode;
		inode->uid=st.st_uid;
		inode->gid=st.st_gid;
		inode->nlink=1;

		inode->size=st.st_size;
		inode->fs=&fs;

		get_current_time(&inode->mtim);
		memcpy(&inode->ctim, &inode->mtim, sizeof(struct timespec));
		memcpy(&inode->atim, &inode->mtim, sizeof(struct timespec));

		special->type=_SPECIAL_TYPE_OVERLAY;
		special->backend.target=target;

		add_desktopentry(special);
		add_inode_hashtable(inode, increase_inodes_workspace, (void *) workspace);

	    } else {

		if (special) {

		    free(special);
		    special=NULL;

		}

		if (target) {

		    free(target);
		    target=NULL;

		}

	    }

	}

	pthread_mutex_unlock(&desktopmutex);

    }

}

void special_file_lookup(struct service_context_s *context, struct entry_s *parent, struct fuse_request_s *request)
{
    struct special_file_s *special=NULL;

    logoutput("special_file_lookup");

    pthread_mutex_lock(&desktopmutex);

    special=lookup_desktopentry(parent->inode->ino);

    if (special) {
	struct inode_s *inode=&special->inode;

	inode->nlookup++;
	logoutput("special_file_lookup: found");
	_fs_common_cached_lookup(context, request, inode);

    } else {

	reply_VFS_error(request, ENOENT);

    }

    pthread_mutex_unlock(&desktopmutex);

}

void remove_special_files()
{
    struct special_file_s *special=NULL;

    for (unsigned int i=0; i<64; i++) {

	special=desktopentrytable[i];

	while (special) {

	    desktopentrytable[i]=special->next;

	    if (special->type==_SPECIAL_TYPE_OVERLAY) {

		if (special->backend.target) free(special->backend.target);

	    }

	    free(special);
	    special=desktopentrytable[i];

	}

    }

}
