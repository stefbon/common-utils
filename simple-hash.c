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
	element->list.next=NULL;
	element->list.prev=NULL;

    }

    return element;

}

static void insert_in_hash(struct simple_hash_s *group, struct hash_element_s *element)
{
    unsigned int i=((*group->hashfunction)(element->data) % group->len);
    add_list_element_last(&group->hash[i].head, &group->hash[i].tail, &element->list);
}

static void move_from_hash(struct simple_hash_s *group, struct hash_element_s *element)
{
    unsigned int i=((*group->hashfunction) (element->data) % group->len);
    remove_list_element(&group->hash[i].head, &group->hash[i].tail, &element->list);
}

struct hash_element_s *lookup_simple_hash(struct simple_hash_s *group, void *data)
{
    struct list_element_s *list=NULL;
    unsigned int i=((*group->hashfunction) (data) % group->len);
    struct hash_element_s *element=NULL;

    list=group->hash[i].head;

    while (list) {

	element=get_hash_element(list);
	if (element->data==data) break;
	list=list->next;
	element=NULL; /* element has an invalid value... forget it otherwise this will be returned */

    }

    return element;

}

int initialize_group(struct simple_hash_s *group, unsigned int (*hashfunction) (void *data), unsigned int len, unsigned int *error)
{
    int result=0;

    pthread_rwlock_init(&group->rwlock, NULL);
    group->hashfunction=hashfunction;
    group->len=len;
    group->hash=NULL;

    if (len>0) {

	group->hash=(struct hash_head_s *) malloc(len * sizeof(struct hash_head_s));

	if (! group->hash) {

	    *error=ENOMEM;
	    goto error;

	}

	for (unsigned int i=0;i<len;i++) {

	    group->hash[i].head=NULL;
	    group->hash[i].tail=NULL;

	}

    }

    out:

    return 0;

    error:

    return -1;

}

void free_group(struct simple_hash_s *group, void (*free_data) (void *data))
{

    pthread_rwlock_wrlock(&group->rwlock);

    if (group->hash) {
	struct list_element_s *list=NULL;
	struct hash_element_s *element=NULL;

	for (unsigned int i=0;i<group->len;i++) {

	    list=group->hash[i].head;

	    while(element) {

		group->hash[i].head=list->next;

		element=get_hash_element(list);
		if (free_data && element->data) free_data(element->data);
		free(element);

		list=group->hash[i].head;

	    }

	}

	free(group->hash);
	group->hash=NULL;

    }

    pthread_rwlock_unlock(&group->rwlock);
    pthread_rwlock_destroy(&group->rwlock);

}

int readlock_hashtable(struct simple_hash_s *group)
{
    return pthread_rwlock_rdlock(&group->rwlock);
}

int writelock_hashtable(struct simple_hash_s *group)
{
    return pthread_rwlock_wrlock(&group->rwlock);
}

int unlock_hashtable(struct simple_hash_s *group)
{
    return pthread_rwlock_unlock(&group->rwlock);
}

void *get_next_hashed_value(struct simple_hash_s *group, void **index, unsigned int hashvalue)
{
    struct hash_element_s *element=NULL;

    if (*index) {

	element=(struct hash_element_s *) *index;
	element=(element->list.next) ? get_hash_element(element->list.next) : NULL;

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
	element->list.next=NULL;
	element->list.prev=NULL;
	insert_in_hash(group, element);

    }

}

void remove_data_from_hash(struct simple_hash_s *group, void *data)
{
    struct hash_element_s *element=lookup_simple_hash(group, data);

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

