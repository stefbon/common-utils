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

#ifndef SB_COMMON_UTILS_DIRECTORY_MANAGEMENT_H
#define SB_COMMON_UTILS_DIRECTORY_MANAGEMENT_H

#include "simple-locking.h"

#define _DIRECTORY_FLAG_REMOVE					1
#define _DIRECTORY_FLAG_DUMMY					2

#define _DIRECTORY_LOCK_READ					1
#define _DIRECTORY_LOCK_PREEXCL					2
#define _DIRECTORY_LOCK_EXCL					3

struct directory_s;

struct pathcalls_s {
    void 				*cache;
    int 				(* get_path)(struct directory_s *directory, void *ptr);
    pthread_mutex_t			mutex;
    void				(* free)(struct pathcalls_s *p);
};

struct dops_s {
    struct entry_s 			*(*find_entry)(struct entry_s *parent, struct name_s *xname, unsigned int *error);
    void 				(*remove_entry)(struct entry_s *entry, unsigned int *error);
    struct entry_s 			*(*insert_entry)(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags);
    struct directory_s			*(*create_directory)(struct inode_s *inode, unsigned int *error);
    struct directory_s			*(*remove_directory)(struct inode_s *inode, unsigned int *error);
    struct entry_s 			*(*find_entry_batch)(struct directory_s *directory, struct name_s *xname, unsigned int *error);
    void 				(*remove_entry_batch)(struct directory_s *directory, struct entry_s *entry, unsigned int *error);
    struct entry_s 			*(*insert_entry_batch)(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags);
    struct simple_lock_s 		*(* create_rlock)(struct inode_s *inode);
    struct simple_lock_s 		*(* create_wlock)(struct inode_s *inode);
    int					(* rlock)(struct inode_s *inode, struct simple_lock_s *lock);
    int					(* wlock)(struct inode_s *inode, struct simple_lock_s *lock);
    int					(* lock)(struct inode_s *inode, struct simple_lock_s *lock);
    int					(* unlock)(struct inode_s *inode, struct simple_lock_s *lock);
    int					(* upgradelock)(struct inode_s *inode, struct simple_lock_s *lock);
    int					(* prelock)(struct inode_s *inode, struct simple_lock_s *lock);
    struct pathcalls_s 			*(*get_pathcalls)(struct inode_s *inode);
};

struct directory_s {
    unsigned char 			flags;
    struct timespec 			synctime;
    struct skiplist_struct		skiplist;
    struct inode_s 			*inode;
    struct directory_s			*next;
    struct directory_s			*prev;
    unsigned int			count;
    struct simple_locking_s		locking;
    struct entry_s			*first;
    struct entry_s			*last;
    struct dops_s 			*dops;
    struct inode_link_s			link;
    struct pathcalls_s			pathcalls;
};

int init_directory(struct directory_s *directory, unsigned int *error);
struct directory_s *_create_directory(struct inode_s *inode, void (* init_cb)(struct directory_s *directory), unsigned int *error);

int get_directory_link(struct inode_s *inode, struct inode_link_s *link);
struct directory_s *get_directory(struct inode_s *inode);

int get_inode_link_directory(struct inode_s *inode, struct inode_link_s *link);
void set_inode_link_directory(struct inode_s *inode, struct inode_link_s *link);

void _add_directory_hashtable(struct directory_s *directory);
void _remove_directory_hashtable(struct directory_s *directory);

void free_directory(struct directory_s *directory);
void destroy_directory(struct directory_s *directory);

void init_directory_readlock(struct directory_s *directory, struct simple_lock_s *lock);
void init_directory_writelock(struct directory_s *directory, struct simple_lock_s *lock);

struct simple_lock_s *_create_rlock_directory(struct directory_s *directory);
struct simple_lock_s *_create_wlock_directory(struct directory_s *directory);

int _rlock_directory(struct directory_s *directory, struct simple_lock_s *lock);
int _wlock_directory(struct directory_s *directory, struct simple_lock_s *lock);

int _lock_directory(struct directory_s *directory, struct simple_lock_s *lock);
int _unlock_directory(struct directory_s *directory, struct simple_lock_s *lock);
int _upgradelock_directory(struct directory_s *directory, struct simple_lock_s *lock);
int _prelock_directory(struct directory_s *directory, struct simple_lock_s *lock);

int lock_pathcalls(struct pathcalls_s *pathcalls);
int unlock_pathcalls(struct pathcalls_s *pathcalls);

unsigned int get_path_pathcalls(struct directory_s *directory, void *ptr);

int init_directory_hashtable();
void free_directory_hashtable();

#endif
