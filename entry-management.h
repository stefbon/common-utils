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

#ifndef SB_COMMON_UTILS_ENTRY_MANAGEMENT_H
#define SB_COMMON_UTILS_ENTRY_MANAGEMENT_H

#include <linux/fuse.h>
#include "workspace-interface.h"

#define MODEMASK 07777

#define _ENTRY_FLAG_TEMP					1
#define _ENTRY_FLAG_VIRTUAL					2
#define _ENTRY_FLAG_REMOTECHANGED				4

#define _INODE_DIRECTORY_SIZE					4096
#define _DEFAULT_BLOCKSIZE					4096

#define FORGET_INODE_FLAG_QUEUE					1
#define FORGET_INODE_FLAG_REMOVE_ENTRY				2
#define FORGET_INODE_FLAG_DELETED				4
#define FORGET_INODE_FLAG_NOTIFY_VFS				8

#define INODE_LINK_TYPE_CONTEXT					1
#define INODE_LINK_TYPE_ID					2
#define INODE_LINK_TYPE_SPECIAL_ENTRY				3
#define INODE_LINK_TYPE_DATA					4
#define INODE_LINK_TYPE_DIRECTORY				5
#define INODE_LINK_TYPE_CACHE					6

#define INODE_FLAG_CACHED					1

#include "skiplist.h"

union datalink_u {
    void				*ptr;
    uint64_t				id;
};

struct inode_link_s {
    unsigned char			type;
    union datalink_u			link;
};

struct inode_s {
    unsigned char			flags;
    uint64_t				nlookup;
    struct inode_s 			*id_next;
    struct inode_s 			*id_prev;
    struct entry_s 			*alias;
    struct stat				st;
    struct timespec			stim;
    struct fuse_fs_s			*fs;
    struct inode_link_s			link;
    unsigned int			cache_size;
    char				cache[];
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
struct inode_s *create_inode(unsigned int s);
void add_inode_hashtable(struct inode_s *inode, void (*cb) (void *data), void *data);

void fill_inode_stat(struct inode_s *inode, struct stat *st);
void get_inode_stat(struct inode_s *inode, struct stat *st);

struct inode_s *realloc_inode(struct inode_s *inode, unsigned int new);

struct inode_s *find_inode(uint64_t ino);
struct inode_s *forget_inode(struct context_interface_s *i, uint64_t ino, uint64_t lookup, void (*cb) (void *data), void *data, unsigned int flags);
void remove_inode(struct context_interface_s *i, struct inode_s *inode);

#define INODE_INFORMATION_OWNER						(1 << 0)
#define INODE_INFORMATION_GROUP						(1 << 1)
#define INODE_INFORMATION_NAME						(1 << 2)
#define INODE_INFORMATION_NLOOKUP					(1 << 3)
#define INODE_INFORMATION_MODE						(1 << 4)
#define INODE_INFORMATION_NLINK						(1 << 5)
#define INODE_INFORMATION_SIZE						(1 << 6)
#define INODE_INFORMATION_MTIM						(1 << 7)
#define INODE_INFORMATION_CTIM						(1 << 8)
#define INODE_INFORMATION_ATIM						(1 << 9)
#define INODE_INFORMATION_STIM						(1 << 10)
#define INODE_INFORMATION_INODE_LINK					(1 << 11)
#define INODE_INFORMATION_FS_COUNT					(1 << 12)

void log_inode_information(struct inode_s *inode, uint64_t what);

#endif
