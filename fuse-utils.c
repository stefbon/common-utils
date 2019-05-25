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

#include "fuse-dentry.h"
#include "fuse-directory.h"
#include "fuse-utils.h"
#include "fuse-fs.h"
#include "workspaces.h"
#include "path-caching.h"
#include "workspace-context.h"

extern void fs_inode_forget(struct inode_s *inode);

static struct directory_s dummy_directory;
static struct dops_s dummy_dops;
static struct dops_s default_dops;
static struct dops_s removed_dops;

typedef struct entry_s *(*find_entry_cb)(struct entry_s *parent, struct name_s *xname, unsigned int *error);
typedef void (*remove_entry_cb)(struct entry_s *entry, unsigned int *error);
typedef struct entry_s *(*insert_entry_cb)(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags);
typedef struct entry_s *(*insert_entry_batch_cb)(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags);

/* DUMMY DIRECTORY OPS */

static void _init_directory(struct directory_s *directory)
{
    directory->dops=&default_dops;
    init_pathcalls(&directory->pathcalls);
}

static struct directory_s *get_directory_dummy(struct inode_s *inode, unsigned int *error)
{
    struct directory_s *directory=_create_directory(inode, _init_directory, error);

    if (directory) {

	inode->link.type=INODE_LINK_TYPE_DIRECTORY;
	inode->link.link.ptr=(void *) directory;

    } else {

	directory=&dummy_directory;

    }

    return directory;
}

struct directory_s *remove_directory_dummy(struct inode_s *inode, unsigned int *error)
{
    return NULL;
}

static struct dops_s dummy_dops = {
    .get_directory		= get_directory_dummy,
    .remove_directory		= remove_directory_dummy,
};

/* DEFAULT DIRECTORY OPS */

static struct directory_s *get_directory_common(struct inode_s *inode, unsigned int *error)
{
    return (struct directory_s *) inode->link.link.ptr;
}

static struct directory_s *remove_directory_common(struct inode_s *inode, unsigned int *error)
{
    struct directory_s *directory=(struct directory_s *) inode->link.link.ptr;
    directory->flags|=_DIRECTORY_FLAG_REMOVE;
    directory->dops=&removed_dops;
    _remove_directory_hashtable(directory);
    directory->inode=NULL;
    set_directory_dump(inode, get_dummy_directory());
    return directory;
}

static void get_inode_link_common(struct directory_s *directory, struct inode_s *inode, struct inode_link_s **link)
{
    *link=&directory->link;
}



static struct dops_s default_dops = {
    .get_directory		= get_directory_common,
    .remove_directory		= remove_directory_common,
};

/* REMOVED DIRECTORY OPS */

static struct directory_s *get_directory_removed(struct inode_s *inode, unsigned int *error)
{
    return (struct directory_s *) inode->link.link.ptr;
}

static struct directory_s *remove_directory_removed(struct inode_s *inode, unsigned int *error)
{
    *error=ENOTDIR;
    return NULL;
}

static struct dops_s removed_dops = {
    .get_directory		= get_directory_removed,
    .remove_directory		= remove_directory_removed,
};

/* simple functions which call the right function for the directory */

struct directory_s *get_directory(struct inode_s *inode, unsigned int *error)
{
    struct simple_lock_s wlock;
    struct directory_s *directory=&dummy_directory;

    init_simple_writelock(&dummy_directory.locking, &wlock);

    if (simple_lock(&wlock)==0) {
	struct directory_s *tmp=(struct directory_s *) inode->link.link.ptr;

	directory=(* tmp->dops->get_directory)(inode, error);
	simple_unlock(&wlock);

    }

    return directory;

}

struct directory_s *remove_directory(struct inode_s *inode, unsigned int *error)
{
    struct simple_lock_s wlock;
    struct directory_s *directory=&dummy_directory;

    init_simple_writelock(&dummy_directory.locking, &wlock);

    if (simple_lock(&wlock)==0) {
	struct directory_s *tmp=(struct directory_s *) inode->link.link.ptr;

	directory=(* directory->dops->remove_directory)(inode, error);
	simple_unlock(&wlock);

    }

    return directory;

}

struct entry_s *find_entry(struct directory_s *directory, struct name_s *lookupname, unsigned int *error)
{
    unsigned int row=0;
    return (struct entry_s *) find_sl(&directory->skiplist, (void *) lookupname, &row, error);
}

void remove_entry(struct directory_s *directory, struct entry_s *entry, unsigned int *error)
{
    struct name_s *lookupname=&entry->name;
    unsigned int row=0;
    delete_sl(&directory->skiplist, (void *) lookupname, &row, error);
}

struct entry_s *insert_entry(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct name_s *lookupname=&entry->name;
    unsigned int row=0;
    unsigned short sl_flags=(flags & _ENTRY_FLAG_TEMP) ? _SL_INSERT_FLAG_NOLANE : 0;
    return (struct entry_s *)insert_sl(&directory->skiplist, (void *) lookupname, &row, error, (void *) entry, sl_flags);
}

struct entry_s *find_entry_batch(struct directory_s *directory, struct name_s *lookupname, unsigned int *error)
{
    unsigned int row=0;
    return (struct entry_s *) find_sl_batch(&directory->skiplist, (void *) lookupname, &row, error);
}

void remove_entry_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error)
{
    struct name_s *lookupname=&entry->name;
    unsigned int row=0;
    delete_sl_batch(&directory->skiplist, (void *) lookupname, &row, error);
}

struct entry_s *insert_entry_batch(struct directory_s *directory, struct entry_s *entry, unsigned int *error, unsigned short flags)
{
    struct name_s *lookupname=&entry->name;
    unsigned int row=0;
    unsigned short sl_flags=(flags & _ENTRY_FLAG_TEMP) ? _SL_INSERT_FLAG_NOLANE : 0;
    return (struct entry_s *) insert_sl_batch(&directory->skiplist, (void *) lookupname, &row, error, (void *) entry, sl_flags);
}

struct pathcalls_s *get_pathcalls(struct directory_s *d)
{
    return &d->pathcalls;
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

    /* make inode_link point to directory self */
    dummy_directory.link.type=INODE_LINK_TYPE_DIRECTORY;
    dummy_directory.link.link.ptr=&dummy_directory;
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

    // logoutput("_cb_created_default: name %s", entry->name.name);

    inode->nlookup=1;
    inode->st.st_nlink=1;

    get_current_time(&inode->stim); 							/* sync time */
    memcpy(&parent->inode->st.st_ctim, &inode->stim, sizeof(struct timespec)); 		/* change the ctime of parent directory since it's attr are changed */
    memcpy(&parent->inode->st.st_mtim, &inode->stim, sizeof(struct timespec)); 		/* change the mtime of parent directory since an entry is added */

    (* ce->cb_adjust_pathmax)(ce); 							/* adjust the maximum path len */
    (* ce->cb_cache_created)(entry, ce); 						/* create the inode stat and cache */
    (* ce->cb_context_created)(ce, entry); 						/* context depending cb, like a FUSE reply and adding inode to context, set fs etc */

    if (S_ISDIR(inode->st.st_mode)) {

	inode->st.st_nlink++;
	parent->inode->st.st_nlink++;
	set_directory_dump(inode, get_dummy_directory());

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
    unsigned int error=0;
    return get_directory(inode, &error);
}

static struct directory_s *get_directory_02(struct create_entry_s *ce)
{
    return ce->tree.directory;
}

static struct directory_s *get_directory_03(struct create_entry_s *ce)
{
    struct inode_s *inode=ce->tree.opendir->inode;
    unsigned int error=0;
    return get_directory(inode, &error);
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
    // logoutput("create_entry_extended: add %.*s", ce->name->len, ce->name->name);
    return _create_entry_extended_common(ce);
}

/* use directory to add entry and inode, directory has to be locked */

struct entry_s *create_entry_extended_batch(struct create_entry_s *ce)
{

    ce->cb_insert_entry=_cb_insert_entry_batch;
    // logoutput("create_entry_extended_batch: add %.*s", ce->name->len, ce->name->name);
    return _create_entry_extended_common(ce);
}

/*
    remove contents of directory and 
    clear the skiplist
    remove from hash table
    destroy the directory

*/

static void _clear_directory(struct context_interface_s *i, struct directory_s *directory, char *path, unsigned int len, unsigned int level)
{
    struct entry_s *entry=NULL, *next=NULL;
    struct inode_s *inode=NULL;

    logoutput("_clear_directory: level %i path %s", level, path);

    entry=(struct entry_s *) directory->first;

    while (entry) {

	logoutput("_clear_directory: found %.*s", entry->name.len, entry->name.name);
	inode=entry->inode;
	next=entry->name_next;

	if (inode) {

	    logoutput("_clear_directory: A");

	    (* inode->fs->forget)(inode);

	    logoutput("_clear_directory: B");

	    if (S_ISDIR(inode->st.st_mode)) {
		unsigned int error=0;
		struct directory_s *subdir=remove_directory(inode, &error);

		if (subdir) {
		    struct name_s *xname=&entry->name;
		    unsigned int keep=len;

		    logoutput("_clear_directory: C");

		    path[len]='/';
		    len++;
		    memcpy(&path[len], xname->name, xname->len);
		    len+=xname->len;
		    path[len]='\0';
		    len++;

		    logoutput("_clear_directory: D");

		    /* do directory recursive */

		    free_pathcache(&subdir->pathcalls);
		    _clear_directory(i, subdir, path, len, level+1);
		    destroy_directory(subdir);
		    len=keep;
		    memset(&path[len], 0, sizeof(path) - len);

		    logoutput("_clear_directory: E");

		}

	    }

	    /* remove and free inode */

	    inode->alias=NULL;
	    free(inode);
	    entry->inode=NULL;

	}

	destroy_entry(entry);
	entry=next;

    }

}

void clear_directory(struct context_interface_s *i, struct directory_s *directory)
{
    struct service_context_s *context=get_service_context(i);
    struct workspace_mount_s *workspace=context->workspace;
    char path[workspace->pathmax];

    memset(path, 0, workspace->pathmax);
    _clear_directory(i, directory, path, 0, 0);
}

int get_inode_link_directory(struct inode_s *inode, struct inode_link_s *link)
{
    unsigned int error=0;
    struct directory_s *directory=get_directory(inode, &error);
    memcpy(link, &directory->link, sizeof(struct inode_link_s));
    return 0;
}

void set_inode_link_directory(struct inode_s *inode, struct inode_link_s *link)
{
    unsigned int error=0;
    struct directory_s *directory=get_directory(inode, &error);
    memcpy(&directory->link, link, sizeof(struct inode_link_s));
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
