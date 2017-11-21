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

#ifndef _FUSE_ENTRY_UTILS_H
#define _FUSE_ENTRY_UTILS_H

struct entry_s *find_entry(struct entry_s *parent, struct name_s *xname, unsigned int *error);
void remove_entry(struct entry_s *entry, unsigned int *error);
struct entry_s *insert_entry(struct entry_s *entry, unsigned int *error, unsigned short flags);

struct entry_s *find_entry_batch(struct directory_s *directory, struct name_s *xname, unsigned int *error);
void remove_entry_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error);
struct entry_s *insert_entry_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags);

struct directory_s *_remove_directory(struct inode_s *inode, unsigned int *error);
struct directory_s *remove_directory(struct inode_s *inode, unsigned int *error);

int lock_directory_read(struct inode_s *inode);
int lock_directory_excl(struct inode_s *inode);
void unlock_directory_read(struct inode_s *inode);
void unlock_directory_excl(struct inode_s *inode);

void init_directory_calls();
struct directory_s *get_dummy_directory();

struct pathcalls_s *get_pathcalls(struct inode_s *inode);

struct entry_s *create_entry_extended( struct entry_s *parent,
					    struct name_s *xname,
					    void (* cb_created)(struct entry_s *entry, void *data),
					    void (* cb_found)(struct entry_s *entry, void *data),
					    void (* cb_error)(struct entry_s *parent, struct name_s *xname, void *data, unsigned int error),
					    void *data);

struct entry_s *create_entry_extended_batch(struct directory_s *directory,
					    struct name_s *xname, 
					    void (* cb_created)(struct entry_s *entry, void *data),
					    void (* cb_found)(struct entry_s *entry, void *data),
					    void (* cb_error)(struct entry_s *parent, struct name_s *xname, void *data, unsigned int error),
					    void *data);

void walk_directory(struct directory_s *directory, void (*cb_entry) (struct directory_s *directory, struct entry_s *entry), void (*cb_directory) (struct directory_s **directory, unsigned char when));
void clear_directory(struct directory_s *directory, void (*cb_entry)(struct entry_s *e, void *ptr), void *ptr);

struct entry_s *walk_fuse_fs(struct entry_s *parent, char *path);

#endif
