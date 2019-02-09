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

#include "utils.h"
#include "fuse-dentry.h"
#include "workerthreads.h"
#include "simple-locking.h"
#include "fuse-interface.h"
#include "fuse-directory.h"
#include "fuse-utils.h"

#include "workspaces.h"
#include "workspace-context.h"

#ifndef SIZE_INODE_HASHTABLE
#define SIZE_INODE_HASHTABLE				10240
#endif

#include "logging.h"

static struct inode_s **inode_hash_table=NULL;
static pthread_mutex_t inode_table_mutex=PTHREAD_MUTEX_INITIALIZER;

static uint64_t inoctr=FUSE_ROOT_ID;
static pthread_mutex_t inodectrmutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned long long nrinodes=0;

struct inode_2delete_s {
    ino_t				ino;
    dev_t 				unique;
    unsigned int			flags;
    uint64_t				forget;
    struct inode_2delete_s		*next;
};

static pthread_t inode_2delete_threadid=0;
static struct inode_2delete_s *inode_2delete_list=NULL;
static pthread_mutex_t inode_2delete_mutex=PTHREAD_MUTEX_INITIALIZER;

#define NAMEINDEX_ROOT1						92			/* number of valid chars*/
#define NAMEINDEX_ROOT2						8464			/* 92 ^ 2 */
#define NAMEINDEX_ROOT3						778688			/* 92 ^ 3 */
#define NAMEINDEX_ROOT4						71639296		/* 92 ^ 4 */
#define NAMEINDEX_ROOT5						6590815232		/* 92 ^ 5 */

void calculate_nameindex(struct name_s *name)
{
    char buffer[6];

    memset(buffer, 32, 6);

    if (name->len>5) {

	memcpy(buffer, name->name, 6);

    } else {

	memcpy(buffer, name->name, name->len);

    }

    unsigned char firstletter=*buffer-32;
    unsigned char secondletter=*(buffer+1)-32;
    unsigned char thirdletter=*(buffer+2)-32;
    unsigned char fourthletter=*(buffer+3)-32;
    unsigned char fifthletter=*(buffer+4)-32;
    unsigned char sixthletter=*(buffer+5)-32;

    name->index=(firstletter * NAMEINDEX_ROOT5) + (secondletter * NAMEINDEX_ROOT4) + (thirdletter * NAMEINDEX_ROOT3) + (fourthletter * NAMEINDEX_ROOT2) + (fifthletter * NAMEINDEX_ROOT1) + sixthletter;

}

int init_inode_hashtable(unsigned int *error)
{
    int result=0;

    inode_hash_table = calloc(SIZE_INODE_HASHTABLE, sizeof(struct inode_s *));

    if ( ! inode_hash_table ) {

	*error=ENOMEM;
	result=-1;

    }

    return result;

}

void free_inode_hashtable()
{

    if (inode_hash_table) {

	free(inode_hash_table);
	inode_hash_table=NULL;

    }

}

/*
    function to assign an ino number to the inode and add the inode
    to the inode hash table

    this can be done using one mutex

    note that the ino is required to add to the hash table, so first get a new ino, and than add to the table

*/

void add_inode_hashtable(struct inode_s *inode, void (*cb) (void *data), void *data)
{
    size_t hash = 0;

    pthread_mutex_lock(&inode_table_mutex);

    inoctr++;
    inode->st.st_ino=inoctr;

    hash = inoctr % SIZE_INODE_HASHTABLE;

    inode->id_next = inode_hash_table[hash];
    inode->id_next = NULL;
    inode_hash_table[hash] = inode;

    pthread_mutex_unlock(&inode_table_mutex);

    (* cb) (data);

}

void init_entry(struct entry_s *entry)
{
    entry->inode=NULL;

    entry->name.name=NULL;
    entry->name.len=0;
    entry->name.index=0;

    entry->parent=NULL;
    entry->name_next=NULL;
    entry->name_prev=NULL;

    entry->flags=0;

}

/*

    allocate an entry and name

    TODO: for the name is now a seperate pointer used entry->name.name, but it would
    be better to use an array for the name
*/

struct entry_s *create_entry(struct entry_s *parent, struct name_s *xname)
{
    struct entry_s *entry;
    char *name;

    entry = malloc(sizeof(struct entry_s));
    name = malloc(xname->len + 1);

    if (entry && name) {

	memset(entry, 0, sizeof(struct entry_s));
	init_entry(entry);

	entry->name.name = name;
	memcpy(name, xname->name, xname->len);
	name[xname->len]='\0'; /* terminating zero */

	entry->name.len=xname->len;
	entry->name.index=xname->index;

	entry->parent = parent;

    } else {

	if (entry) {

	    free(entry);
	    entry=NULL;

	}

	if (name) {

	    free(name);
	    name=NULL;

	}

    }

    return entry;

}

void rename_entry(struct entry_s *entry, char **name, unsigned int len)
{

    if (entry->name.name) free(entry->name.name);

    entry->name.name=*name;
    entry->name.len=len;
    entry->name.index=0;

    *name=NULL;

    calculate_nameindex(&entry->name);

}

void destroy_entry(struct entry_s *entry)
{

    if ( entry->name.name) {

	free(entry->name.name);
	entry->name.name=NULL;

    }

    if ( entry->inode ) {

	entry->inode->alias=NULL;
	entry->inode=NULL;

    }

    free(entry);

}

void init_inode(struct inode_s *inode)
{
    struct stat *st=&inode->st;

    memset(inode, 0, sizeof(struct inode_s) + inode->cache_size);

    inode->flags=0;
    inode->id_next=NULL;
    inode->id_prev=NULL;

    inode->alias=NULL;
    inode->nlookup=0;

    st->st_ino=0;
    st->st_mode=0;
    st->st_nlink=0;
    st->st_uid=(uid_t) -1;
    st->st_gid=(gid_t) -1;
    st->st_size=0;

    /* used for context->unique */
    st->st_rdev=0;
    st->st_dev=0;

    st->st_blksize=0;
    st->st_blocks=0;

    st->st_mtim.tv_sec=0;
    st->st_mtim.tv_nsec=0;
    st->st_ctim.tv_sec=0;
    st->st_ctim.tv_nsec=0;
    st->st_atim.tv_sec=0;
    st->st_atim.tv_nsec=0;

    /* synctime */
    inode->stim.tv_sec=0;
    inode->stim.tv_nsec=0;

    inode->link.type=0;
    inode->link.link.ptr=NULL;

}

void get_inode_stat(struct inode_s *inode, struct stat *st)
{
    memcpy(st, &inode->st, sizeof(struct stat));
}

void fill_inode_stat(struct inode_s *inode, struct stat *st)
{
    //inode->mode=st->st_mode;
    //inode->nlink=st->st_nlink;

    inode->st.st_uid=st->st_uid;
    inode->st.st_gid=st->st_gid;

    //inode->size=st->st_size;

    inode->st.st_mtim.tv_sec=st->st_mtim.tv_sec;
    inode->st.st_mtim.tv_nsec=st->st_mtim.tv_nsec;

    inode->st.st_ctim.tv_sec=st->st_ctim.tv_sec;
    inode->st.st_ctim.tv_nsec=st->st_ctim.tv_nsec;

    inode->st.st_atim.tv_sec=st->st_atim.tv_sec;
    inode->st.st_atim.tv_nsec=st->st_atim.tv_nsec;

}

struct inode_s *create_inode(unsigned int cache_size)
{
    struct inode_s *inode=NULL;

    inode = malloc(sizeof(struct inode_s) + cache_size);

    if (inode) {

	inode->cache_size=cache_size;
	init_inode(inode);

    }

    return inode;

}

void free_inode(struct inode_s *inode)
{
    free(inode);
}

struct inode_s *realloc_inode(struct inode_s *inode, unsigned int new)
{
    struct inode_s *keep=inode;

    inode=realloc(inode, sizeof(struct inode_s) + new); /* assume always good */

    if (inode != keep) {
	size_t hash=0;
	struct inode_s *next=NULL;
	struct inode_s *prev=NULL;

	if (inode==NULL) return NULL;
	hash=inode->st.st_ino % SIZE_INODE_HASHTABLE;
	next=inode->id_next;
	prev=inode->id_prev;

	/* repair */

	if (next) next->id_prev=inode;
	if (prev) {

	    prev->id_next=inode;

	} else {

	    /* no prev means it's the first */
	    inode_hash_table[hash]=inode;

	}

    }

    inode->cache_size=new;
    return inode;
}

struct inode_s *find_inode(ino_t ino)
{
    size_t hash=ino % SIZE_INODE_HASHTABLE;
    struct inode_s *inode=NULL;

    pthread_mutex_lock(&inode_table_mutex);
    inode=inode_hash_table[hash];

    while(inode) {

	if (inode->st.st_ino==ino) break;
	inode=inode->id_next;

    }

    pthread_mutex_unlock(&inode_table_mutex);
    return inode;

}

static void inode_2delete_thread(void *ptr)
{
    struct inode_2delete_s *i2d=NULL;
    size_t hash=0;
    struct inode_s *inode=NULL;

    geti2d:

    pthread_mutex_lock(&inode_2delete_mutex);

    if (inode_2delete_list) {

	i2d=inode_2delete_list;
	inode_2delete_list=i2d->next;

    } else {

	inode_2delete_threadid=0;
	pthread_mutex_unlock(&inode_2delete_mutex);
	return;

    }

    pthread_mutex_unlock(&inode_2delete_mutex);

    hash=i2d->ino % SIZE_INODE_HASHTABLE;

    pthread_mutex_lock(&inode_table_mutex);
    inode=inode_hash_table[hash];

    while (inode) {

	if (inode->st.st_ino==i2d->ino) {
	    struct entry_s *entry=inode->alias;
	    struct entry_s *parent=(entry) ? entry->parent : NULL;

	    if (i2d->flags & FORGET_INODE_FLAG_FORGET) {

		if (inode->nlookup<=i2d->forget) {

		    inode->nlookup=0;

		} else {

		    inode->nlookup-=i2d->forget;

		}

	    }

	    if (parent) {
		struct simple_lock_s wlock;
		struct directory_s *directory=get_directory(parent->inode);

		/* remove entry from directory */

		if (wlock_directory(directory, &wlock)==0) {
		    unsigned int error=0;

		    remove_entry_batch(directory, entry, &error);
		    unlock_directory(directory, &wlock);

		}

		entry->parent=NULL;

	    }

	    if ((i2d->flags & FORGET_INODE_FLAG_DELETED) && (inode->flags & INODE_FLAG_DELETED)==0) {
		struct simple_lock_s rlock;
		struct service_context_s *context=NULL;

		/* inform VFS, only when the VFS is not the initiator  */

		logoutput("inode_2delete_thread: remote deleted ino %lli name %s", i2d->ino, (entry) ? entry->name.name : "-UNKNOWN-");

		init_rlock_service_context_hash(&rlock);
		lock_service_context_hash(&rlock);
		context=search_service_context(i2d->unique);

		if (context) {
		    struct service_context_s *root=get_root_context(context);

		    if (parent) {

			notify_VFS_delete(root->interface.ptr, parent->inode->st.st_ino, inode->st.st_ino, entry->name.name, entry->name.len);

		    } else if (entry) {

			notify_VFS_delete(root->interface.ptr, 0, inode->st.st_ino, entry->name.name, entry->name.len);

		    } else {

			notify_VFS_delete(root->interface.ptr, 0, inode->st.st_ino, NULL, 0);

		    }

		    unlock_service_context_hash(&rlock);

		}

		inode->flags|=INODE_FLAG_DELETED;

	    }

	    /* only when lookup count becomes zero remove it foregood */

	    if (inode->nlookup==0) {

		logoutput("inode_2delete_thread: forget inode ino %lli name %s", i2d->ino, (entry) ? entry->name.name : "-UNKNOWN-");

		if (inode->flags & INODE_FLAG_HASHED) {
		    struct inode_s *next=inode->id_next;
		    struct inode_s *prev=inode->id_prev;

		    if (next) next->id_prev=prev;
		    if (prev) {

			prev->id_next=next;

		    } else {

			inode_hash_table[hash]=next;

		    }

		    inode->id_next=NULL;
		    inode->id_prev=NULL;
		    inode->flags-=INODE_FLAG_HASHED;

		}

		/* call the inode specific forget which will also release the attached data */

		(* inode->fs->forget)(inode);

		entry->inode=NULL;
		inode->alias=NULL;
		destroy_entry(entry);
		free_inode(inode);

	    }

	    break;

	}

	inode=inode->id_next;

    }

    pthread_mutex_unlock(&inode_table_mutex);
    free(i2d);
    i2d=NULL;

    goto geti2d;

}

void queue_inode_2forget(ino_t ino, dev_t dev, unsigned int flags, uint64_t forget)
{
    struct inode_2delete_s *i2d=NULL;

    i2d=malloc(sizeof(struct inode_2delete_s));

    if (i2d) {

	i2d->ino=ino;
	i2d->unique=dev;
	i2d->flags=flags;
	i2d->forget=forget;

	pthread_mutex_lock(&inode_2delete_mutex);
	i2d->next=inode_2delete_list;
	inode_2delete_list=i2d;

	if (inode_2delete_threadid==0) {
	    unsigned int error=0;

	    inode_2delete_threadid=1;
	    work_workerthread(NULL, 0, inode_2delete_thread, NULL, &error);

	}

	pthread_mutex_unlock(&inode_2delete_mutex);

    }

}

void log_inode_information(struct inode_s *inode, uint64_t what)
{
    if (what & INODE_INFORMATION_OWNER) logoutput("log_inode_information: owner :%i", inode->st.st_uid);
    if (what & INODE_INFORMATION_GROUP) logoutput("log_inode_information: owner :%i", inode->st.st_gid);
    if (what & INODE_INFORMATION_NAME) {
	struct entry_s *entry=inode->alias;

	if (entry) {

	    logoutput("log_inode_information: entry name :%.*s", entry->name.len, entry->name.name);

	} else {

	    logoutput("log_inode_information: no entry");

	}

    }
    if (what & INODE_INFORMATION_NLOOKUP) logoutput("log_inode_information: nlookup :%li", inode->nlookup);
    if (what & INODE_INFORMATION_MODE) logoutput("log_inode_information: mode :%i", inode->st.st_mode);
    if (what & INODE_INFORMATION_NLINK) logoutput("log_inode_information: nlink :%i", inode->st.st_nlink);
    if (what & INODE_INFORMATION_SIZE) logoutput("log_inode_information: size :%i", inode->st.st_size);
    if (what & INODE_INFORMATION_MTIM) logoutput("log_inode_information: mtim %li.%li", inode->st.st_mtim.tv_sec, inode->st.st_mtim.tv_nsec);
    if (what & INODE_INFORMATION_CTIM) logoutput("log_inode_information: ctim %li.%li", inode->st.st_ctim.tv_sec, inode->st.st_ctim.tv_nsec);
    if (what & INODE_INFORMATION_ATIM) logoutput("log_inode_information: atim %li.%li", inode->st.st_atim.tv_sec, inode->st.st_atim.tv_nsec);
    if (what & INODE_INFORMATION_STIM) logoutput("log_inode_information: stim %li.%li", inode->stim.tv_sec, inode->stim.tv_nsec);

}
