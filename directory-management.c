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

#include "simple-locking.h"
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

void init_directory_readlock(struct directory_s *directory, struct simple_lock_s *lock)
{
    init_simple_readlock(&directory->locking, lock);
}

void init_directory_writelock(struct directory_s *directory, struct simple_lock_s *lock)
{
    init_simple_writelock(&directory->locking, lock);
}

struct simple_lock_s *_create_rlock_directory(struct directory_s *directory)
{
    struct simple_lock_s *lock=malloc(sizeof(struct simple_lock_s));

    if (lock) {

	init_directory_readlock(directory, lock);
	lock->flags|=SIMPLE_LOCK_FLAG_ALLOCATED;
	if (simple_lock(lock)==0) return lock;
	free(lock);

    }

    return NULL;

}

struct simple_lock_s *_create_wlock_directory(struct directory_s *directory)
{
    struct simple_lock_s *lock=malloc(sizeof(struct simple_lock_s));

    if (lock) {

	init_directory_writelock(directory, lock);
	lock->flags|=SIMPLE_LOCK_FLAG_ALLOCATED;
	if (simple_lock(lock)==0) return lock;
	free(lock);

    }

    return NULL;

}

int _lock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    return simple_lock(lock);
}

int _rlock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    init_directory_readlock(directory, lock);
    return simple_lock(lock);
}

int _wlock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    init_directory_writelock(directory, lock);
    return simple_lock(lock);
}

int _unlock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    int result=simple_unlock(lock);
    // if (lock->flags & SIMPLE_LOCK_FLAG_ALLOCATED) free(lock);
    return result;
}

int _upgradelock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    return simple_upgradelock(lock);
}

int _prelock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    if (directory->flags & _DIRECTORY_FLAG_REMOVE) return -1;
    return simple_prelock(lock);
}


/* callbacks for the skiplist */

static void *create_rlock_skiplist(struct skiplist_struct *sl)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));

    return (void *) _create_rlock_directory(directory);
}

static void *create_wlock_skiplist(struct skiplist_struct *sl)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));

    return (void *) _create_wlock_directory(directory);
}

static int lock_skiplist(struct skiplist_struct *sl, void *ptr)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));
    struct simple_lock_s *lock=(struct simple_lock_s *) ptr;

    return _lock_directory(directory, lock);
}

static int unlock_skiplist(struct skiplist_struct *sl, void *ptr)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));
    struct simple_lock_s *lock=(struct simple_lock_s *) ptr;

    return _unlock_directory(directory, lock);
}

static int upgradelock_skiplist(struct skiplist_struct *sl, void *ptr)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));
    struct simple_lock_s *lock=(struct simple_lock_s *) ptr;

    return _upgradelock_directory(directory, lock);
}

static int prelock_skiplist(struct skiplist_struct *sl, void *ptr)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));
    struct simple_lock_s *lock=(struct simple_lock_s *) ptr;

    return _prelock_directory(directory, lock);
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

    result=init_simple_locking(&directory->locking);

    if (result==-1) {

	logoutput_warning("init_directory: error initializing locking");
	goto out;

    }

    directory->first=NULL;
    directory->last=NULL;

    directory->dops=NULL;

    result=init_skiplist(&directory->skiplist, 4, get_next_entry, get_prev_entry,
			compare_entry, insert_before_entry, insert_after_entry, delete_entry,
			create_rlock_skiplist, create_wlock_skiplist,
			lock_skiplist, unlock_skiplist, upgradelock_skiplist, prelock_skiplist, count_entries, first_entry, last_entry, error);

    if (result==-1) {

	logoutput_warning("init_directory: error %i initializing skiplist", *error);

    }

    out:

    return result;

}

struct directory_s *_create_directory(struct inode_s *inode, void (* init_cb)(struct directory_s *directory), unsigned int *error)
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
    clear_simple_locking(&directory->locking);
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
