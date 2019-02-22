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

#include "global-defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <inttypes.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>

#define OFFSET_MAX 0x7fffffffffffffffLL

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#define LOGGING
#include "logging.h"
#include "pathinfo.h"
#include "utils.h"

#include "fuse-dentry.h"
#include "fuse-directory.h"
#include "fuse-interface.h"
#include "fuse-fs.h"
#include "workspaces.h"
#include "workspace-context.h"
#include "workspace-interface.h"

extern int check_entry_special(struct inode_s *inode);

void copy_fuse_fs(struct fuse_fs_s *to, struct fuse_fs_s *from)
{
    memset(to, 0, sizeof(struct fuse_fs_s));
    memcpy(to, from, sizeof(struct fuse_fs_s));
}

void fs_inode_forget(struct inode_s *inode)
{
    (* inode->fs->forget)(inode);
}

int fs_lock_datalink(struct inode_s *inode)
{
    return (* inode->fs->lock_datalink)(inode);
}

int fs_unlock_datalink(struct inode_s *inode)
{
    return (* inode->fs->unlock_datalink)(inode);
}

void fs_get_inode_link(struct inode_s *inode, struct inode_link_s **link)
{
    (* inode->fs->get_inode_link)(inode, link);
}

void fuse_fs_forget(struct fuse_request_s *request)
{
    struct fuse_forget_in *forget_in=(struct fuse_forget_in *) request->buffer;
    struct service_context_s *context=NULL;

    // logoutput("FORGET (thread %i): ino %lli forget %i", (int) gettid(), (long long) request->ino, forget_in->nlookup);

    context=get_service_context(request->interface);
    queue_inode_2forget(request->ino, context->unique, FORGET_INODE_FLAG_FORGET, forget_in->nlookup);
}

void fuse_fs_forget_multi(struct fuse_request_s *request)
{
    struct service_context_s *context=NULL;
    struct fuse_batch_forget_in *batch_forget_in=(struct fuse_batch_forget_in *)request->buffer;
    struct fuse_forget_one *forgets=(struct fuse_forget_one *) (request->buffer + sizeof(struct fuse_batch_forget_in));
    unsigned int i=0;

    // logoutput("FORGET_MULTI: (thread %i) count %i", (int) gettid(), batch_forget_in->count);

    context=get_service_context(request->interface);

    for (i=0; i<batch_forget_in->count; i++) {

	// logoutput("FORGET_MULTI: ino %lli forget %i", (int) gettid(), forgets[i].nodeid);
	queue_inode_2forget(forgets[i].nodeid, context->unique, FORGET_INODE_FLAG_FORGET, forgets[i].nlookup);

    }

}

void fuse_fs_lookup(struct fuse_request_s *request)
{
    char *name=(char *) request->buffer;
    struct service_context_s *context=get_service_context(request->interface);

    logoutput("fuse_fs_lookup");

    if (request->ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;

	(* inode->fs->type.dir.lookup)(context, request, inode, name, strlen(name));

    } else {
	struct inode_s *inode=find_inode(request->ino);

	if (inode) {

	    (* inode->fs->type.dir.lookup)(context, request, inode, name, strlen(name));

	} else {

	    logoutput("fuse_fs_lookup: %li not found", request->ino);
	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_getattr(struct fuse_request_s *request)
{
    struct fuse_getattr_in *getattr_in=(struct fuse_getattr_in *) request->buffer;
    struct service_context_s *context=get_service_context(request->interface);

    logoutput("fuse_fs_getattr: ino %li", request->ino);

    if (request->ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;

	if ((getattr_in->getattr_flags & FUSE_GETATTR_FH) && getattr_in->fh>0) {
	    struct fuse_openfile_s *openfile= (struct fuse_openfile_s *) getattr_in->fh;

	    // logoutput("fuse_fs_getattr: fgetattr");

	    (* inode->fs->type.nondir.fgetattr) (openfile, request);

	} else {

	    // logoutput("fuse_fs_getattr: getattr");

	    (* inode->fs->getattr)(context, request, inode);

	}

    } else {
	struct inode_s *inode=find_inode(request->ino);

	if (inode) {

	    if ((getattr_in->getattr_flags & FUSE_GETATTR_FH) && getattr_in->fh>0) {
		struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) getattr_in->fh;

		// logoutput("fuse_fs_getattr: fgetattr");

		(* inode->fs->type.nondir.fgetattr) (openfile, request);

	    } else {

		// logoutput("fuse_fs_getattr: getattr");

		(* inode->fs->getattr)(context, request, inode);

	    }

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

static void fill_stat_attr(struct fuse_setattr_in *attr, struct stat *st)
{
    st->st_mode=attr->mode;
    st->st_uid=attr->uid;
    st->st_gid=attr->gid;
    st->st_size=attr->size;
    st->st_atim.tv_sec=attr->atime;
    st->st_atim.tv_nsec=attr->atimensec;
    st->st_mtim.tv_sec=attr->mtime;
    st->st_mtim.tv_nsec=attr->mtimensec;
    st->st_ctim.tv_sec=attr->ctime;
    st->st_ctim.tv_nsec=attr->ctimensec;
}

void fuse_fs_setattr(struct fuse_request_s *request)
{
    struct fuse_setattr_in *setattr_in=(struct fuse_setattr_in *) request->buffer;
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;

    if (ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;
	struct stat st;

	if (setattr_in->valid & FATTR_FH) {
	    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) setattr_in->fh;

	    setattr_in->valid -= FATTR_FH;
	    fill_stat_attr(setattr_in, &st);
	    (* inode->fs->type.nondir.fsetattr) (openfile, request, &st, setattr_in->valid);

	} else {

	    fill_stat_attr(setattr_in, &st);
	    (* inode->fs->setattr)(context, request, inode, &st, setattr_in->valid);

	}

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {
	    struct stat st;

	    if (setattr_in->valid & FATTR_FH) {
		struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) setattr_in->fh;

		setattr_in->valid -= FATTR_FH;
		fill_stat_attr(setattr_in, &st);
		(* inode->fs->type.nondir.fsetattr) (openfile, request, &st, setattr_in->valid);

	    } else {

		fill_stat_attr(setattr_in, &st);
		(* inode->fs->setattr)(context, request, inode, &st, setattr_in->valid);

	    }

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_readlink(struct fuse_request_s *request)
{
    struct inode_s *inode=find_inode(request->ino);
    struct service_context_s *context=get_service_context(request->interface);

    if (inode) {

	(* inode->fs->type.nondir.readlink)(context, request, inode);

    } else {

	reply_VFS_error(request, ENOENT);

    }

}

void fuse_fs_mkdir(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;
    struct fuse_mkdir_in *mkdir_in=(struct fuse_mkdir_in *)request->buffer;
    char *name=(char *) (request->buffer + sizeof(struct fuse_mkdir_in));
    unsigned int len=strlen(name);

    if (ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;

	(* inode->fs->type.dir.mkdir)(context, request, inode, name, len, mkdir_in->mode, mkdir_in->umask);

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {

	    (* inode->fs->type.dir.mkdir)(context, request, inode, name, len, mkdir_in->mode, mkdir_in->umask);

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_mknod(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;
    struct fuse_mknod_in *mknod_in=(struct fuse_mknod_in *) request->buffer;
    char *name=(char *) (request->buffer + sizeof(struct fuse_mknod_in));
    unsigned int len=strlen(name);

    if (ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;

	(* inode->fs->type.dir.mknod)(context, request, inode, name, len, mknod_in->mode, mknod_in->rdev, mknod_in->umask);

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {

	    (* inode->fs->type.dir.mknod)(context, request, inode, name, len, mknod_in->mode, mknod_in->rdev, mknod_in->umask);

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_symlink(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;
    char *name=(char *) request->buffer;
    unsigned int len0=strlen(name);
    char *target=(char *) (request->buffer + len0);
    unsigned int len1=strlen(target);

    if (ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;

	(* inode->fs->type.dir.symlink)(context, request, inode, name, len0, target, len1);

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {

	    (* inode->fs->type.dir.symlink)(context, request, inode, name, len0, target, len1);

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_unlink(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;
    char *name=(char *) request->buffer;
    struct inode_s *inode=find_inode(ino);

    if (inode) {

	(* inode->fs->type.dir.unlink)(context, request, inode, name, strlen(name));

    } else {

	reply_VFS_error(request, ENOENT);

    }

}

void fuse_fs_rmdir(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;
    char *name=(char *) request->buffer;
    struct inode_s *inode=find_inode(ino);

    if (inode) {

	(* inode->fs->type.dir.rmdir)(context, request, inode, name, strlen(name));

    } else {

	reply_VFS_error(request, (ino==FUSE_ROOT_ID) ? EACCES : ENOENT);

    }

}

void fuse_fs_rename(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;
    struct fuse_rename_in *rename_in=(struct fuse_rename_in *) request->buffer;
    char *oldname=(char *) (request->buffer + sizeof(struct fuse_rename_in));
    uint64_t newino=rename_in->newdir;
    char *newname=(char *) (request->buffer + sizeof(struct fuse_rename_in) + strlen(oldname));

    if (ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;

	if (newino==FUSE_ROOT_ID) {
	    struct inode_s *newinode=&workspace->rootinode;

	    (* inode->fs->type.dir.rename)(context, request, inode, oldname, newinode, newname, 0);

	} else {
	    struct inode_s *newinode=find_inode(newino);

	    if (newinode) {

		(* inode->fs->type.dir.rename)(context, request, inode, oldname, newinode, newname, 0);

	    } else {

		reply_VFS_error(request, ENOENT);

	    }

	}

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {

	    if (newino==FUSE_ROOT_ID) {
		struct workspace_mount_s *workspace=context->workspace;
		struct inode_s *newinode=&workspace->rootinode;

		(* inode->fs->type.dir.rename)(context, request, inode, oldname, newinode, newname, 0);

	    } else {
		struct inode_s *newinode=find_inode(newino);

		if (newinode) {

		    (* inode->fs->type.dir.rename)(context, request, inode, oldname, newinode, newname, 0);

		} else {

		    reply_VFS_error(request, ENOENT);

		}

	    }

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_link(struct fuse_request_s *request)
{
    reply_VFS_error(request, ENOSYS);
}

void fuse_fs_open(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;
    struct inode_s *inode=find_inode(ino);

    if (inode) {
	struct fuse_open_in *open_in=(struct fuse_open_in *) request->buffer;
	struct fuse_openfile_s *openfile=NULL;

	openfile=malloc(sizeof(struct fuse_openfile_s));

	if (openfile) {

	    memset(openfile, 0, sizeof(struct fuse_openfile_s));

	    openfile->context=context;
	    openfile->inode=inode;
	    openfile->error=0;
	    openfile->flock=0;

	    (* inode->fs->type.nondir.open)(openfile, request, open_in->flags & (O_ACCMODE | O_APPEND | O_TRUNC));

	    if (openfile->error>0) {

		/* subcall has send a reply to VFS already, here only free */

		free(openfile);
		openfile=NULL;

	    }

	} else {

	    reply_VFS_error(request, ENOMEM);

	}

    } else {

	reply_VFS_error(request, ENOENT);

    }

}

void fuse_fs_read(struct fuse_request_s *request)
{
    struct fuse_read_in *read_in=(struct fuse_read_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) read_in->fh;

    if (openfile) {
	struct inode_s *inode=openfile->inode;
	uint64_t lock_owner=(read_in->flags & FUSE_READ_LOCKOWNER) ? read_in->lock_owner : 0;

	(* inode->fs->type.nondir.read) (openfile, request, read_in->size, read_in->offset, read_in->flags, lock_owner);

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_write(struct fuse_request_s *request)
{
    struct fuse_write_in *write_in=(struct fuse_write_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) write_in->fh;

    if (openfile) {
	struct inode_s *inode=openfile->inode;
	char *buffer=(char *) (request->buffer + sizeof(struct fuse_write_in));
	uint64_t lock_owner=(write_in->flags & FUSE_WRITE_LOCKOWNER) ? write_in->lock_owner : 0;

	(* inode->fs->type.nondir.write) (openfile, request, buffer, write_in->size, write_in->offset, write_in->flags, lock_owner);

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_flush(struct fuse_request_s *request)
{
    struct fuse_flush_in *flush_in=(struct fuse_flush_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) flush_in->fh;

    if (openfile) {
	struct inode_s *inode=openfile->inode;

	(* inode->fs->type.nondir.flush) (openfile, request, flush_in->lock_owner);

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_fsync(struct fuse_request_s *request)
{
    struct fuse_fsync_in *fsync_in=(struct fuse_fsync_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) fsync_in->fh;

    if (openfile) {
	struct inode_s *inode=openfile->inode;

	(* inode->fs->type.nondir.fsync) (openfile, request, fsync_in->fsync_flags & 1);

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_release(struct fuse_request_s *request)
{
    struct fuse_release_in *release_in=(struct fuse_release_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) release_in->fh;

    if (openfile) {
	struct inode_s *inode=openfile->inode;
	uint64_t lock_owner=(release_in->release_flags & FUSE_RELEASE_FLOCK_UNLOCK) ? release_in->lock_owner : 0;

	(* inode->fs->type.nondir.release) (openfile, request, release_in->release_flags, lock_owner);

	free(openfile);
	openfile=NULL;

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_create(struct fuse_request_s *request)
{
    uint64_t ino=request->ino;
    struct inode_s *inode=find_inode(ino);
    struct service_context_s *context=get_service_context(request->interface);

    if (inode) {
	struct fuse_create_in *create_in=(struct fuse_create_in *) request->buffer;
	char *name=(char *) (request->buffer + sizeof(struct fuse_create_in));
	unsigned int len=request->size - sizeof(struct fuse_create_in);
	struct fuse_openfile_s *openfile=NULL;

	openfile=malloc(sizeof(struct fuse_openfile_s));

	if (openfile) {

	    memset(openfile, 0, sizeof(struct fuse_openfile_s));

	    openfile->context=context;
	    openfile->inode=inode;
	    openfile->error=0;
	    openfile->flock=0;

	    (* inode->fs->type.dir.create)(openfile, request, name, len, create_in->flags, create_in->mode, create_in->umask);

	    if (openfile->error>0) {

		/* subcall has send a reply to VFS already, here only free */

		free(openfile);
		openfile=NULL;

	    }

	} else {

	    reply_VFS_error(request, ENOMEM);

	}

    } else {

	reply_VFS_error(request, ENOENT);

    }

}

static signed char skip_file_default(struct fuse_opendir_s *opendir, struct inode_s *inode)
{
    return (signed char) check_entry_special(inode); /* by default do not include special files in readdir in virtual maps etc */
}

void _fuse_fs_opendir(struct service_context_s *context, struct inode_s *inode, struct fuse_request_s *request, struct fuse_open_in *open_in)
{
    struct fuse_opendir_s *opendir=NULL;

    opendir=malloc(sizeof(struct fuse_opendir_s));

    if (opendir) {

	memset(opendir, 0, sizeof(struct fuse_opendir_s));

	opendir->context=context;
	opendir->inode=inode;
	opendir->entry=NULL;
	opendir->mode=0;
	opendir->count_created=0;
	opendir->count_found=0;
	opendir->error=0;
	opendir->readdir=inode->fs->type.dir.readdir;
	opendir->readdirplus=inode->fs->type.dir.readdirplus;
	opendir->releasedir=inode->fs->type.dir.releasedir;
	opendir->fsyncdir=inode->fs->type.dir.fsyncdir;
	opendir->data=NULL;

	opendir->skip_file=skip_file_default;

	//logoutput_info("_fuse_fs_opendir: fs defined %s", (inode->fs) ? "yes" : "no");
	//if (inode->fs) logoutput_info("_fuse_fs_opendir: opendir defined %s", (inode->fs->type.dir.opendir) ? "yes" : "no");

	(* inode->fs->type.dir.opendir)(opendir, request, open_in->flags);

	if (opendir->error>0) {

	    /* subcall has send a reply to VFS already, here only free */

	    free(opendir);
	    opendir=NULL;

	}

    } else {

	reply_VFS_error(request, ENOMEM);

    }

}

void fuse_fs_opendir(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;

    logoutput("fuse_fs_opendir");

    if (ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;
	struct fuse_open_in *open_in=(struct fuse_open_in *) request->buffer;

	_fuse_fs_opendir(context, inode, request, open_in);

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {
	    struct fuse_open_in *open_in=(struct fuse_open_in *) request->buffer;

	    _fuse_fs_opendir(context, inode, request, open_in);

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_readdir(struct fuse_request_s *request)
{
    struct fuse_read_in *read_in=(struct fuse_read_in *) request->buffer;
    struct fuse_opendir_s *opendir=(struct fuse_opendir_s *) (uintptr_t) read_in->fh;

    logoutput("fuse_fs_readdir");

    if (opendir) {
	struct inode_s *inode=opendir->inode;

	(* opendir->readdir)(opendir, request, read_in->size, read_in->offset);

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_readdirplus(struct fuse_request_s *request)
{
    struct fuse_read_in *read_in=(struct fuse_read_in *) request->buffer;
    struct fuse_opendir_s *opendir=(struct fuse_opendir_s *) (uintptr_t) read_in->fh;

    logoutput("fuse_fs_readdirplus");

    if (opendir) {
	struct inode_s *inode=opendir->inode;

	(* opendir->readdirplus)(opendir, request, read_in->size, read_in->offset);

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_releasedir(struct fuse_request_s *request)
{
    struct fuse_release_in *release_in=(struct fuse_release_in *) request->buffer;
    struct fuse_opendir_s *opendir=(struct fuse_opendir_s *) (uintptr_t) release_in->fh;

    logoutput("fuse_fs_releasedir");

    if (opendir) {
	struct inode_s *inode=opendir->inode;

	(* opendir->releasedir)(opendir, request);

	free(opendir);
	opendir=NULL;
	release_in->fh=0;

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_fsyncdir(struct fuse_request_s *request)
{
    struct fuse_fsync_in *fsync_in=(struct fuse_fsync_in *) request->buffer;
    struct fuse_opendir_s *opendir=(struct fuse_opendir_s *) (uintptr_t) fsync_in->fh;

    if (opendir) {
	struct inode_s *inode=opendir->inode;

	(* opendir->fsyncdir)(opendir, request, fsync_in->fsync_flags & 1);

    } else {

	reply_VFS_error(request, EIO);

    }

}

/*
    get information about a lock
    if this is the case it returns the same lock with type F_UNLCK
    used for posix locks
*/

void fuse_fs_getlock(struct fuse_request_s *request)
{
    struct fuse_lk_in *lk_in=(struct fuse_lk_in *) request->buffer;

    if (lk_in->fh>0) {
	struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) lk_in->fh;

	if (openfile) {
	    struct inode_s *inode=openfile->inode;
	    struct flock flock;

	    flock.l_type=lk_in->lk.type;
	    flock.l_whence=SEEK_SET;
	    flock.l_start=lk_in->lk.start;
	    flock.l_len=(lk_in->lk.end==OFFSET_MAX) ? 0 : lk_in->lk.end - lk_in->lk.start + 1;
	    flock.l_pid=lk_in->lk.pid;

	    (* inode->fs->type.nondir.getlock) (openfile, request, &flock);

	} else {

	    reply_VFS_error(request, EIO);

	}

    } else {

	reply_VFS_error(request, EIO);

    }
}

static void _fuse_fs_flock_lock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct fuse_lk_in *lk_in, unsigned char type)
{
    struct inode_s *inode=openfile->inode;

    switch (lk_in->lk.type) {
	case F_RDLCK:
	    type|=LOCK_SH;
	    break;
	case F_WRLCK:
	    type|=LOCK_EX;
	    break;
	case F_UNLCK:
	    type|=LOCK_UN;
	    break;
    }

    (* inode->fs->type.nondir.flock) (openfile, request, type);

}

static void _fuse_fs_posix_lock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct fuse_lk_in *lk_in)
{
    struct inode_s *inode=openfile->inode;
    struct flock flock;

    flock.l_type=lk_in->lk.type;
    flock.l_whence=SEEK_SET;
    flock.l_start=lk_in->lk.start;
    flock.l_len=(lk_in->lk.end==OFFSET_MAX) ? 0 : lk_in->lk.end - lk_in->lk.start + 1;
    flock.l_pid=lk_in->lk.pid;

    (* inode->fs->type.nondir.setlock) (openfile, request, &flock);

}

static void _fuse_fs_posix_lock_wait(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct fuse_lk_in *lk_in)
{
    struct inode_s *inode=openfile->inode;
    struct flock flock;

    flock.l_type=lk_in->lk.type;
    flock.l_whence=SEEK_SET;
    flock.l_start=lk_in->lk.start;
    flock.l_len=(lk_in->lk.end==OFFSET_MAX) ? 0 : lk_in->lk.end - lk_in->lk.start + 1;
    flock.l_pid=lk_in->lk.pid;

    (* inode->fs->type.nondir.setlockw) (openfile, request, &flock);

}

/*
    generic function to set a lock
    it's called to set a posix lock and to set a flock lock
    depending in the presence of FUSE_LK_FLOCK in the flags
    this function is used when both locks are used (set in the init phase)
*/

void fuse_fs_lock(struct fuse_request_s *request)
{
    struct fuse_lk_in *lk_in=(struct fuse_lk_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) lk_in->fh;

    if (openfile) {
	struct inode_s *inode=openfile->inode;

	if (lk_in->lk_flags & FUSE_LK_FLOCK) {

	    _fuse_fs_flock_lock(openfile, request, lk_in, LOCK_NB);

	} else {

	    _fuse_fs_posix_lock(openfile, request, lk_in);

	}

    } else {

	reply_VFS_error(request, EIO);

    }

}

/*
    generic function to set a lock and wait for a release
    it's called to set a posix lock and to set a flock lock
    depending in the presence of FUSE_LK_FLOCK in the flags
    this function is used when both locks are used (set in the init phase)
*/

void fuse_fs_lock_wait(struct fuse_request_s *request)
{
    struct fuse_lk_in *lk_in=(struct fuse_lk_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) lk_in->fh;

    if (openfile) {
	struct inode_s *inode=openfile->inode;

	if (lk_in->lk_flags & FUSE_LK_FLOCK) {

	    _fuse_fs_flock_lock(openfile, request, lk_in, 0);

	} else {

	    _fuse_fs_posix_lock_wait(openfile, request, lk_in);

	}

    } else {

	reply_VFS_error(request, EIO);

    }

}

/*
    function to set a posix lock
    called when only posix locks are used
    (so every lock is a posix lock)
*/

void fuse_fs_posix_lock(struct fuse_request_s *request)
{
    struct fuse_lk_in *lk_in=(struct fuse_lk_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) lk_in->fh;

    if (openfile) {

	_fuse_fs_posix_lock(openfile, request, lk_in);

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_posix_lock_wait(struct fuse_request_s *request)
{
    struct fuse_lk_in *lk_in=(struct fuse_lk_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) lk_in->fh;

    if (openfile) {

	_fuse_fs_posix_lock_wait(openfile, request, lk_in);

    } else {

	reply_VFS_error(request, EIO);

    }

}

/*
    function to set a flock lock
    called when only flock locks are used
    (so every lock is a flock lock)
*/

void fuse_fs_flock_lock(struct fuse_request_s *request)
{
    struct fuse_lk_in *lk_in=(struct fuse_lk_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) lk_in->fh;

    if (openfile) {

	_fuse_fs_flock_lock(openfile, request, lk_in, LOCK_NB);

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_flock_lock_wait(struct fuse_request_s *request)
{
    struct fuse_lk_in *lk_in=(struct fuse_lk_in *) request->buffer;
    struct fuse_openfile_s *openfile=(struct fuse_openfile_s *) (uintptr_t) lk_in->fh;

    if (openfile) {

	_fuse_fs_flock_lock(openfile, request, lk_in, 0);

    } else {

	reply_VFS_error(request, EIO);

    }

}

void fuse_fs_setxattr(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;

    reply_VFS_error(request, ENODATA);
    return;

    if (ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;
	struct fuse_setxattr_in *setxattr_in=(struct fuse_setxattr_in *) request->buffer;
	char *name=(char *) ((char *) setxattr_in + sizeof(struct fuse_setxattr_in));
	char *value=(char *) (name + strlen(name) + 1);

	(* inode->fs->setxattr)(context, request, inode, name, value, setxattr_in->size, setxattr_in->flags);

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {
	    struct fuse_setxattr_in *setxattr_in=(struct fuse_setxattr_in *) request->buffer;
	    char *name=(char *) ((char *) setxattr_in + sizeof(struct fuse_setxattr_in));
	    char *value=(char *) (name + strlen(name) + 1);

	    (* inode->fs->setxattr)(context, request, inode, name, value, setxattr_in->size, setxattr_in->flags);

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_getxattr(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;

    reply_VFS_error(request, ENODATA);
    return;

    if (ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;
	struct fuse_getxattr_in *getxattr_in=(struct fuse_getxattr_in *) request->buffer;
	char *name=(char *) ((char *) getxattr_in + sizeof(struct fuse_getxattr_in));

	logoutput("fuse_fs_getxattr: root ino %li name %s", ino, name);

	(* inode->fs->getxattr)(context, request, inode, name, getxattr_in->size);

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {
	    struct fuse_getxattr_in *getxattr_in=(struct fuse_getxattr_in *) request->buffer;
	    char *name=(char *) ((char *) getxattr_in + sizeof(struct fuse_getxattr_in));
	    struct entry_s *entry=inode->alias;

	    logoutput("fuse_fs_getxattr: ino %li entry %.*s name %s", ino, entry->name.len, entry->name.name, name);

	    (* inode->fs->getxattr)(context, request, inode, name, getxattr_in->size);

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_listxattr(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;

    reply_VFS_error(request, ENODATA);
    return;

    if (ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;
	struct fuse_getxattr_in *getxattr_in=(struct fuse_getxattr_in *) request->buffer;

	(* inode->fs->listxattr)(context, request, inode, getxattr_in->size);

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {
	    struct fuse_getxattr_in *getxattr_in=(struct fuse_getxattr_in *) request->buffer;

	    (* inode->fs->listxattr)(context, request, inode, getxattr_in->size);

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_removexattr(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;

    if (ino==FUSE_ROOT_ID) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;
	char *name=(char *) request->buffer;

	(* inode->fs->removexattr)(context, request, inode, name);

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {
	    char *name=(char *) request->buffer;

	    (* inode->fs->removexattr)(context, request, inode, name);

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_statfs(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    uint64_t ino=request->ino;

    logoutput("fuse_fs_statfs: ino %li", ino);

    if (ino==FUSE_ROOT_ID || ino==0) {
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;

	(* inode->fs->statfs)(context, request, inode);

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {

	    (* inode->fs->statfs)(context, request, inode);

	} else {

	    reply_VFS_error(request, ENOENT);

	}

    }

}

void fuse_fs_interrupt(struct fuse_request_s *request)
{
    struct service_context_s *context=get_service_context(request->interface);
    struct fuse_interrupt_in *intr_in=(struct fuse_interrupt_in *) request->buffer;

    logoutput("INTERRUPT (thread %i) unique %lu", (int) gettid(), intr_in->unique);
    set_request_interrupted(request->interface->ptr, intr_in->unique);

}

void fuse_fs_init(struct fuse_request_s *request)
{
    struct fuse_init_in *init_in=(struct fuse_init_in *) request->buffer;
    struct service_context_s *context=get_service_context(request->interface);

    logoutput("INIT (thread %i)", (int) gettid());

    logoutput("fuse_fs_init: kernel proto %i:%i", init_in->major, init_in->minor);
    logoutput("fuse_fs_init: library proto %i:%i", FUSE_KERNEL_VERSION, FUSE_KERNEL_MINOR_VERSION);
    logoutput("fuse_fs_init: kernel max readahead %i", init_in->max_readahead);

    if (init_in->major<7) {

	logoutput("fuse_fs_init: unsupported kernel protocol version");
	reply_VFS_error(request, EPROTO);

	return;

    } else {
	struct fuse_init_out init_out;

	memset(&init_out, 0, sizeof(struct fuse_init_out));

	init_out.major=FUSE_KERNEL_VERSION;
	init_out.minor=FUSE_KERNEL_MINOR_VERSION;
	init_out.flags=0;

	if (init_in->major>7) {

	    reply_VFS_data(request, (char *) &init_out, sizeof(init_out));
	    return;

	} else {

	    /*
		TODO: make flags and options depend on the context: is it a network fs or not ?
	    */

#ifdef FUSE_ASYNC_READ

	    if (init_in->flags & FUSE_ASYNC_READ) {
		int option=0;

		if (get_interface_option_integer(request->interface, "async-read", &option)>0 && option==1) {

		    init_out.flags |= FUSE_ASYNC_READ;
		    logoutput("fuse_fs_init: kernel supports asynchronous read requests (enable)");

		} else {

		    logoutput("fuse_fs_init: kernel supports asynchronous read requests (disable)");

		}

	    }

#endif

#ifdef FUSE_POSIX_LOCKS

	    if (init_in->flags & FUSE_POSIX_LOCKS) {
		int option=0;

		if (get_interface_option_integer(request->interface, "posix-locks", &option)>0 && option==1) {

		    init_out.flags |= FUSE_POSIX_LOCKS;
		    logoutput("fuse_fs_init: kernel supports posix locks (enable)");

		} else {

		    logoutput("fuse_fs_init: kernel supports posix locks (disable)");

		}

	    }

#endif

#ifdef FUSE_FILE_OPS

	    if (init_in->flags & FUSE_FILE_OPS) {
		int option=0;

		if (get_interface_option_integer(request->interface, "file-ops", &option)>0 && option==1) {

		    init_out.flags |= FUSE_FILE_OPS;
		    logoutput("fuse_fs_init: kernel supports handles for fstat and fsetattr (enable)");

		} else {

		    logoutput("fuse_fs_init: kernel supports handles for fstat and fsetattr (disable)");

		}

	    }

#endif

#ifdef FUSE_ATOMIC_O_TRUNC

	    if (init_in->flags & FUSE_ATOMIC_O_TRUNC) {
		int option=0;

		if (get_interface_option_integer(request->interface, "atomic-o-trunc", &option)>0 && option==1)  {

		    logoutput("fuse_fs_init: kernel supports filesystem handling of O_TRUNC (enable)");
		    init_out.flags |= FUSE_ATOMIC_O_TRUNC;

		} else {

		    logoutput("fuse_fs_init: kernel supports filesystem handling of O_TRUNC (disable)");

		}

	    }

#endif

#ifdef FUSE_EXPORT_SUPPORT

	    if (init_in->flags & FUSE_EXPORT_SUPPORT) {
		int option=0;

		if (get_interface_option_integer(request->interface, "export_support", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports filesystem lookups of . and .. (enable)");
		    init_out.flags |= FUSE_EXPORT_SUPPORT;

		} else {

		    logoutput("fuse_fs_init: kernel supports filesystem lookups of . and .. (disable)");

		}

	    }

#endif

#ifdef FUSE_BIG_WRITES

	    if (init_in->flags & FUSE_BIG_WRITES) {
		int option=0;

		if (get_interface_option_integer(request->interface, "big-writes", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports writing of more than 4Kb (enable)");
		    init_out.flags |= FUSE_BIG_WRITES;

		} else {

		    logoutput("fuse_fs_init: kernel supports writing of more than 4Kb (disable)");

		}

	    }

#endif

#ifdef FUSE_DONT_MASK

	    if (init_in->flags & FUSE_DONT_MASK) {
		int option=0;

		if (get_interface_option_integer(request->interface, "dont-mask", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports masking kernelspace (enable)");
		    init_out.flags |= FUSE_DONT_MASK;

		} else {

		    logoutput("fuse_fs_init: kernel supports masking kernelspace (disable)");

		}

	    }

#endif

#ifdef FUSE_SPLICE_WRITE

	    if (init_in->flags & FUSE_SPLICE_WRITE) {
		int option=0;

		if (get_interface_option_integer(request->interface, "splice-write", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports splice writes (enable)");
		    init_out.flags |= FUSE_SPLICE_WRITE;

		} else {

		    logoutput("fuse_fs_init: kernel supports splice writes (disable)");

		}

	    }

#endif

#ifdef FUSE_SPLICE_MOVE

	    if (init_in->flags & FUSE_SPLICE_MOVE) {
		int option=0;

		if (get_interface_option_integer(request->interface, "splice-move", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports splice moves (enable)");
		    init_out.flags |= FUSE_SPLICE_MOVE;

		} else {

		    logoutput("fuse_fs_init: kernel supports splice moves (disable)");

		}

	    }

#endif

#ifdef FUSE_SPLICE_READ

	    if (init_in->flags & FUSE_SPLICE_READ) {
		int option=0;

		if (get_interface_option_integer(request->interface, "splice-read", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports splice reads (enable)");
		    init_out.flags |= FUSE_SPLICE_READ;

		} else {

		    logoutput("fuse_fs_init: kernel supports splice reads (disable)");

		}

	    }

#endif

#ifdef FUSE_FLOCK_LOCKS

	    if (init_in->flags & FUSE_FLOCK_LOCKS) {
		int option=0;

		if (get_interface_option_integer(request->interface, "flock-locks", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports BSD file locks (enable)");
		    init_out.flags |= FUSE_FLOCK_LOCKS;

		} else {

		    logoutput("fuse_fs_init: kernel supports BSD file locks (disable)");

		}

	    }

#endif

#ifdef FUSE_HAS_IOCTL_DIR

	    if (init_in->flags & FUSE_HAS_IOCTL_DIR) {
		int option=0;

		if (get_interface_option_integer(request->interface, "has-ioctl-dir", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports ioctl on directories (enable)");
		    init_out.flags |= FUSE_HAS_IOCTL_DIR;

		} else {

		    logoutput("fuse_fs_init: kernel supports ioctl on directories (disable)");

		}

	    }

#endif

#ifdef FUSE_AUTO_INVAL_DATA

	    if (init_in->flags & FUSE_AUTO_INVAL_DATA) {
		int option=0;

		if (get_interface_option_integer(request->interface, "auto-inval-data", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports automatic invalidate cached pages (enable)");
		    init_out.flags |= FUSE_AUTO_INVAL_DATA;

		} else {

		    logoutput("fuse_fs_init: kernel supports automatic invalidate cached pages (disable)");

		}

	    }

#endif

#ifdef FUSE_DO_READDIRPLUS

	    if (init_in->flags & FUSE_DO_READDIRPLUS) {
		int option=0;

		if (get_interface_option_integer(request->interface, "do-readdirplus", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports doing readdirplus in stead of readdir (enable)");
		    init_out.flags |= FUSE_DO_READDIRPLUS;

		} else {

		    logoutput("fuse_fs_init: kernel supports doing readdirplus in stead of readdir (disable)");

		}

	    }

#endif

#ifdef FUSE_READDIRPLUS_AUTO

	    if (init_in->flags & FUSE_READDIRPLUS_AUTO) {
		int option=0;

		if (get_interface_option_integer(request->interface, "readdirplus-auto", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports addaptive readdirplus (enable)");
		    init_out.flags |= FUSE_READDIRPLUS_AUTO;

		} else {

		    logoutput("fuse_fs_init: kernel supports addaptive readdirplus (disable)");

		}

	    }

#endif

#ifdef FUSE_ASYNC_DIO

	    if (init_in->flags & FUSE_ASYNC_DIO) {
		int option=0;

		if (get_interface_option_integer(request->interface, "async-dio", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports asynchronous direct I/O (enable)");
		    init_out.flags |= FUSE_ASYNC_DIO;

		} else {

		    logoutput("fuse_fs_init: kernel supports asynchronous direct I/O (disable)");

		}

	    }

#endif

#ifdef FUSE_WRITEBACK_CACHE

	    if (init_in->flags & FUSE_WRITEBACK_CACHE) {
		int option=0;

		if (get_interface_option_integer(request->interface, "writeback-cache", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports writeback cache for buffered writes (enable)");
		    init_out.flags |= FUSE_WRITEBACK_CACHE;

		} else {

		    logoutput("fuse_fs_init: kernel supports writeback cache for buffered writes (disable)");

		}

	    }

#endif

#ifdef FUSE_NO_OPEN_SUPPORT

	    if (init_in->flags & FUSE_NO_OPEN_SUPPORT) {
		int option=0;

		if (get_interface_option_integer(request->interface, "no-open-support", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports zero-message open (enable)");
		    init_out.flags |= FUSE_NO_OPEN_SUPPORT;

		} else {

		    logoutput("fuse_fs_init: kernel supports zero-message open (disable)");

		}

	    } else {

		logoutput("fuse_fs_init: kernel does not support zero-message open");

	    }

#else

	    logoutput("fuse_fs_init: no support for zero-message open");

#endif

#ifdef FUSE_PARALLEL_DIROPS

	    if (init_in->flags & FUSE_PARALLEL_DIROPS) {
		int option=0;

		if (get_interface_option_integer(request->interface, "parallel-dirops", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports parallel dir ops (enable)");
		    init_out.flags |= FUSE_PARALLEL_DIROPS;

		} else {

		    logoutput("fuse_fs_init: kernel supports parallel dir ops (disable)");

		}

	    } else {

		logoutput("fuse_fs_init: kernel does not support parallel dir ops");

	    }

#else

	    logoutput("fuse_fs_init: no support for parallel dir ops");

#endif

#ifdef FUSE_POSIX_ACL

	    if (init_in->flags & FUSE_POSIX_ACL) {
		int option=0;

		if (get_interface_option_integer(request->interface, "posix-acl", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports enable/disable posix acls (enable)");
		    init_out.flags |= FUSE_POSIX_ACL;

		} else {

		    logoutput("fuse_fs_init: kernel supports enable/disable posix acls (disable)");

		}

	    } else {

		logoutput("fuse_fs_init: kernel does not support enable/disable posix acls");

	    }

#else

	    logoutput("fuse_fs_init: no support for enable/disable posix acls");

#endif

#ifdef FUSE_DO_FSNOTIFY

	    if (init_in->flags & FUSE_DO_FSNOTIFY) {
		int option=0;

		if (get_interface_option_integer(request->interface, "fsnotify", &option)>0 && option==1) {

		    logoutput("fuse_fs_init: kernel supports enable/disable fsnotify (enable)");
		    init_out.flags |= FUSE_DO_FSNOTIFY;

		} else {

		    logoutput("fuse_fs_init: kernel supports enable/disable fsnotify (disable)");

		}

	    } else {

		logoutput("fuse_fs_init: kernel does not support enable/disable fsnotify");

	    }

#else

	    logoutput("fuse_fs_init: no support for enable/disable fsnotify");

#endif



	    init_out.max_readahead = init_in->max_readahead;
	    init_out.max_write = 4096; /* 4K */
	    init_out.max_background=(1 << 16) - 1;
	    init_out.congestion_threshold=(3 * init_out.max_background) / 4;

	    reply_VFS_data(request, (char *) &init_out, sizeof(init_out));

	    /*
		adjust various callbacks
		- flock or posix locks
	    */

	    if ((init_out.flags & FUSE_FLOCK_LOCKS) && !(init_out.flags & FUSE_POSIX_LOCKS)) {

		/* only flocks */

		register_fuse_function(request->interface->ptr, FUSE_SETLK, fuse_fs_flock_lock);
		register_fuse_function(request->interface->ptr, FUSE_SETLKW, fuse_fs_flock_lock_wait);

	    } else if (!(init_out.flags & FUSE_FLOCK_LOCKS) && (init_out.flags & FUSE_POSIX_LOCKS)) {

		/* only posix locks */

		register_fuse_function(request->interface->ptr, FUSE_SETLK, fuse_fs_posix_lock);
		register_fuse_function(request->interface->ptr, FUSE_SETLKW, fuse_fs_posix_lock_wait);

	    }

	    if (!(init_out.flags & FUSE_DONT_MASK)) {

		/* do not apply mask to permissions: kernel has done it already */

		disable_masking_userspace(request->interface->ptr);

	    }

	}

    }

}

void fuse_fs_destroy (struct fuse_request_s *request)
{
    logoutput("DESTROY (thread %i)", (int) gettid());
}

#ifdef FUSE_DO_FSNOTIFY

void fuse_fs_fsnotify(struct fuse_request_s *request)
{
    uint64_t ino=request->ino;
    struct fuse_fsnotify_in *fsnotify_in=(struct fuse_fsnotify_in *) request->buffer;

    if (ino==FUSE_ROOT_ID) {
	struct service_context_s *context=get_service_context(request->interface);
	struct workspace_mount_s *workspace=context->workspace;
	struct inode_s *inode=&workspace->rootinode;

	logoutput("FSNOTIFY: watch with mask %i on %s", fsnotify_in->mask, inode->alias->name.name);

	(* inode->fs->type.dir.fsnotify)(context, request, inode, fsnotify_in->mask);

    } else {
	struct inode_s *inode=find_inode(ino);

	if (inode) {

	    if (S_ISDIR(inode->mode)) {
		struct service_context_s *context=get_service_context(request->interface);

		logoutput("FSNOTIFY: watch with mask %i on %s", fsnotify_in->mask, inode->alias->name.name);

		(* inode->fs->type.dir.fsnotify)(context, request, inode, fsnotify_in->mask);

	    } else {

		reply_VFS_error(request, 0);

	    }

	}

    }

}

#endif

void register_fuse_functions(struct context_interface_s *interface)
{
    void *ptr=interface->ptr;

    register_fuse_function(ptr, FUSE_INIT, fuse_fs_init);
    register_fuse_function(ptr, FUSE_DESTROY, fuse_fs_destroy);

    register_fuse_function(ptr, FUSE_LOOKUP, fuse_fs_lookup);
    register_fuse_function(ptr, FUSE_FORGET, fuse_fs_forget);
    register_fuse_function(ptr, FUSE_BATCH_FORGET, fuse_fs_forget_multi);

    register_fuse_function(ptr, FUSE_GETATTR, fuse_fs_getattr);
    register_fuse_function(ptr, FUSE_SETATTR, fuse_fs_setattr);

    register_fuse_function(ptr, FUSE_MKDIR, fuse_fs_mkdir);
    register_fuse_function(ptr, FUSE_MKNOD, fuse_fs_mknod);
    register_fuse_function(ptr, FUSE_SYMLINK, fuse_fs_symlink);

    register_fuse_function(ptr, FUSE_RMDIR, fuse_fs_rmdir);
    register_fuse_function(ptr, FUSE_UNLINK, fuse_fs_unlink);

    register_fuse_function(ptr, FUSE_READLINK, fuse_fs_readlink);
    register_fuse_function(ptr, FUSE_RENAME, fuse_fs_rename);
    register_fuse_function(ptr, FUSE_LINK, fuse_fs_link);

    register_fuse_function(ptr, FUSE_OPENDIR, fuse_fs_opendir);
    register_fuse_function(ptr, FUSE_READDIR, fuse_fs_readdir);
    register_fuse_function(ptr, FUSE_READDIRPLUS, fuse_fs_readdirplus);
    register_fuse_function(ptr, FUSE_RELEASEDIR, fuse_fs_releasedir);
    register_fuse_function(ptr, FUSE_FSYNCDIR, fuse_fs_fsyncdir);

    register_fuse_function(ptr, FUSE_CREATE, fuse_fs_create);
    register_fuse_function(ptr, FUSE_OPEN, fuse_fs_open);
    register_fuse_function(ptr, FUSE_READ, fuse_fs_read);
    register_fuse_function(ptr, FUSE_WRITE, fuse_fs_write);
    register_fuse_function(ptr, FUSE_FSYNC, fuse_fs_fsync);
    register_fuse_function(ptr, FUSE_FLUSH, fuse_fs_flush);
    register_fuse_function(ptr, FUSE_RELEASE, fuse_fs_release);

    register_fuse_function(ptr, FUSE_GETLK, fuse_fs_getlock);
    register_fuse_function(ptr, FUSE_SETLK, fuse_fs_lock);
    register_fuse_function(ptr, FUSE_SETLKW, fuse_fs_lock_wait);

    register_fuse_function(ptr, FUSE_STATFS, fuse_fs_statfs);

    register_fuse_function(ptr, FUSE_LISTXATTR, fuse_fs_listxattr);
    register_fuse_function(ptr, FUSE_GETXATTR, fuse_fs_getxattr);
    register_fuse_function(ptr, FUSE_SETXATTR, fuse_fs_setxattr);
    register_fuse_function(ptr, FUSE_REMOVEXATTR, fuse_fs_removexattr);

    register_fuse_function(ptr, FUSE_INTERRUPT, fuse_fs_interrupt);

#ifdef FUSE_DO_FSNOTIFY
    register_fuse_function(ptr, FUSE_FSNOTIFY, fuse_fs_fsnotify);
#endif

};
