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
#include "utils.h"

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
#include "workspace-context.h"

extern void fs_inode_forget(struct inode_s *inode);

static struct directory_s dummy_directory;
static struct service_context_s dummy_context;
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
    struct directory_s *directory=get_directory(parent->inode);

    if (fs_lock_datalink(parent->inode)==0) {

	if ((directory->flags & _DIRECTORY_FLAG_DUMMY)==0) find_entry=directory->dops->find_entry;
	fs_unlock_datalink(parent->inode);

    }

    return (* find_entry)(parent, xname, error);
}

typedef void (*remove_entry_cb)(struct entry_s *entry, unsigned int *error);

static void _remove_entry_dummy(struct entry_s *entry, unsigned int *error)
{
    *error=EINVAL;
}

static void remove_entry_dummy(struct entry_s *entry, unsigned int *error)
{
    struct entry_s *parent=entry->parent;
    remove_entry_cb remove_entry=_remove_entry_dummy;
    struct directory_s *directory=get_directory(parent->inode);

    if (fs_lock_datalink(parent->inode)==0) {

	if ((directory->flags & _DIRECTORY_FLAG_DUMMY)==0) remove_entry=directory->dops->remove_entry;
	fs_unlock_datalink(parent->inode);

    }

    (* remove_entry)(entry, error);
}

/*
    insert an entry in a dummy directory
    since this is not possible (dummy directories do not have any entries) do create a
    "real" directory first
    note there is no lock required, this lock is already set(!)
*/

typedef struct entry_s *(*insert_entry_cb)(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags);

static struct entry_s *_insert_entry_dummy(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    *error=ENOMEM;
    return NULL;
}

static struct entry_s *insert_entry_dummy(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct entry_s *parent=entry->parent;
    insert_entry_cb insert_entry=_insert_entry_dummy;

    if (fs_lock_datalink(directory->inode)==0) {

	/* this can be also an cb like cb_get_real_directory */

	if (directory->flags & _DIRECTORY_FLAG_DUMMY) {

	    directory=(* directory->dops->create_directory)(parent->inode, error);
	    if (directory) insert_entry=directory->dops->insert_entry;

	}

	fs_unlock_datalink(directory->inode);

    }

    return (* insert_entry)(directory, entry, error, flags);

}

static void _init_directory(struct directory_s *directory)
{
    directory->dops=&default_dops;
    init_pathcalls(&directory->pathcalls);
}

static struct directory_s *create_directory(struct inode_s *inode, unsigned int *error)
{
    return _create_directory(inode, _init_directory, error);
}

struct directory_s *_remove_directory(struct inode_s *inode, unsigned int *error)
{
    struct directory_s *directory=NULL;

    if (fs_lock_datalink(inode)==0) {

	directory=get_directory(inode);

	if (directory->flags & _DIRECTORY_FLAG_DUMMY) {

	    directory=NULL;

	} else {
	    struct simple_lock_s wlock;

	    if ((* directory->dops->wlock)(inode, &wlock)==0) {

		directory->flags |= _DIRECTORY_FLAG_REMOVE;
		directory->dops=&removed_dops;
		directory->inode=NULL;

		(* directory->dops->unlock)(inode, &wlock);

	    }

	}

	fs_unlock_datalink(inode);

    }

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

typedef struct entry_s *(*insert_entry_batch_cb)(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags);

static struct entry_s *_insert_entry_batch_dummy(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    *error=ENOMEM;
    return NULL;
}

struct entry_s *insert_entry_batch_dummy(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct entry_s *parent=entry->parent;
    insert_entry_batch_cb insert_entry_batch=_insert_entry_batch_dummy;
    struct directory_s *real_directory=NULL;

    if (fs_lock_datalink(parent->inode)==0) {

	real_directory=get_directory(parent->inode);

	if (real_directory->flags & _DIRECTORY_FLAG_DUMMY) {

	    real_directory=(* directory->dops->create_directory)(parent->inode, error);
	    if (real_directory) insert_entry_batch=real_directory->dops->insert_entry_batch;

	}

	fs_unlock_datalink(parent->inode);

    }

    return (* insert_entry_batch)(real_directory, entry, error, flags);

}

static struct simple_lock_s *_create_lock_dummy_common(struct inode_s *inode, struct simple_lock_s *(*cb)(struct directory_s *d))
{
    struct directory_s *directory=NULL;

    if (fs_lock_datalink(inode)==0) {

	directory=get_directory(inode);

	if (directory->flags & _DIRECTORY_FLAG_DUMMY) {
	    unsigned int error=0;

	    directory=(* directory->dops->create_directory)(inode, &error);

	    if (directory==NULL) {

		fs_unlock_datalink(inode);
		return NULL;

	    }

	}

	fs_unlock_datalink(inode);

    }

    return cb(directory);
}

static struct simple_lock_s *create_rlock_dummy(struct inode_s *inode)
{
    return _create_lock_dummy_common(inode, _create_rlock_directory);
}

static struct simple_lock_s *create_wlock_dummy(struct inode_s *inode)
{
    return _create_lock_dummy_common(inode, _create_wlock_directory);
}

static int _lock_dummy_common(struct inode_s *inode, struct simple_lock_s *lock, int (* cb)(struct directory_s *d, struct simple_lock_s *l))
{
    struct directory_s *directory=NULL;

    if (fs_lock_datalink(inode)==0) {

	directory=get_directory(inode);

	if (directory->flags & _DIRECTORY_FLAG_DUMMY) {
	    unsigned int error=0;

	    directory=(* directory->dops->create_directory)(inode, &error);

	    if (! directory) {

		fs_unlock_datalink(inode);
		return -1;

	    }

	}

	fs_unlock_datalink(inode);

    }

    return cb(directory, lock);

}

static int rlock_dummy(struct inode_s *inode, struct simple_lock_s *lock)
{
    return _lock_dummy_common(inode, lock, _rlock_directory);
}

static int wlock_dummy(struct inode_s *inode, struct simple_lock_s *lock)
{
    return _lock_dummy_common(inode, lock, _wlock_directory);
}

static int lock_dummy(struct inode_s *inode, struct simple_lock_s *lock)
{
    return _lock_dummy_common(inode, lock, _lock_directory);
}

static int unlock_dummy(struct inode_s *inode, struct simple_lock_s *lock)
{
    return _lock_dummy_common(inode, lock, _unlock_directory);
}

static int upgradelock_dummy(struct inode_s *inode, struct simple_lock_s *lock)
{
    return _lock_dummy_common(inode, lock, _upgradelock_directory);
}

static int prelock_dummy(struct inode_s *inode, struct simple_lock_s *lock)
{
    return _lock_dummy_common(inode, lock, _prelock_directory);
}

static struct pathcalls_s *get_pathcalls_dummy(struct inode_s *inode)
{
    struct pathcalls_s *pathcalls=NULL;

    if (fs_lock_datalink(inode)==0) {
	struct directory_s *directory=NULL;

	directory=get_directory(inode);

	if (directory->flags & _DIRECTORY_FLAG_DUMMY) {
	    unsigned int error=0;

	    directory=(* directory->dops->create_directory)(inode, &error);

	    if (directory) pathcalls=&directory->pathcalls;

	} else {

	    pathcalls=&directory->pathcalls;

	}

	fs_unlock_datalink(inode);

    }

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
    .create_rlock		= create_rlock_dummy,
    .create_wlock		= create_wlock_dummy,
    .rlock			= rlock_dummy,
    .wlock			= wlock_dummy,
    .lock			= lock_dummy,
    .unlock			= unlock_dummy,
    .upgradelock		= upgradelock_dummy,
    .prelock			= prelock_dummy,
    .get_pathcalls		= get_pathcalls_dummy
};

/* common entry function (find, insert, remove) for a normal directory */

static struct entry_s *find_entry_common(struct entry_s *parent, struct name_s *xname, unsigned int *error)
{
    struct directory_s *directory=get_directory(parent->inode);
    unsigned int row=0;

    *error=0;
    return (struct entry_s *) find_sl(&directory->skiplist, (void *) xname, &row, error);

}

static void remove_entry_common(struct entry_s *entry, unsigned int *error)
{
    struct entry_s *parent=entry->parent;

    if (parent) {
	struct directory_s *directory=get_directory(parent->inode);
	struct name_s *lookupname=&entry->name;
	unsigned int row=0;

	delete_sl(&directory->skiplist, (void *) lookupname, &row, error);

    }

}

static struct entry_s *insert_entry_common(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct entry_s *parent=entry->parent;
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

static struct simple_lock_s *create_rlock_common(struct inode_s *inode)
{
    struct directory_s *directory=get_directory(inode);
    return _create_rlock_directory(directory);
}

static struct simple_lock_s *create_wlock_common(struct inode_s *inode)
{
    struct directory_s *directory=get_directory(inode);
    return _create_wlock_directory(directory);
}

static int rlock_common(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return _rlock_directory(directory, lock);
}

static int wlock_common(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return _wlock_directory(directory, lock);
}

static int lock_common(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return _lock_directory(directory, lock);
}

static int unlock_common(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return _unlock_directory(directory, lock);
}

static int upgradelock_common(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return _upgradelock_directory(directory, lock);
}

static int prelock_common(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return _prelock_directory(directory, lock);
}

static struct pathcalls_s *get_pathcalls_common(struct inode_s *inode)
{
    struct directory_s *directory=get_directory(inode);
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
    .create_rlock		= create_rlock_common,
    .create_wlock		= create_wlock_common,
    .rlock			= rlock_common,
    .wlock			= wlock_common,
    .lock			= lock_common,
    .unlock			= unlock_common,
    .upgradelock		= upgradelock_common,
    .prelock			= prelock_common,
    .get_pathcalls		= get_pathcalls_common,
};

/* entry functions when a directory is set as removed */

static struct entry_s *find_entry_removed(struct entry_s *p, struct name_s *xname, unsigned int *error)
{
    *error=ENOTDIR;
    return NULL;
}

static void remove_entry_removed(struct entry_s *e, unsigned int *error)
{
    *error=ENOTDIR;
    return;
}

static struct entry_s *insert_entry_removed(struct directory_s *d, struct entry_s *e, unsigned int *error, unsigned short flags)
{
    *error=ENOTDIR;
    return NULL;
}

static struct directory_s *create_directory_removed(struct inode_s *inode, unsigned int *error)
{
    *error=ENOTDIR;
    return NULL;
}

static struct directory_s *remove_directory_removed(struct inode_s *inode, unsigned int *error)
{
    *error=ENOTDIR;
    return NULL;
}

static struct entry_s *find_entry_removed_batch(struct directory_s *d, struct name_s *xname, unsigned int *error)
{
    *error=ENOTDIR;
    return NULL;
}

static void remove_entry_removed_batch(struct directory_s *d, struct entry_s *entry, unsigned int *error)
{
    *error=ENOTDIR;
    return;
}

static struct entry_s *insert_entry_removed_batch(struct directory_s *d, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    *error=ENOTDIR;
    return NULL;
}

struct simple_lock_s *create_lock_removed(struct inode_s *inode)
{
    return NULL;
}

static int lock_removed(struct inode_s *inode, struct simple_lock_s *lock)
{
    return -1;
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
    .create_rlock		= create_lock_removed,
    .create_wlock		= create_lock_removed,
    .rlock			= lock_removed,
    .wlock			= lock_removed,
    .lock			= lock_removed,
    .unlock			= lock_removed,
    .upgradelock		= lock_removed,
    .prelock			= lock_removed,
    .get_pathcalls		= get_pathcalls_removed,
};

/* simple functions which call the right function for the directory */

struct entry_s *find_entry(struct entry_s *parent, struct name_s *xname, unsigned int *error)
{
    struct directory_s *directory=get_directory(parent->inode);
    return (* directory->dops->find_entry)(parent, xname, error);
}

void remove_entry(struct entry_s *entry, unsigned int *error)
{
    struct entry_s *parent=entry->parent;
    struct directory_s *directory=get_directory(parent->inode);
    (* directory->dops->remove_entry)(entry, error);
}

struct entry_s *insert_entry(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct entry_s *parent=entry->parent;
    return (* directory->dops->insert_entry)(directory, entry, error, flags);
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
    struct directory_s *directory=get_directory(inode);
    return (* directory->dops->remove_directory)(inode, error);
}

struct simple_lock_s *create_rlock_directory(struct inode_s *inode)
{
    struct directory_s *directory=get_directory(inode);
    return (* directory->dops->create_rlock)(inode);
}

struct simple_lock_s *create_wlock_directory(struct inode_s *inode)
{
    struct directory_s *directory=get_directory(inode);
    return (* directory->dops->create_wlock)(inode);
}

int rlock_directory(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return (* directory->dops->rlock)(inode, lock);
}

int wlock_directory(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return (* directory->dops->wlock)(inode, lock);
}

int lock_directory(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return (* directory->dops->lock)(inode, lock);
}

int unlock_directory(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return (* directory->dops->unlock)(inode, lock);
}

int upgradelock_directory(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return (* directory->dops->upgradelock)(inode, lock);
}

int prelock_directory(struct inode_s *inode, struct simple_lock_s *lock)
{
    struct directory_s *directory=get_directory(inode);
    return (* directory->dops->prelock)(inode, lock);
}

struct pathcalls_s *get_pathcalls(struct inode_s *inode)
{
    struct directory_s *directory=get_directory(inode);
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

    /* dummy_directory -> dummy context -> virtual fs */

    
}

struct directory_s *get_dummy_directory()
{
    return &dummy_directory;
}

/*
    FUNCTIONS to CREATE an entry and inode
*/

static struct entry_s *_cb_create_entry(struct entry_s *parent, struct name_s *name)
{
    return create_entry(parent, name);
}
static struct inode_s *_cb_create_inode(unsigned int s)
{
    return create_inode(s);
}
static struct entry_s *_cb_insert_entry(struct directory_s *directory, struct entry_s *entry, unsigned int flags, unsigned int *error)
{
    return insert_entry(directory, entry, error, flags);
}
static struct entry_s *_cb_insert_entry_batch(struct directory_s *directory, struct entry_s *entry, unsigned int flags, unsigned int *error)
{
    return insert_entry_batch(directory, entry, error, flags);
}
static void _cb_adjust_pathmax_default(struct create_entry_s *ce)
{
    adjust_pathmax(ce->context->workspace, ce->pathlen);
}
static void _cb_context_created(struct create_entry_s *ce, struct entry_s *entry)
{
    add_inode_context(ce->context, entry->inode);
}
static void _cb_context_found(struct create_entry_s *ce, struct entry_s *entry)
{
}
static unsigned int _cb_cache_size(struct create_entry_s *ce)
{
    return 0;
}
static void _cb_cache_created(struct entry_s *entry, struct create_entry_s *ce)
{
    fill_inode_stat(entry->inode, &ce->cache.st);
    entry->inode->st.st_mode=ce->cache.st.st_mode;
    entry->inode->st.st_size=ce->cache.st.st_size;
    entry->inode->st.st_nlink=ce->cache.st.st_nlink;
}
static void _cb_cache_found(struct entry_s *entry, struct create_entry_s *ce)
{
    fill_inode_stat(entry->inode, &ce->cache.st);
    entry->inode->st.st_mode=ce->cache.st.st_mode;
    entry->inode->st.st_size=ce->cache.st.st_size;
    entry->inode->st.st_nlink=ce->cache.st.st_nlink;
}

static void _cb_created_default(struct entry_s *entry, struct create_entry_s *ce)
{
    struct service_context_s *context=ce->context;
    struct inode_s *inode=entry->inode;
    struct entry_s *parent=entry->parent;

    logoutput("_cb_created_default: name %s", entry->name.name);

    inode->nlookup=1;
    inode->st.st_nlink=1;

    get_current_time(&inode->stim); /* sync time */
    memcpy(&parent->inode->st.st_ctim, &inode->stim, sizeof(struct timespec)); /* change the ctime of parent directory since it's attr are changed */
    memcpy(&parent->inode->st.st_mtim, &inode->stim, sizeof(struct timespec)); /* change the mtime of parent directory since an entry is added */

    (* ce->cb_adjust_pathmax)(ce); /* adjust the maximum path len */
    (* ce->cb_cache_created)(entry, ce); /* create the inode stat and cache */
    (* ce->cb_context_created)(ce, entry); /* context depending cb, like a FUSE reply and adding inode to context, set fs etc */

    if (S_ISDIR(inode->st.st_mode)) {

	inode->st.st_nlink++;
	parent->inode->st.st_nlink++;

    }

}

static void _cb_found_default(struct entry_s *entry, struct create_entry_s *ce)
{
    struct service_context_s *context=ce->context;
    struct stat *st=&ce->cache.st;
    struct inode_s *inode=entry->inode;

    // logoutput("_cb_found_default: name %s", entry->name.name);

    inode->nlookup++;
    get_current_time(&inode->stim);

    /* when just created (for example by readdir) adjust the pathcache */

    if (inode->nlookup==1) (* ce->cb_adjust_pathmax)(ce); /* adjust the maximum path len */
    (* ce->cb_cache_found)(entry, ce); /* get/set the inode stat cache */
    (* ce->cb_context_found)(ce, entry); /* context depending cb, like a FUSE reply and adding inode to context, set fs etc */

}

static void _cb_error_default(struct entry_s *parent, struct name_s *xname, struct create_entry_s *ce, unsigned int error)
{
    if (error==0) error=EIO;
    logoutput("_cb_error_default: error %i (%s)", error, strerror(error));
}

static struct directory_s *get_directory_01(struct create_entry_s *ce)
{
    struct inode_s *inode=ce->tree.parent->inode;
    return get_directory(inode);
}

static struct directory_s *get_directory_02(struct create_entry_s *ce)
{
    return ce->tree.directory;
}

static struct directory_s *get_directory_03(struct create_entry_s *ce)
{
    struct inode_s *inode=ce->tree.opendir->inode;
    return get_directory(inode);
}

void init_create_entry(struct create_entry_s *ce, struct name_s *n, struct entry_s *p, struct directory_s *d, struct fuse_opendir_s *fo, struct service_context_s *c, struct stat *st, void *ptr)
{
    memset(ce, 0, sizeof(struct create_entry_s));

    ce->name=n;

    if (p) {

	ce->tree.parent=p;
	ce->get_directory=get_directory_01;

    } else if (d) {

	ce->tree.directory=d;
	ce->get_directory=get_directory_02;

    } else if (fo) {

	ce->tree.opendir=fo;
	ce->get_directory=get_directory_03;

    }

    ce->context=c;
    if (st) memcpy(&ce->cache.st, st, sizeof(struct stat));
    ce->flags=0;
    ce->ptr=ptr;
    ce->error=0;

    ce->cb_create_entry=_cb_create_entry;
    ce->cb_create_inode=_cb_create_inode;
    ce->cb_insert_entry=_cb_insert_entry;

    ce->cb_created=_cb_created_default;
    ce->cb_found=_cb_found_default;
    ce->cb_error=_cb_error_default;

    ce->cb_cache_size=_cb_cache_size;
    ce->cb_cache_created=_cb_cache_created;
    ce->cb_cache_found=_cb_cache_found;

    ce->cb_adjust_pathmax=_cb_adjust_pathmax_default;
    ce->cb_context_created=_cb_context_created;
    ce->cb_context_found=_cb_context_found;

}

static struct entry_s *_create_entry_extended_common(struct create_entry_s *ce)
{
    struct entry_s *entry=NULL, *result=NULL;
    struct inode_s *inode=NULL;
    unsigned int error=0;
    struct entry_s *parent=NULL;
    struct directory_s *directory=NULL;
    unsigned int cache_size=(* ce->cb_cache_size)(ce);

    directory=(* ce->get_directory)(ce);
    parent=directory->inode->alias;

    entry=(* ce->cb_create_entry)(parent, ce->name);
    inode=(* ce->cb_create_inode)(cache_size);

    if (entry && inode) {

	result=(* ce->cb_insert_entry)(directory, entry, 0, &error);

	if (error==0) {

	    /* new */

	    inode->alias=entry;
	    entry->inode=inode;

	    (* ce->cb_created)(entry, ce);

	} else {

	    logoutput("_create_entry_extended_common: error insert entry %.*s", ce->name->len, ce->name->name);

	    if (error==EEXIST) {

		/* existing found */

		destroy_entry(entry);
		entry=result;
		free(inode);
		inode=entry->inode;

		error=0;
		if (cache_size != inode->cache_size) inode=realloc_inode(inode, cache_size); /* assume this always goes good .... */
		(* ce->cb_found)(entry, ce);

	    } else {

		/* another error */

		destroy_entry(entry);
		free(inode);
		(* ce->cb_error) (parent, ce->name, ce, error);
		return NULL;

	    }

	}

    } else {

	/* unable to allocate entry and/or inode */

	if (entry) destroy_entry(entry);
	if (inode) free(inode);
	(* ce->cb_error) (parent, ce->name, ce, error);
	error=ENOMEM;
	return NULL;

    }

    return entry;

}

struct entry_s *create_entry_extended(struct create_entry_s *ce)
{
    logoutput("create_entry_extended: add %.*s", ce->name->len, ce->name->name);
    return _create_entry_extended_common(ce);
}

/* use directory to add entry and inode, directory has to be locked */

struct entry_s *create_entry_extended_batch(struct create_entry_s *ce)
{

    ce->cb_insert_entry=_cb_insert_entry_batch;
    logoutput("create_entry_extended_batch: add %.*s", ce->name->len, ce->name->name);
    return _create_entry_extended_common(ce);
}

/*
    remove contents of directory and 
    clear the skiplist
    remove from hash table
    destroy the directory

*/


static void _default_entry_cb(struct entry_s *entry, void *ptr)
{
}

static void _clear_directory(struct context_interface_s *i, struct directory_s *directory, void (*cb_entry)(struct entry_s *entry, void *ptr), void *ptr)
{
    struct entry_s *entry=NULL, *next=NULL;
    struct inode_s *inode=NULL;

    entry=(struct entry_s *) directory->first;

    while(entry) {

	inode=entry->inode;
	next=entry->name_next;

	(* cb_entry)(entry, ptr);

	if (inode) {

	    if (S_ISDIR(inode->st.st_mode)) {
		unsigned int error=0;
		struct directory_s *subdir1=get_directory(inode);
		struct directory_s *subdir2=NULL;

		subdir2=(subdir1) ? (* subdir1->dops->remove_directory)(inode, &error) : NULL;

		if (subdir2) {

		    /* do directory recursive */

		    free_pathcache(&subdir2->pathcalls);
		    _clear_directory(i, subdir2, cb_entry, ptr);
		    destroy_directory(subdir2);

		}

	    }

	    /* remove and free inode */

	    remove_inode(i, inode);

	} else {

	    destroy_entry(entry);

	}

	entry=next;

    }

}

void clear_directory(struct context_interface_s *i, struct directory_s *directory, void (*cb_entry)(struct entry_s *entry, void *ptr), void *ptr)
{

    if (! cb_entry) cb_entry=_default_entry_cb;
    _clear_directory(i, directory, cb_entry, ptr);

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
