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

#include "logging.h"
#include "utils.h"

#include "skiplist.h"
#include "skiplist-find.h"
#include "skiplist-delete.h"
#include "skiplist-insert.h"
#include "skiplist-seek.h"

#include "simple-locking.h"
#include "fuse-dentry.h"
#include "fuse-directory.h"

#ifndef SIZE_DIRECTORY_HASHTABLE
#define SIZE_DIRECTORY_HASHTABLE			1024
#endif

static struct directory_s *directory_hashtable[2048];
static pthread_mutex_t directory_hashtable_mutex=PTHREAD_MUTEX_INITIALIZER;

extern struct directory_s *get_dummy_directory();
extern void fs_get_inode_link(struct inode_s *inode, struct inode_link_s **link);

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

struct simple_lock_s *create_rlock_directory(struct directory_s *directory)
{
    struct simple_lock_s *lock=malloc(sizeof(struct simple_lock_s));

    // logoutput_info("_create_rlock_directory: directory %s", (directory) ? "defined" : "notdefined");

    if (lock) {

	init_directory_readlock(directory, lock);
	lock->flags|=SIMPLE_LOCK_FLAG_ALLOCATED;
	if (simple_lock(lock)==0) return lock;
	free(lock);

    }

    return NULL;

}

struct simple_lock_s *create_wlock_directory(struct directory_s *directory)
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

int lock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    return simple_lock(lock);
}

int rlock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    init_directory_readlock(directory, lock);
    return simple_lock(lock);
}

int wlock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    init_directory_writelock(directory, lock);
    return simple_lock(lock);
}

int unlock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    int result=simple_unlock(lock);
    // if (lock->flags & SIMPLE_LOCK_FLAG_ALLOCATED) free(lock);
    return result;
}

int upgradelock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    return simple_upgradelock(lock);
}

int prelock_directory(struct directory_s *directory, struct simple_lock_s *lock)
{
    if (directory->flags & _DIRECTORY_FLAG_REMOVE) return -1;
    return simple_prelock(lock);
}


/* callbacks for the skiplist */

static void *create_rlock_skiplist(struct skiplist_struct *sl)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));
    //logoutput("create_rlock_skiplist");
    return (void *) create_rlock_directory(directory);
}

static void *create_wlock_skiplist(struct skiplist_struct *sl)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));
    //logoutput_info("create_wlock_skiplist");
    return (void *) create_wlock_directory(directory);
}

static int lock_skiplist(struct skiplist_struct *sl, void *ptr)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));
    struct simple_lock_s *lock=(struct simple_lock_s *) ptr;
    return lock_directory(directory, lock);
}

static int unlock_skiplist(struct skiplist_struct *sl, void *ptr)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));
    struct simple_lock_s *lock=(struct simple_lock_s *) ptr;
    return unlock_directory(directory, lock);
}

static int upgradelock_skiplist(struct skiplist_struct *sl, void *ptr)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));
    struct simple_lock_s *lock=(struct simple_lock_s *) ptr;
    return upgradelock_directory(directory, lock);
}

static int prelock_skiplist(struct skiplist_struct *sl, void *ptr)
{
    struct directory_s *directory=(struct directory_s *) ( ((char *) sl) - offsetof(struct directory_s, skiplist));
    struct simple_lock_s *lock=(struct simple_lock_s *) ptr;
    return prelock_directory(directory, lock);
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

    logoutput("init_directory");

    memset(directory, 0, sizeof(struct directory_s));
    directory->flags=0;
    directory->synctime.tv_sec=0;
    directory->synctime.tv_nsec=0;
    directory->inode=NULL;
    directory->next=NULL;
    directory->prev=NULL;
    directory->count=0;

    result=init_simple_locking(&directory->locking);

    if (result==-1) {

	logoutput_warning("init_directory: error initializing locking");
	goto out;

    }

    directory->first=NULL;
    directory->last=NULL;

    directory->dops=NULL;
    directory->link.type=0;
    directory->link.link.ptr=NULL;

    result=init_skiplist(&directory->skiplist, 4, get_next_entry, get_prev_entry,
			compare_entry, insert_before_entry, insert_after_entry, delete_entry,
			create_rlock_skiplist, create_wlock_skiplist,
			lock_skiplist, unlock_skiplist, upgradelock_skiplist, prelock_skiplist, count_entries, first_entry, last_entry, error);

    if (result==-1) {

	logoutput_warning("init_directory: error %i initializing skiplist", *error);

    } else {

	logoutput("init_directory: directory intialized");

    }

    out:

    return result;

}

/* search the directory using a hash table
    search is misleading a bit here, since it's always defined: a default value is taken (&dummy_directory)*/

struct directory_s *search_directory(struct inode_s *inode)
{
    unsigned int hashvalue = inode->st.st_ino % 2048;
    struct directory_s *directory=get_dummy_directory(); /* default value */
    struct directory_s *search=NULL;

    pthread_mutex_lock(&directory_hashtable_mutex);
    search=directory_hashtable[hashvalue];

    while (search) {

	if (search->inode==inode) {

	    directory=search;
	    break;

	}

	search=search->next;

    }

    pthread_mutex_unlock(&directory_hashtable_mutex);
    return directory;
}

struct directory_s *get_directory_dump(struct inode_s *inode)
{
    return (struct directory_s *) inode->link.link.ptr;
}

void set_directory_dump(struct inode_s *inode, struct directory_s *d)
{
    inode->link.type=INODE_LINK_TYPE_DIRECTORY;
    inode->link.link.ptr=(void *) d;
}

struct directory_s *get_directory(struct inode_s *inode)
{
    struct inode_link_s *link=NULL;
    fs_get_inode_link(inode, &link);
    return get_directory_dump(inode);
}

int get_inode_link_directory(struct inode_s *inode, struct inode_link_s *link)
{
    struct directory_s *directory=get_directory(inode);
    memcpy(link, &directory->link, sizeof(struct inode_link_s));
    return 0;
}

void set_inode_link_directory(struct inode_s *inode, struct inode_link_s *link)
{
    struct directory_s *directory=get_directory(inode);
    memcpy(&directory->link, link, sizeof(struct inode_link_s));
}

static void _add_directory_hashtable(struct directory_s *directory)
{
    unsigned int hashvalue = directory->inode->st.st_ino % 2048;

    pthread_mutex_lock(&directory_hashtable_mutex);
    directory->prev=NULL;
    directory->next=directory_hashtable[hashvalue];
    directory_hashtable[hashvalue]=directory;
    pthread_mutex_unlock(&directory_hashtable_mutex);
}

void _remove_directory_hashtable(struct directory_s *directory)
{
    unsigned int hashvalue = directory->inode->st.st_ino % 2048;

    pthread_mutex_lock(&directory_hashtable_mutex);

    if (directory==directory_hashtable[hashvalue]) {
	struct directory_s *next=directory->next;

	directory_hashtable[hashvalue]=next;
	if (next) next->prev=NULL;

    } else {
	struct directory_s *next=directory->next;
	struct directory_s *prev=directory->prev;

	if (next) next->prev=directory->prev;
	if (prev) prev->next=directory->next;

    }

    pthread_mutex_unlock(&directory_hashtable_mutex);

    directory->prev=NULL;
    directory->next=NULL;

}

struct directory_s *_create_directory(struct inode_s *inode, void (* init_cb)(struct directory_s *directory), unsigned int *error)
{
    struct directory_s *directory=NULL;

    logoutput("_create_directory: inode %li", inode->st.st_ino);

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
	    _add_directory_hashtable(directory);

	}

    }

    return directory;

}

void free_pathcalls(struct pathcalls_s *p)
{
    pthread_mutex_destroy(&p->mutex);
    (* p->free)(p);
}

void free_directory(struct directory_s *directory)
{
    _remove_directory_hashtable(directory);
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

int init_directory_hashtable()
{
    memset(directory_hashtable, 0, sizeof(directory_hashtable));
    for (unsigned int i=0; i<2048; i++) directory_hashtable[i]=NULL;
    pthread_mutex_init(&directory_hashtable_mutex, NULL);
    return 0;
}

void free_directory_hashtable()
{
    memset(directory_hashtable, 0, sizeof(directory_hashtable));
    for (unsigned int i=0; i<2048; i++) directory_hashtable[i]=NULL;
    pthread_mutex_destroy(&directory_hashtable_mutex);
}
