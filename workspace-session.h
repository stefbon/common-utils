/*
  2010, 2011, 2012, 2013, 2014, 2015 Stef Bon <stefbon@gmail.com>

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

#ifndef FS_WORKSPACE_SESSION_H
#define FS_WORKSPACE_SESSION_H

int initialize_fuse_users(unsigned int *error);
void free_fuse_users();

void add_fuse_user_hash(struct fuse_user_s *user);
void remove_fuse_user_hash(struct fuse_user_s *user);

struct fuse_user_s *add_fuse_user(uid_t uid, char *status, unsigned int *error);
void free_fuse_user(void *data);

struct fuse_user_s *lookup_fuse_user(uid_t uid);
struct fuse_user_s *get_next_fuse_user(void **index, unsigned int *hashvalue);

void lock_users_hash();
void unlock_users_hash();

unsigned char use_workspace_base(struct fuse_user_s *user, struct workspace_base_s *base);
char *get_mountpoint_workspace_base(struct fuse_user_s *user, struct workspace_base_s *base, struct pathinfo_s *pathinfo);

#endif
