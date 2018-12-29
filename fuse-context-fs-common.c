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

#include "logging.h"
#include "pathinfo.h"
#include "main.h"
#include "beventloop.h"
#include "utils.h"

#include "fuse-dentry.h"
#include "fuse-directory.h"
#include "fuse-utils.h"

#include "fuse-fs.h"
#include "workspaces.h"
#include "workspace-context.h"

#include "fuse-fs-common.h"
#include "path-caching.h"

/* READ */

void service_fs_read(struct fuse_openfile_s *openfile, struct fuse_request_s *request, size_t size, off_t off, unsigned int flags, uint64_t lock_owner)
{
    logoutput("READ %s (thread %i)", openfile->context->name, (int) gettid());
    (* openfile->context->fs->read)(openfile, request, size, off, flags, lock_owner);
}

/* WRITE */

void service_fs_write(struct fuse_openfile_s *openfile, struct fuse_request_s *request, const char *buff, size_t size, off_t off, unsigned int flags, uint64_t lock_owner)
{
    logoutput("WRITE %s (thread %i)", openfile->context->name, (int) gettid());
    (* openfile->context->fs->write)(openfile, request, buff, size, off, flags, lock_owner);
}

/* FSYNC */

void service_fs_fsync(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned char datasync)
{
    logoutput("FSYNC %s (thread %i)", openfile->context->name, (int) gettid());
    (* openfile->context->fs->fsync)(openfile, request, datasync);
}

/* FLUSH */

void service_fs_flush(struct fuse_openfile_s *openfile, struct fuse_request_s *request, uint64_t lockowner)
{
    logoutput("FLUSH %s (thread %i) lockowner %li", openfile->context->name, (int) gettid(), (unsigned long int) lockowner);
    (* openfile->context->fs->flush)(openfile, request, lockowner);
}

/* FGETATTR */

void service_fs_fgetattr(struct fuse_openfile_s *openfile, struct fuse_request_s *request)
{
    logoutput("FGETATTR %s (thread %i)", openfile->context->name, (int) gettid());
    (* openfile->context->fs->fgetattr)(openfile, request);
}

/* FSETATTR */

void service_fs_fsetattr(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct stat *st, int set)
{
    logoutput("FSETATTR %s (thread %i)", openfile->context->name, (int) gettid());
    (* openfile->context->fs->fsetattr)(openfile, request, st, set);
}

/* RELEASE */

void service_fs_release(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned int flags, uint64_t lockowner)
{
    logoutput("RELEASE %s (thread %i) lockowner %li", openfile->context->name, (int) gettid(), (unsigned long int) lockowner);
    (* openfile->context->fs->release)(openfile, request, flags, lockowner);
}

/* GETLOCK (bytelock) */

void service_fs_getlock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock)
{
    logoutput("GETLOCK %s (thread %i)", openfile->context->name, (int) gettid());
    reply_VFS_error(request, ENOSYS);
    (* openfile->context->fs->getlock)(openfile, request, flock);
}

/* SETLOCK (bytelock) */

void service_fs_setlock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock)
{
    logoutput("SETLOCK %s (thread %i)", openfile->context->name, (int) gettid());
    reply_VFS_error(request, ENOSYS);
    (* openfile->context->fs->setlock)(openfile, request, flock);
}

/* SETLOCKW (bytelock) */

void service_fs_setlockw(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock)
{
    logoutput("SETLOCKW %s (thread %i)", openfile->context->name, (int) gettid());
    reply_VFS_error(request, ENOSYS);
    (* openfile->context->fs->setlockw)(openfile, request, flock);
}

/* FLOCK (filelock) */

void service_fs_flock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned char type)
{

    logoutput("FLOCK %s (thread %i) lock %i:%i", openfile->context->name, (int) gettid(), openfile->flock, type);
    reply_VFS_error(request, ENOSYS);

    /*
	type can be one of following:

	- LOCK_SH : shared lock
	- LOCK_EX : exlusive lock
	- LOCK_UN : remove current lock

	service fs has to deal with down/upgrades/release of locks
	previous lock is in openfile->flock
    */

    (* openfile->context->fs->flock)(openfile, request, type);

}

/* READDIR */

void service_fs_readdir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset)
{
    logoutput("READDIR %s (thread %i)", opendir->context->name, (int) gettid());
    (* opendir->context->fs->readdir)(opendir, request, size, offset);
}

/* READDIRPLUS */

void service_fs_readdirplus(struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset)
{
    logoutput("READDIRPLUS %s (thread %i)", opendir->context->name, (int) gettid());
    (* opendir->context->fs->readdirplus)(opendir, request, size, offset);
}

/* FSYNCDIR */

void service_fs_fsyncdir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned char datasync)
{
    logoutput("FSYNCDIR %s (thread %i)", opendir->context->name, (int) gettid());
    (* opendir->context->fs->fsyncdir)(opendir, request, datasync);
}

/* RELEASEDIR */

void service_fs_releasedir(struct fuse_opendir_s *opendir, struct fuse_request_s *request)
{
    struct directory_s *directory=get_directory(opendir->inode);

    logoutput("RELEASEDIR %s (thread %i)", opendir->context->name, (int) gettid());

    (* opendir->context->fs->releasedir)(opendir, request);

    if (directory) {
	struct pathcalls_s *pathcalls=&directory->pathcalls;

	lock_pathcalls(pathcalls);
	free_pathcache(pathcalls);
	unlock_pathcalls(pathcalls);

    }

}
