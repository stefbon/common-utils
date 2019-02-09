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

#ifndef SB_COMMON_UTILS_FUSE_UTILS_H
#define SB_COMMON_UTILS_FUSE_UTILS_H

struct create_entry_s {
    struct name_s			*name;
    union {
	struct entry_s			*parent;
	struct directory_s 		*directory;
	struct fuse_opendir_s		*opendir;
    } tree;
    struct service_context_s		*context;
    struct entry_s 			*(*cb_create_entry)(struct entry_s *p, struct name_s *n);
    struct inode_s			*(*cb_create_inode)(unsigned int size);
    struct entry_s			*(*cb_insert_entry)(struct directory_s *d, struct entry_s *e, unsigned int f, unsigned int *error);
    void				(*cb_created)(struct entry_s *e, struct create_entry_s *ce);
    void				(*cb_found)(struct entry_s *e, struct create_entry_s *ce);
    void				(*cb_error)(struct entry_s *p, struct name_s *n, struct create_entry_s *ce, unsigned int e);
    unsigned int			(*cb_cache_size)(struct create_entry_s *ce);
    void				(*cb_cache_created)(struct entry_s *e, struct create_entry_s *ce);
    void				(*cb_cache_found)(struct entry_s *e, struct create_entry_s *ce);
    void				(*cb_adjust_pathmax)(struct create_entry_s *ce);
    void				(*cb_context_created)(struct create_entry_s *ce, struct entry_s *e);
    void				(*cb_context_found)(struct create_entry_s *ce, struct entry_s *e);
    struct directory_s 			*(* get_directory)(struct create_entry_s *ce);
    unsigned int			pathlen;
    union {
	struct stat			st;
	struct inode_link_s 		link;
    } cache;
    unsigned int			flags;
    void				*ptr;
    unsigned int			error;
};

/* prototypes */

struct entry_s *find_entry(struct entry_s *parent, struct name_s *xname, unsigned int *error);
void remove_entry(struct entry_s *entry, unsigned int *error);
struct entry_s *insert_entry(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags);

struct entry_s *find_entry_batch(struct directory_s *directory, struct name_s *xname, unsigned int *error);
void remove_entry_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error);
struct entry_s *insert_entry_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags);

struct directory_s *_remove_directory(struct inode_s *inode, unsigned int *error);
struct directory_s *remove_directory(struct inode_s *inode, unsigned int *error);

void init_directory_calls();
struct directory_s *get_dummy_directory();
struct pathcalls_s *get_pathcalls(struct directory_s *d);

void init_create_entry(struct create_entry_s *ce, struct name_s *n, struct entry_s *p, struct directory_s *d, struct fuse_opendir_s *fo, struct service_context_s *c, struct stat *st, void *ptr);
struct entry_s *create_entry_extended(struct create_entry_s *ce);
struct entry_s *create_entry_extended_batch(struct create_entry_s *ce);

void clear_directory(struct context_interface_s *i, struct directory_s *directory);

struct entry_s *walk_fuse_fs(struct entry_s *parent, char *path);

#endif
