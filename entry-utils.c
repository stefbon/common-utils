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
#include "pathinfo.h"

#include "skiplist.h"
#include "skiplist-find.h"
#include "skiplist-delete.h"
#include "skiplist-insert.h"

#include "entry-management.h"
#include "directory-management.h"
#include "entry-utils.h"
#include "fuse-fs.h"
#include "workspaces.h"
#include "path-caching.h"

extern void fs_inode_forget(struct inode_s *inode);

static struct directory_s dummy_directory;
static struct dops_s dummy_dops;
static struct dops_s default_dops;
static struct dops_s removed_dops;

typedef struct entry_s *(*find_entry_cb)(struct entry_s *parent, struct name_s *xname, unsigned int *error);

static struct entry_s *_find_entry_dummy(struct entry_s *parent, struct name_s *xname, unsigned int *error)
{
    *error=ENOENT;
    return NULL;
}

static struct entry_s *find_entry_dummy(struct entry_s *parent, struct name_s *xname, unsigned int *error)
{
    find_entry_cb find_entry=_find_entry_dummy;
    union datalink_u *link=&parent->inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;

    pthread_mutex_lock(&directory->mutex);

    if (!(directory->flags & _DIRECTORY_FLAG_DUMMY)) find_entry=directory->dops->find_entry;

    pthread_mutex_unlock(&directory->mutex);

    return (* find_entry)(parent, xname, error);
}

typedef void (*remove_entry_cb)(struct entry_s *entry, unsigned int *error);

static void _remove_entry_dummy(struct entry_s *entry, unsigned int *error)
{
    *error=EINVAL;
}

static void remove_entry_dummy(struct entry_s *entry, unsigned int *error)
{
    remove_entry_cb remove_entry=_remove_entry_dummy;
    union datalink_u *link=&entry->parent->inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;

    pthread_mutex_lock(&directory->mutex);

    if (!(directory->flags & _DIRECTORY_FLAG_DUMMY)) remove_entry=directory->dops->remove_entry;

    pthread_mutex_unlock(&directory->mutex);

    (* remove_entry)(entry, error);
}

/*
    insert an entry in a dummy directory
    since this is not possible (dummy directories do not have any entries) do create a
    "real" directory first
    note there is no lock required, this lock is already set(!)
*/

static struct entry_s *insert_entry_dummy(struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct entry_s *parent=entry->parent;
    struct directory_s *directory=NULL;
    struct directory_s *real_directory=NULL;
    union datalink_u *link=&parent->inode->link;

    /* when inserting an entry in the dummy directory, then create the "real" directory first */

    directory=(struct directory_s *) link->data;

    pthread_mutex_lock(&directory->mutex);

    /* get the directory again: the inode can point to a just created directory */

    real_directory=(struct directory_s *) link->data;

    if (real_directory->flags & _DIRECTORY_FLAG_DUMMY) {

	/* dealing with a dummy directory */

	real_directory=(* directory->dops->create_directory)(parent->inode, 0, error);

	if (! real_directory) {

	    if (*error==0) *error=ENOMEM;
	    pthread_mutex_unlock(&directory->mutex);
	    return NULL;

	}

    }

    pthread_mutex_unlock(&directory->mutex);

    return (* real_directory->dops->insert_entry)(entry, error, flags);

}

static void _init_directory(struct directory_s *directory)
{
    directory->dops=&default_dops;
    init_pathcalls(&directory->pathcalls);
}

static struct directory_s *create_directory(struct inode_s *inode, unsigned int lock, unsigned int *error)
{
    return _create_directory(inode, _init_directory, lock, error);
}

struct directory_s *_remove_directory(struct inode_s *inode, unsigned int *error)
{
    struct directory_s *directory=NULL;
    union datalink_u *link=&inode->link;

    pthread_mutex_lock(&dummy_directory.mutex);

    directory=(struct directory_s *) link->data;

    if (directory->flags & _DIRECTORY_FLAG_DUMMY) {

	directory=NULL;

    } else {

	if ((* directory->dops->lock_excl)(inode)==0) {

	    directory->flags |= _DIRECTORY_FLAG_REMOVE;
	    directory->dops=&removed_dops;
	    directory->inode=NULL;

	    (* directory->dops->unlock_excl)(inode);

	}

	inode->link.data=(void *) &dummy_directory;

    }

    pthread_mutex_unlock(&dummy_directory.mutex);

    return directory;

}

static struct directory_s *remove_directory_common(struct inode_s *inode, unsigned int *error)
{
    return _remove_directory(inode, error);
}

static struct entry_s *find_entry_batch_dummy(struct directory_s *directory, struct name_s *xname, unsigned int *error)
{
    *error=ENOENT;
    return NULL;
}

void remove_entry_batch_dummy(struct directory_s *directory, struct entry_s *entry, unsigned int *error)
{
    *error=EINVAL;
}

/*
    insert an entry in a dummy directory
    since this is not possible (dummy directories do not have any entries) do create a
    "real" directory first
    note there is no lock required, this lock is already set in the contex(!)
*/

struct entry_s *insert_entry_batch_dummy(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct entry_s *parent=entry->parent;
    struct directory_s *real_directory=NULL;
    union datalink_u *link=&parent->inode->link;

    real_directory=(struct directory_s *)link->data;

    if (real_directory->flags & _DIRECTORY_FLAG_DUMMY) {

	/* dealing with a dummy directory */

	real_directory=(* directory->dops->create_directory)(parent->inode, 0, error);

	if (! real_directory) {

	    if (*error==0) *error=ENOMEM;
	    return NULL;

	}

    }

    return (* real_directory->dops->insert_entry_batch)(real_directory, entry, error, flags);
}

static int lock_read_dummy(struct inode_s *inode)
{
    struct directory_s *directory=NULL;
    struct directory_s *real_directory=NULL;
    union datalink_u *link=&inode->link;

    directory=(struct directory_s *) link->data;

    pthread_mutex_lock(&directory->mutex);

    /* get the directory again: the inode can point to a just created directory */

    real_directory=(struct directory_s *) link->data;

    if (real_directory->flags & _DIRECTORY_FLAG_DUMMY) {
	unsigned int error=0;

	/*
	    dealing with a dummy directory
	    create a directory which is read locked
	*/

	real_directory=(* directory->dops->create_directory)(inode, 4, &error);

	if (! real_directory) {

	    pthread_mutex_unlock(&directory->mutex);
	    return -1;

	} else {

	    pthread_mutex_unlock(&directory->mutex);
	    return 0;

	}

    }

    pthread_mutex_unlock(&directory->mutex);

    return _lock_directory_read(real_directory);

}

static void unlock_read_dummy(struct inode_s *inode)
{
    /* unlocking the directory read is not usefull for the dummy directory */
}

static int lock_excl_dummy(struct inode_s *inode)
{
    struct directory_s *directory=NULL;
    struct directory_s *real_directory=NULL;
    union datalink_u *link=&inode->link;

    directory=(struct directory_s *) link->data;

    pthread_mutex_lock(&directory->mutex);

    /* get the directory again: the inode can point to a just created directory */

    real_directory=(struct directory_s *) link->data;

    if (real_directory->flags & _DIRECTORY_FLAG_DUMMY) {
	unsigned int error=0;

	/*
	    dealing with a dummy directory
	    create a directory which is excl locked
	*/

	real_directory=(* directory->dops->create_directory)(inode, 2, &error);

	if (! real_directory) {

	    pthread_mutex_unlock(&directory->mutex);
	    return -1;

	} else {

	    real_directory->write_thread = pthread_self();
	    pthread_mutex_unlock(&directory->mutex);

	    return 0;

	}

    }

    pthread_mutex_unlock(&directory->mutex);

    return _lock_directory_excl(real_directory);

}

static void unlock_excl_dummy(struct inode_s *inode)
{
    /* unlocking the directory excl is not usefull for the dummy directory */
}

static struct pathcalls_s *get_pathcalls_dummy(struct inode_s *inode)
{
    struct directory_s *directory=NULL;
    struct directory_s *real_directory=NULL;
    struct pathcalls_s *pathcalls=NULL;
    union datalink_u *link=&inode->link;

    directory=(struct directory_s *) link->data;

    pthread_mutex_lock(&directory->mutex);

    /* get the directory again: the inode can point to a just created directory */

    real_directory=(struct directory_s *) link->data;

    if (real_directory->flags & _DIRECTORY_FLAG_DUMMY) {
	unsigned int error=0;

	/*
	    dealing with a dummy directory
	    create a directory which is excl locked
	*/

	real_directory=(* directory->dops->create_directory)(inode, 0, &error);

	if (real_directory) pathcalls=&real_directory->pathcalls;

    } else {

	pathcalls=&real_directory->pathcalls;

    }

    pthread_mutex_unlock(&directory->mutex);

    return pathcalls;

}

static struct dops_s dummy_dops = {
    .find_entry			= find_entry_dummy,
    .remove_entry		= remove_entry_dummy,
    .insert_entry		= insert_entry_dummy,
    .create_directory		= create_directory,
    .remove_directory		= remove_directory_common,
    .find_entry_batch		= find_entry_batch_dummy,
    .remove_entry_batch		= remove_entry_batch_dummy,
    .insert_entry_batch		= insert_entry_batch_dummy,
    .lock_read			= lock_read_dummy,
    .unlock_read		= unlock_read_dummy,
    .lock_excl			= lock_excl_dummy,
    .unlock_excl		= unlock_excl_dummy,
    .get_pathcalls		= get_pathcalls_dummy
};

/*
    common entry function (find, insert, remove) for a normal directory
*/

static struct entry_s *find_entry_common(struct entry_s *parent, struct name_s *xname, unsigned int *error)
{
    union datalink_u *link=&parent->inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    unsigned int row=0;

    *error=0;

    return (struct entry_s *) find_sl(&directory->skiplist, (void *) xname, &row, error);

}

static void remove_entry_common(struct entry_s *entry, unsigned int *error)
{
    struct entry_s *parent=entry->parent;
    union datalink_u *link=&parent->inode->link;
    struct directory_s *directory=get_directory(parent->inode);
    struct name_s *lookupname=&entry->name;
    unsigned int row=0;

    delete_sl(&directory->skiplist, (void *) lookupname, &row, error);

}

static struct entry_s *insert_entry_common(struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct entry_s *parent=entry->parent;
    union datalink_u *link=&parent->inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    struct name_s *lookupname=&entry->name;
    unsigned int row=0;
    unsigned short sl_flags=0;

    if (flags & _ENTRY_FLAG_TEMP) sl_flags |= _SL_INSERT_FLAG_NOLANE;

    return (struct entry_s *)insert_sl(&directory->skiplist, (void *) lookupname, &row, error, (void *) entry, sl_flags);

}

static struct entry_s *find_entry_common_batch(struct directory_s *directory, struct name_s *xname, unsigned int *error)
{
    unsigned int row=0;

    return (struct entry_s *) find_sl_batch(&directory->skiplist, (void *) xname, &row, error);

}

static void remove_entry_common_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error)
{
    struct name_s *lookupname=&entry->name;
    unsigned int row=0;

    delete_sl_batch(&directory->skiplist, (void *) lookupname, &row, error);

}

static struct entry_s *insert_entry_common_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct name_s *lookupname=&entry->name;
    unsigned int row=0;
    unsigned short sl_flags=0;

    if (flags & _ENTRY_FLAG_TEMP) sl_flags |= _SL_INSERT_FLAG_NOLANE;

    return (struct entry_s *) insert_sl_batch(&directory->skiplist, (void *) lookupname, &row, error, (void *) entry, sl_flags);

}

static int lock_read_common(struct inode_s *inode)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    return _lock_directory_read(directory);

}

static void unlock_read_common(struct inode_s *inode)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    _unlock_directory_read(directory);
}

static int lock_excl_common(struct inode_s *inode)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    return _lock_directory_excl(directory);

}
static void unlock_excl_common(struct inode_s *inode)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    _unlock_directory_excl(directory);
}

static struct pathcalls_s *get_pathcalls_common(struct inode_s *inode)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    return &directory->pathcalls;
}

static struct dops_s default_dops = {
    .find_entry			= find_entry_common,
    .remove_entry		= remove_entry_common,
    .insert_entry		= insert_entry_common,
    .create_directory		= create_directory,
    .remove_directory		= remove_directory_common,
    .find_entry_batch		= find_entry_common_batch,
    .remove_entry_batch		= remove_entry_common_batch,
    .insert_entry_batch		= insert_entry_common_batch,
    .lock_read			= lock_read_common,
    .unlock_read		= unlock_read_common,
    .lock_excl			= lock_excl_common,
    .unlock_excl		= unlock_excl_common,
    .get_pathcalls		= get_pathcalls_common,
};

/*
    entry functions when a directory is set as removed
*/

static struct entry_s *find_entry_removed(struct entry_s *parent, struct name_s *xname, unsigned int *error)
{

    *error=ENOTDIR;
    return NULL;

}

static void remove_entry_removed(struct entry_s *entry, unsigned int *error)
{

    *error=ENOTDIR;
    return;

}

static struct entry_s *insert_entry_removed(struct entry_s *entry, unsigned int *error, unsigned short flags)
{

    *error=ENOTDIR;
    return NULL;

}

static struct directory_s *create_directory_removed(struct inode_s *inode, unsigned int lock, unsigned int *error)
{
    *error=ENOTDIR;
    return NULL;

}

static struct directory_s *remove_directory_removed(struct inode_s *inode, unsigned int *error)
{
    *error=ENOTDIR;
    return NULL;

}

static struct entry_s *find_entry_removed_batch(struct directory_s *directory, struct name_s *xname, unsigned int *error)
{

    *error=ENOTDIR;
    return NULL;

}

static void remove_entry_removed_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error)
{

    *error=ENOTDIR;
    return;

}

static struct entry_s *insert_entry_removed_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{

    *error=ENOTDIR;
    return NULL;

}

static int lock_read_removed(struct inode_s *inode)
{

    return -1;

}

static void unlock_read_removed(struct inode_s *inode)
{
}

static int lock_excl_removed(struct inode_s *inode)
{

    return -1;

}

static void unlock_excl_removed(struct inode_s *inode)
{
}

static struct pathcalls_s *get_pathcalls_removed(struct inode_s *inode)
{
    return NULL;
}

static struct dops_s removed_dops = {
    .find_entry			= find_entry_removed,
    .remove_entry		= remove_entry_removed,
    .insert_entry		= insert_entry_removed,
    .create_directory		= create_directory_removed,
    .remove_directory		= remove_directory_removed,
    .find_entry_batch		= find_entry_removed_batch,
    .remove_entry_batch		= remove_entry_removed_batch,
    .insert_entry_batch		= insert_entry_removed_batch,
    .lock_read			= lock_read_removed,
    .unlock_read		= unlock_read_removed,
    .lock_excl			= lock_excl_removed,
    .unlock_excl		= unlock_excl_removed,
    .get_pathcalls		= get_pathcalls_removed,
};

/*
    simple functions which call the right function for the directory
*/

struct entry_s *find_entry(struct entry_s *parent, struct name_s *xname, unsigned int *error)
{
    union datalink_u *link=&parent->inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;

    return (* directory->dops->find_entry)(parent, xname, error);
}

void remove_entry(struct entry_s *entry, unsigned int *error)
{
    struct entry_s *parent=entry->parent;
    union datalink_u *link=&parent->inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;

    (* directory->dops->remove_entry)(entry, error);
}

struct entry_s *insert_entry(struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct entry_s *parent=entry->parent;
    union datalink_u *link=&parent->inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;

    return (* directory->dops->insert_entry)(entry, error, flags);

}

struct entry_s *find_entry_batch(struct directory_s *directory, struct name_s *xname, unsigned int *error)
{
    return (* directory->dops->find_entry_batch)(directory, xname, error);
}

void remove_entry_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error)
{
    (* directory->dops->remove_entry_batch)(directory, entry, error);
}

struct entry_s *insert_entry_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    return (* directory->dops->insert_entry_batch)(directory, entry, error, flags);
}

struct directory_s *remove_directory(struct inode_s *inode, unsigned int *error)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    return (* directory->dops->remove_directory)(inode, error);
}

int lock_directory_read(struct inode_s *inode)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    return (* directory->dops->lock_read)(inode);
}

int lock_directory_excl(struct inode_s *inode)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    return (* directory->dops->lock_excl)(inode);
}

void unlock_directory_read(struct inode_s *inode)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    (* directory->dops->unlock_read)(inode);
}

void unlock_directory_excl(struct inode_s *inode)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    (* directory->dops->unlock_excl)(inode);
}

struct pathcalls_s *get_pathcalls(struct inode_s *inode)
{
    union datalink_u *link=&inode->link;
    struct directory_s *directory=(struct directory_s *) link->data;
    return (* directory->dops->get_pathcalls)(inode);
}

void init_directory_calls()
{
    unsigned int error=0;

    if (init_directory(&dummy_directory, &error)==-1) {

	logoutput_error("init_directory_calls: error initializing dummy directory");

    } else {

	logoutput("init_directory_calls: initialized dummy directory");

    }

    init_pathcalls(&dummy_directory.pathcalls);
    dummy_directory.dops=&dummy_dops;
    dummy_directory.flags|=_DIRECTORY_FLAG_DUMMY;

}

struct directory_s *get_dummy_directory()
{
    return &dummy_directory;
}

struct entry_s *create_entry_extended( struct entry_s *parent, 
					    struct name_s *xname, 
					    void (* cb_created)(struct entry_s *entry, void *data),
					    void (* cb_found)(struct entry_s *entry, void *data),
					    void (* cb_error)(struct entry_s *parent, struct name_s *xname, void *data, unsigned int error),
					    void *data)
{
    struct entry_s *entry=NULL, *result=NULL;
    struct inode_s *inode=NULL;
    unsigned int error=0;

    entry=create_entry(parent, xname);
    inode=create_inode();

    if (entry && inode) {

	result=insert_entry(entry, &error, 0);

	if (error==0) {

	    /* new */

	    inode->alias=entry;
	    entry->inode=inode;

	    (* cb_created)(entry, data);

	} else {

	    if (error==EEXIST) {

		/* existing found */

		destroy_entry(entry);
		entry=result;

		free(inode);
		inode=entry->inode;

		error=0;

		(* cb_found)(entry, data);

	    } else {

		/* another error */

		destroy_entry(entry);
		entry=NULL;

		free(inode);
		inode=NULL;

		goto error;

	    }

	}

    } else {

	/* unable to allocate entry and/or inode */

	if (entry) {

	    destroy_entry(entry);
	    entry=NULL;

	}

	if (inode) {

	    free(inode);
	    inode=NULL;

	}

	error=ENOMEM;
	goto error;

    }

    return entry;

    error:

    (* cb_error) (parent, xname, data, error);

    return NULL;

}


struct entry_s *create_entry_extended_batch(struct directory_s *directory,
					    struct name_s *xname, 
					    void (* cb_created)(struct entry_s *entry, void *data),
					    void (* cb_found)(struct entry_s *entry, void *data),
					    void (* cb_error)(struct entry_s *parent, struct name_s *xname, void *data, unsigned int error),
					    void *data)
{
    struct entry_s *entry=NULL, *result=NULL, *parent=directory->inode->alias;
    struct inode_s *inode=NULL;
    unsigned error=0;

    entry=create_entry(parent, xname);
    inode=create_inode();

    if (entry && inode) {

	result=insert_entry_batch(directory, entry, &error, 0);

	if (error==0) {

	    /* new */

	    inode->alias=entry;
	    entry->inode=inode;

	    (* cb_created)(entry, data);

	} else {

	    if (error==EEXIST) {

		/* existing found */

		destroy_entry(entry);
		entry=result;

		free(inode);
		inode=entry->inode;

		error=0;

		(* cb_found)(entry, data);

	    } else {

		/* another error */

		destroy_entry(entry);
		entry=NULL;

		free(inode);
		inode=NULL;

		goto error;

	    }

	}

    } else {

	/* unable to allocate entry and/or inode */

	if (entry) {

	    destroy_entry(entry);
	    entry=NULL;

	}

	if (inode) {

	    free(inode);
	    inode=NULL;

	}

	error=ENOMEM;
	goto error;

    }

    return entry;

    error:

    (* cb_error) (parent, xname, data, error);

    return NULL;

}

static void _remove_entry_cb (struct directory_s *directory, struct entry_s *entry)
{

    if (entry->inode) {
	struct inode_s *inode=entry->inode;

	remove_inode(inode);
	free(inode);
	inode=NULL;

	entry->inode=NULL;

    }

    destroy_entry(entry);

}

static void _remove_directory_cb (struct directory_s **directory, unsigned char when)
{

    if (when==0) {

	_lock_directory_excl(*directory);
	(*directory)->flags |= _DIRECTORY_FLAG_REMOVE;
	_unlock_directory_excl(*directory);

    } else {

	destroy_directory(*directory);
	*directory=NULL;

    }

}

/*
    remove contents of directory and 
    clear the skiplist
    remove from hash table
    destroy the directory

*/

static void _walk_directory(struct directory_s *directory, void (*cb_entry) (struct directory_s *directory, struct entry_s *entry), void (*cb_directory) (struct directory_s **directory, unsigned char when))
{
    struct entry_s *entry=NULL, *next=NULL;
    struct inode_s *inode=NULL;
    unsigned int error=0;

    cb_directory(&directory, 0);

    entry=(struct entry_s *) directory->first;

    while(entry) {

	inode=entry->inode;

	if (inode) {

	    if (S_ISDIR(inode->mode)) {
		struct directory_s *subdir=get_directory(inode);

		if (subdir) _walk_directory(subdir, cb_entry, cb_directory);

	    }

	}

	next=entry->name_next;

	(* cb_entry) (directory, entry);

	entry=next;

    }

    cb_directory(&directory, 1);

}

void walk_directory(struct directory_s *directory, void (*cb_entry) (struct directory_s *directory, struct entry_s *entry), void (*cb_directory) (struct directory_s **directory, unsigned char when))
{

    if (! cb_entry || ! cb_directory) return;
    _walk_directory(directory, cb_entry, cb_directory);

}

static void _default_entry_cb(struct entry_s *entry, void *ptr)
{
}

static void _clear_directory(struct directory_s *directory, void (*cb_entry)(struct entry_s *entry, void *ptr), void *ptr)
{
    struct entry_s *entry=NULL, *next=NULL;
    struct inode_s *inode=NULL;

    entry=(struct entry_s *) directory->first;

    while(entry) {

	inode=entry->inode;
	next=entry->name_next;

	(* cb_entry)(entry, ptr);

	if (inode) {

	    if (S_ISDIR(inode->mode)) {
		unsigned int error=0;
		union datalink_u *link=&inode->link;
		struct directory_s *subdir1=(struct directory_s *) link->data;
		struct directory_s *subdir2=NULL;

		subdir2=(subdir1) ? (* subdir1->dops->remove_directory)(inode, &error) : NULL;

		if (subdir2) {

		    /* do directory recursive */

		    free_pathcache(&subdir2->pathcalls);
		    _clear_directory(subdir2, cb_entry, ptr);
		    destroy_directory(subdir2);

		}

	    }

	    /* remove and free inode */

	    remove_inode(inode);
	    free(inode);
	    inode=NULL;

	    entry->inode=NULL;

	}

	destroy_entry(entry);
	entry=next;

    }

}

void clear_directory(struct directory_s *directory, void (*cb_entry)(struct entry_s *entry, void *ptr), void *ptr)
{

    if (! cb_entry) cb_entry=_default_entry_cb;
    _clear_directory(directory, cb_entry, ptr);

}

/*
    determine the entry when walking down a path
    if the entry does not exist, return NULL
*/

struct entry_s *walk_fuse_fs(struct entry_s *parent, char *path)
{
    struct entry_s *entry=NULL;
    struct name_s xname={NULL, 0, 0};
    unsigned int len=0;
    char *slash=NULL;
    unsigned int error=0;

    entry=NULL;

    xname.name=path;

    while(1) {

        /*  walk through path from begin to end and 
            check every part */

        slash=strchr(xname.name, '/');

        if (slash==xname.name) {

            xname.name++;
            if (*xname.name=='\0') break;
            continue;

        }

        if (slash) *slash='\0';

	xname.len=strlen(xname.name);
	calculate_nameindex(&xname);

	error=0;
        entry=find_entry(parent, &xname, &error);

	if (slash) {

	    *slash='/';

	    if (! entry) break;

	    parent=entry;
	    xname.name=slash+1;

	} else {

	    break;

	}

    }

    return entry;

}
