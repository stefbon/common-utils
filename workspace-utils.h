/*
  2010, 2011, 2012, 2013, 2014 Stef Bon <stefbon@gmail.com>

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

#ifndef FS_WORKSPACE_UTILS_H
#define FS_WORKSPACE_UTILS_H

char *get_path_from_template(char *template, struct fuse_user_s *user, char *buff, size_t len0);
void read_workspace_files(char *path);
struct workspace_base_s *get_next_workspace_base(struct workspace_base_s *base);

int create_directory(struct pathinfo_s *pathinfo, mode_t mode, unsigned int *error);
unsigned char ismounted(char *path);
unsigned char user_is_groupmember(char *username, struct group *grp);

void free_workspaces_base();

#endif
