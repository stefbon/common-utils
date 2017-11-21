/*
  2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017 Stef Bon <stefbon@gmail.com>

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

#ifndef FUSE_CONTEXT_FS_COMMON_H
#define FUSE_CONTEXT_FS_COMMON_H

// Prototypes

void service_fs_read(struct fuse_openfile_s *openfile, struct fuse_request_s *request, size_t size, off_t off, unsigned int flags, uint64_t lock_owner);
void service_fs_write(struct fuse_openfile_s *openfile, struct fuse_request_s *request, const char *buff, size_t size, off_t off, unsigned int flags, uint64_t lock_owner);
void service_fs_fsync(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned char datasync);
void service_fs_flush(struct fuse_openfile_s *openfile, struct fuse_request_s *request, uint64_t lockowner);
void service_fs_fgetattr(struct fuse_openfile_s *openfile, struct fuse_request_s *request);
void service_fs_fsetattr(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct stat *st, int set);
void service_fs_release(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned int flags, uint64_t lockowner);
void service_fs_getlock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock);
void service_fs_setlock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock);
void service_fs_setlockw(struct fuse_openfile_s *openfile, struct fuse_request_s *request, struct flock *flock);
void service_fs_flock(struct fuse_openfile_s *openfile, struct fuse_request_s *request, unsigned char type);
void service_fs_readdir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset);
void service_fs_readdirplus(struct fuse_opendir_s *opendir, struct fuse_request_s *request, size_t size, off_t offset);
void service_fs_fsyncdir(struct fuse_opendir_s *opendir, struct fuse_request_s *request, unsigned char datasync);
void service_fs_releasedir(struct fuse_opendir_s *opendir, struct fuse_request_s *request);

#endif
