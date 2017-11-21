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

#include "global-defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <inttypes.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "logging.h"

#include "skiplist.h"
#include "skiplist-find.h"
#include "skiplist-delete.h"
#include "skiplist-insert.h"
#include "skiplist-seek.h"

#include "entry-management.h"
#include "directory-management.h"

#ifndef SIZE_DIRECTORY_HASHTABLE
#define SIZE_DIRECTORY_HASHTABLE			1024
#endif

/*
    callbacks for the skiplist
    compare two elements to determine the right order
*/

static int compare_entry(void *a, void *b)
{
    int result=0;
    struct entry_s *entry=(struct entry_s *) a;
    struct name_s *name=(struct name_s *) b;

    if (entry->name.index > name->index) {

	result=1; /* entry->name is bigger */

    } else if (entry->name.index==name->index) {

	if (name->len > 6) {

	    if (entry->name.len > 6) {

		result=strcmp(entry->name.name + 6, name->name + 6);

	    } else {

		result=-1; /* name is bigger */

	    }

	} else if (name->len==6) {

	    if (entry->name.len>6) {

		result=1;

	    } else {

		result=0;

	    }

	} else {

	    result=0;

	}

    } else {

	result=-1;

    }

    return result;

}

static void *get_next_entry(void *data)
{
    struct entry_s *entry=(struct entry_s *) data;
    return (void *) entry->name_next;
}

static void *get_prev_entry(void *data)
{
    struct entry_s *entry=(struct entry_s *) data;
    return (void *) entry->name_prev;
}

/* insert an entry (a) before another (b) */

static void insert_before_entry(void *a, void *b, struct skiplist_struct *sl)
{
    struct entry_s *entry=(struct entry_s *) a;
    struct entry_s *before=(struct entry_s *) b;
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));

    if (before==directory->first) {

	entry->name_next=before;
	before->name_prev=entry;

	directory->first=entry;

    } else {
	struct entry_s *prev=before->name_prev;

	prev->name_next=entry;
	entry->name_prev=prev;

	entry->name_next=before;
	before->name_prev=entry;

    }

    directory->count++;

}

/* insert an entry (a) after another (b) */

static void insert_after_entry(void *a, void *b, struct skiplist_struct *sl)
{
    struct entry_s *entry=(struct entry_s *) a;
    struct entry_s *after=(struct entry_s *) b;
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));

    if ( ! after) after=directory->last;

    if (after==directory->last) {

	if ( ! after) {

	    /* empty */

	    directory->last=entry;
	    directory->first=entry;

	} else {

	    entry->name_prev=after;
	    after->name_next=entry;

	    directory->last=entry;

	}

    } else {
	struct entry_s *next=after->name_next;

	next->name_prev=entry;
	entry->name_next=next;

	entry->name_prev=after;
	after->name_next=entry;

    }

    directory->count++;

}

/* delete an entry from the linked list */

static void delete_entry(void *a, struct skiplist_struct *sl)
{
    struct entry_s *entry=(struct entry_s *) a;
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));

    if (entry==directory->first) {

	if (entry==directory->last) {

	    directory->first=NULL;
	    directory->last=NULL;

	} else {

	    directory->first=entry->name_next;
	    directory->first->name_prev=NULL;

	}

    } else if (entry==directory->last) {

	directory->last=entry->name_prev;
	directory->last->name_next=NULL;

    } else {
	struct entry_s *next=entry->name_next;
	struct entry_s *prev=entry->name_prev;

	prev->name_next=next;
	next->name_prev=prev;

    }

    entry->name_next=NULL;
    entry->name_prev=NULL;

    directory->count--;

}

/* lock a directory read */

int _lock_directory_read(struct directory_s *directory)
{

    if (directory->flags & _DIRECTORY_FLAG_REMOVE) return -1;

    /* increase the readers */

    pthread_mutex_lock(&directory->mutex);

    while (directory->lock & 3) {

	pthread_cond_wait(&directory->cond, &directory->mutex);

    }

    directory->lock+=4;

    pthread_mutex_unlock(&directory->mutex);

    return 0;

}

/* lock a directory pre exclusive (=prevent more readers and exclusive access by any other thread) */

int _lock_directory_preexcl(struct directory_s *directory)
{

    if (directory->flags & _DIRECTORY_FLAG_REMOVE) return -1;

    /* set a lock to prepare the exclusive lock */

    pthread_mutex_lock(&directory->mutex);

    if (directory->lock & 1) {

	if (directory->write_thread != pthread_self()) {

	    /* some other thread else already got it */

	    pthread_mutex_unlock(&directory->mutex);
	    return -1;

	}

    } else {

	directory->lock |= 1;
	directory->write_thread = pthread_self();

    }

    pthread_mutex_unlock(&directory->mutex);

    return 0;

}

/* lock a directory exclusive */

int _lock_directory_excl(struct directory_s *directory)
{

    if (directory->flags & _DIRECTORY_FLAG_REMOVE) return -1;

    /*
	set a exclusive lock
	only possible if:
	- no readers
	- no other thread has already exclusive acces and/or set the pre excl bit set
    */

    pthread_mutex_lock(&directory->mutex);

    if (directory->lock & 1) {

	/* preexclusive bit set */

	if (directory->write_thread==pthread_self()) {

	    /* we own the preexcl bit wait for readers to finish */

	    while (directory->lock>>3 > 1) {

		pthread_cond_wait(&directory->cond, &directory->mutex);

	    }

	    directory->lock |= 2;

	} else {

	    /* another thread owns the preexcl bit */

	    pthread_mutex_unlock(&directory->mutex);
	    return -1;

	}

    } else if (directory->lock & 2) {

	if (directory->write_thread!=pthread_self()) {

	    /* another thread has already locked it exclusive */

	    pthread_mutex_unlock(&directory->mutex);
	    return -1;

	}

	/* this thread owns the preexcl bit: ok */

	directory->lock |= 1;

    } else {

	directory->lock |= 1;

	/* wait for readers to finish */

	while(directory->lock>>3 > 1) {

	    pthread_cond_wait(&directory->cond, &directory->mutex);

	}

	directory->lock |= 2;
	directory->write_thread = pthread_self();

    }

    pthread_mutex_unlock(&directory->mutex);

    return 0;

}

int _unlock_directory_read(struct directory_s *directory)
{

    /* decrease the readers */

    pthread_mutex_lock(&directory->mutex);

    directory->lock-=4;

    pthread_cond_broadcast(&directory->cond);
    pthread_mutex_unlock(&directory->mutex);

    return 0;

}

int _unlock_directory_preexcl(struct directory_s *directory)
{

    /* remove the pre excl lock */

    pthread_mutex_lock(&directory->mutex);

    if (directory->lock & 1) {

	if (directory->lock & 2) {

	    pthread_mutex_unlock(&directory->mutex);
	    return -1;

	} else {

	    directory->lock -= 1;
	    directory->write_thread = 0;
	    pthread_cond_broadcast(&directory->cond);

	}

    } else {

	pthread_mutex_unlock(&directory->mutex);
	return -1;

    }

    pthread_mutex_unlock(&directory->mutex);

    return 0;

}

int _unlock_directory_excl(struct directory_s *directory)
{

    /* set a exclusive lock */

    pthread_mutex_lock(&directory->mutex);

    if (directory->write_thread==pthread_self()) {

	if (directory->lock & 1) directory->lock -= 1;
	if (directory->lock & 2) directory->lock -= 2;
	directory->write_thread = 0;
	pthread_cond_broadcast(&directory->cond);

    } else {

	pthread_mutex_unlock(&directory->mutex);
	return -1;

    }

    pthread_mutex_unlock(&directory->mutex);

    return 0;

}

/* callbacks for the skiplist */

static int lock_skiplist(struct skiplist_struct *sl, unsigned short flags)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));

    if (flags==_SKIPLIST_READLOCK) {

	return _lock_directory_read(directory);

    } else if (flags==_SKIPLIST_PREEXCLLOCK) {

	return _lock_directory_preexcl(directory);

    } else if (flags==_SKIPLIST_EXCLLOCK) {

	return _lock_directory_excl(directory);

    }

    return -1;

}

static int unlock_skiplist(struct skiplist_struct *sl, unsigned short flags)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));

    if (flags==_SKIPLIST_READLOCK) {

	return _unlock_directory_read(directory);

    } else if (flags==_SKIPLIST_PREEXCLLOCK) {

	return _unlock_directory_preexcl(directory);

    } else if (flags==_SKIPLIST_EXCLLOCK) {

	return _unlock_directory_excl(directory);

    }

    return -1;

}

static unsigned int count_entries(struct skiplist_struct *sl)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));

    return directory->count;
}

static void *first_entry(struct skiplist_struct *sl)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));

    return (void *) directory->first;
}

static void *last_entry(struct skiplist_struct *sl)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));

    return (void *) directory->last;
}

int init_directory(struct directory_s *directory, unsigned int *error)
{
    int result=0;

    memset(directory, 0, sizeof(struct directory_s));

    directory->flags=0;
    directory->synctime.tv_sec=0;
    directory->synctime.tv_nsec=0;
    directory->inode=NULL;
    directory->count=0;

    pthread_mutex_init(&directory->mutex, NULL);
    pthread_cond_init(&directory->cond, NULL);
    directory->lock=0;
    directory->write_thread=0;

    directory->first=NULL;
    directory->last=NULL;

    directory->dops=NULL;

    result=init_skiplist(&directory->skiplist, 4, get_next_entry, get_prev_entry,
			compare_entry, insert_before_entry, insert_after_entry, delete_entry,
			lock_skiplist, unlock_skiplist, count_entries, first_entry, last_entry, error);

    if (result==-1) {

	logoutput_error("init_directory: error %i initializing skiplist", *error);

    }

    return result;

}

struct directory_s *_create_directory(struct inode_s *inode, void (* init_cb)(struct directory_s *directory), unsigned int lock, unsigned int *error)
{
    struct directory_s *directory=NULL;

    directory=malloc(sizeof(struct directory_s));

    if (directory) {
	int result=0;

	memset(directory, 0, sizeof(struct directory_s));

	result=init_directory(directory, error);

	if (result==-1) {

	    destroy_directory(directory);
	    directory=NULL;

	} else {

	    directory->lock=lock;
	    directory->inode=inode;

	    (* init_cb)(directory);

	    memcpy(&directory->link, &inode->link, sizeof(union datalink_u));
	    inode->link.data=(void *) directory;

	}

    }

    return directory;

}

struct directory_s *get_directory(struct inode_s *inode)
{
    return (struct directory_s *) inode->link.data;
}

void free_pathcalls(struct pathcalls_s *p)
{
    pthread_mutex_destroy(&p->mutex);
    (* p->free)(p);

}

void free_directory(struct directory_s *directory)
{
    clear_skiplist(&directory->skiplist);
    destroy_lock_skiplist(&directory->skiplist);

    pthread_mutex_destroy(&directory->mutex);
    pthread_cond_destroy(&directory->cond);

    free_pathcalls(&directory->pathcalls);

}

void destroy_directory(struct directory_s *directory)
{

    free_directory(directory);
    free(directory);

}

int lock_pathcalls(struct pathcalls_s *p)
{
    return pthread_mutex_lock(&p->mutex);
}

int unlock_pathcalls(struct pathcalls_s *p)
{
    return pthread_mutex_unlock(&p->mutex);
}

unsigned int get_path_pathcalls(struct directory_s *directory, void *ptr)
{
    return (* directory->pathcalls.get_path)(directory, ptr);
}
