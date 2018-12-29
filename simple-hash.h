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
#ifndef SB_COMMON_UTILS_SIMPLE_HASH_H
#define SB_COMMON_UTILS_SIMPLE_HASH_H

#include "simple-list.h"
#include "simple-locking.h"
#define SIMPLE_HASH_HASHSIZE	512

struct hash_element_s {
    void 			*data;
    struct list_element_s 	list;
};

/* TODO: add "add", "remove" and "lookup" functions as cb */

struct simple_hash_s {
    struct simple_locking_s	locking;
    unsigned int 		(*hashfunction) (void *data);
    int 			len;
    struct list_header_s 	*hash;
};

/* prototypes */

int initialize_group(struct simple_hash_s *group, unsigned int (*hashfunction) (void *data), unsigned int len, unsigned int *error);
void free_group(struct simple_hash_s *group, void (*free_data) (void *data));

void init_rlock_hashtable(struct simple_hash_s *group, struct simple_lock_s *lock);
void init_wlock_hashtable(struct simple_hash_s *group, struct simple_lock_s *lock);

int lock_hashtable(struct simple_lock_s *l);
int unlock_hashtable(struct simple_lock_s *l);

void *get_next_hashed_value(struct simple_hash_s *group, void **index, unsigned int hashvalue);

void add_data_to_hash(struct simple_hash_s *group, void *data);
void remove_data_from_hash(struct simple_hash_s *group, void *data);
void remove_data_from_hash_index(struct simple_hash_s *group, void **index);

unsigned int get_hashvalue_index(void *index, struct simple_hash_s *group);

#endif
