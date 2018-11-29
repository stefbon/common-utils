/*

  2010, 2011, 2012, 2013, 2014, 2015, 2016 Stef Bon <stefbon@gmail.com>

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
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <pthread.h>

#include "simple-list.h"
#include "simple-hash.h"
#undef LOGGING
#include "logging.h"

static inline struct hash_element_s *get_hash_element(struct list_element_s *list)
{
    return (struct hash_element_s *) ( ((char *) list) - offsetof(struct hash_element_s, list));
}

static struct hash_element_s *create_hash_element()
{
    struct hash_element_s *element=NULL;

    element=malloc(sizeof(struct hash_element_s));

    if (element) {

	element->data=NULL;
	init_list_element(&element->list, NULL);

    }

    return element;

}

static void insert_in_hash(struct simple_hash_s *group, struct hash_element_s *element)
{
    unsigned int i=((*group->hashfunction)(element->data) % group->len);
    add_list_element_last(&group->hash[i], &element->list);
}

static void move_from_hash(struct simple_hash_s *group, struct hash_element_s *element)
{
    unsigned int i=((*group->hashfunction) (element->data) % group->len);
    remove_list_element(&element->list);
}

struct hash_element_s *lookup_simple_hash(struct simple_hash_s *group, void *data)
{
    struct list_element_s *list=NULL;
    unsigned int i=0;
    struct hash_element_s *element=NULL;

    i=((*group->hashfunction) (data) % group->len);

    list=group->hash[i].head;

    while (list) {

	element=get_hash_element(list);
	if (element->data==data) break;
	list=list->n;
	element=NULL; /* element has an invalid value... forget it otherwise this will be returned */

    }

    return element;

}

int lock_hashtable(struct simple_lock_s *lock)
{
    return simple_lock(lock);
}

int unlock_hashtable(struct simple_lock_s *lock)
{
    return simple_unlock(lock);
}

void init_rlock_hashtable(struct simple_hash_s *group, struct simple_lock_s *lock)
{
    init_simple_readlock(&group->locking, lock);
}

void init_wlock_hashtable(struct simple_hash_s *group, struct simple_lock_s *lock)
{
    init_simple_writelock(&group->locking, lock);
}

int initialize_group(struct simple_hash_s *group, unsigned int (*hashfunction) (void *data), unsigned int len, unsigned int *error)
{
    int result=0;

    *error=ENOMEM;

    if (init_simple_locking(&group->locking)==-1) goto error;

    group->hashfunction=hashfunction;
    group->len=len;
    group->hash=NULL;

    if (len>0) {

	group->hash=(struct list_header_s *) malloc(len * sizeof(struct list_header_s));
	if (! group->hash) goto error;
	*error=0;
	for (unsigned int i=0;i<len;i++) init_list_header(&group->hash[i], SIMPLE_LIST_TYPE_EMPTY, NULL);

    }

    out:

    return 0;

    error:

    return -1;

}

void free_group(struct simple_hash_s *group, void (*free_data) (void *data))
{
    struct simple_lock_s wlock;

    init_wlock_hashtable(group, &wlock);
    lock_hashtable(&wlock);

    if (group->hash) {
	struct list_element_s *list=NULL;
	struct hash_element_s *element=NULL;

	for (unsigned int i=0;i<group->len;i++) {

	    list=group->hash[i].head;

	    while(element) {

		group->hash[i].head=list->n;

		element=get_hash_element(list);
		if (free_data && element->data) free_data(element->data);
		free(element);

		list=group->hash[i].head;

	    }

	}

	free(group->hash);
	group->hash=NULL;

    }

    unlock_hashtable(&wlock);
    clear_simple_locking(&group->locking);

}


void *get_next_hashed_value(struct simple_hash_s *group, void **index, unsigned int hashvalue)
{
    struct hash_element_s *element=NULL;

    if (*index) {

	element=(struct hash_element_s *) *index;
	element=(element->list.n) ? get_hash_element(element->list.n) : NULL;

    } else {

	hashvalue=hashvalue % group->len;
	element=(group->hash[hashvalue].head) ? get_hash_element(group->hash[hashvalue].head) : NULL;

    }

    *index=(void *) element;
    return (element) ? element->data : NULL;
}

void add_data_to_hash(struct simple_hash_s *group, void *data)
{
    struct hash_element_s *element=create_hash_element();

    if (element) {

	element->data=data;
	init_list_element(&element->list, NULL);
	insert_in_hash(group, element);

    }

}

void remove_data_from_hash(struct simple_hash_s *group, void *data)
{
    struct hash_element_s *element=lookup_simple_hash(group, data);

    logoutput("remove_data_from_hash");

    if (element) {

	move_from_hash(group, element);
	element->data=NULL;
	free(element);

    }

}

void remove_data_from_hash_index(struct simple_hash_s *group, void **index)
{
    struct hash_element_s *element=(struct hash_element_s *) *index;

    if (element) {

	move_from_hash(group, element);
	free(element);

	*index=NULL;

    }

}

unsigned int get_hashvalue_index(void *index, struct simple_hash_s *group)
{
    struct hash_element_s *element=(struct hash_element_s *) index;
    return ((*group->hashfunction) (element->data)) % group->len;
}

