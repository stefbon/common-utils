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

#ifndef _FUSE_ENTRY_MANAGEMENT_H
#define _FUSE_ENTRY_MANAGEMENT_H

#include <linux/fuse.h>

#define MODEMASK 07777

#define _ENTRY_FLAG_TEMP					1
#define _ENTRY_FLAG_VIRTUAL					2
#define _ENTRY_FLAG_REMOTECHANGED				4

#define _INODE_DIRECTORY_SIZE					4096
#define _DEFAULT_BLOCKSIZE					4096

#include "skiplist.h"

union datalink_u {
    void				*data;
    uint64_t				id;
};

struct inode_s {
    uint64_t 				ino;
    uint64_t 				nlookup;
    struct inode_s 			*id_next;
    struct entry_s 			*alias;
    mode_t				mode;
    nlink_t				nlink;
    uid_t				uid;
    gid_t				gid;
    off_t				size;
    struct timespec			mtim;
    struct timespec			ctim;
    struct timespec			atim;
    struct timespec			stim;
    struct fuse_fs_s			*fs;
    unsigned int			link_type;
    union datalink_u 			link;
};

struct name_s {
    char 				*name;
    size_t				len;
    unsigned long long			index;
};

/*
    TODO:
    add the "normal" index..
    next and prev using the order of adding
*/

struct entry_s {
    struct name_s			name;
    struct inode_s 			*inode;
    struct entry_s 			*name_next;
    struct entry_s 			*name_prev;
    struct entry_s 			*parent;
    unsigned char			flags;
};

// Prototypes

void calculate_nameindex(struct name_s *name);

int init_hashtables();

int init_inode_hashtable(unsigned int *error);
void free_inode_hashtable();

void init_entry(struct entry_s *entry);
struct entry_s *create_entry(struct entry_s *parent, struct name_s *xname);
void destroy_entry(struct entry_s *entry);
void rename_entry(struct entry_s *entry, char **name, unsigned int len);

void init_inode(struct inode_s *inode);
struct inode_s *create_inode();
void add_inode_hashtable(struct inode_s *inode, void (*cb) (void *data), void *data);

void fill_inode_stat(struct inode_s *inode, struct stat *st);

struct inode_s *find_inode(uint64_t ino);
struct inode_s *forget_inode(uint64_t ino, void (*cb) (void *data), void *data);
void remove_inode(struct inode_s *inode);

#endif
