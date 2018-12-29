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

#ifndef SIZE_INODE_HASHTABLE
#define SIZE_INODE_HASHTABLE				10240
#endif

#include "logging.h"

static struct inode_s **inode_hash_table=NULL;
static pthread_mutex_t inode_table_mutex=PTHREAD_MUTEX_INITIALIZER;

static uint64_t inoctr=FUSE_ROOT_ID;
static pthread_mutex_t inodectrmutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned long long nrinodes=0;

static struct inode_s *inode_tobedeleted=NULL;
static pthread_mutex_t inode_tobedeleted_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t inode_tobedeleted_cond=PTHREAD_COND_INITIALIZER;
static pthread_t inode_remove_thread=0;

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
    size_t hash=inode->st.st_ino % SIZE_INODE_HASHTABLE;
    struct inode_s *keep=inode;
    struct inode_s *next=inode->id_next;
    struct inode_s *prev=inode->id_prev;

    inode=realloc(inode, sizeof(struct inode_s) + new); /* assume always good */

    if (inode != keep) {

	if (inode==NULL) return NULL;
	inode->cache_size=new;

	/* repair */

	inode->id_next=next;
	if (next) next->id_prev=inode;
	inode->id_prev=prev;
	if (prev) {

	    prev->id_next=inode;

	} else {

	    /* no prev means it's the first */

	    inode_hash_table[hash]=inode;

	}

    }

    return inode;

}

struct inode_s *find_inode(uint64_t ino)
{
    size_t hash=ino % SIZE_INODE_HASHTABLE;
    struct inode_s *inode=NULL;

    // logoutput("find_inode: ino %li", (long) ino);

    pthread_mutex_lock(&inode_table_mutex);

    inode=inode_hash_table[hash];

    while(inode) {

	if (inode->st.st_ino==ino) break;
	inode=inode->id_next;

    }

    pthread_mutex_unlock(&inode_table_mutex);

    // logoutput("find_inode: ino %li %s", (long) ino, (inode) ? "found" : "notfound");

    return inode;

}

static void remove_inodes_thread(void *ptr)
{
    struct context_interface_s *interface=(struct context_interface_s *) ptr;
    struct inode_s *inode=NULL;

    logoutput_info("remove_inodes_thread");

    inode=inode_tobedeleted;

    while(inode) {

	inode_tobedeleted=inode->id_next;

	if (inode->alias && (inode->flags & FORGET_INODE_FLAG_REMOVE_ENTRY)) {
	    struct entry_s *entry=inode->alias;
	    struct entry_s *parent=(entry) ? entry->parent : NULL;

	    logoutput_info("remove_inodes_thread: remove %lli", inode->st.st_ino);

	    if (inode->flags & FORGET_INODE_FLAG_NOTIFY_VFS) {

		if (entry) notify_VFS_delete(interface->ptr, parent->inode->st.st_ino, inode->st.st_ino, entry->name.name, entry->name.len);

	    }

	    // logoutput_info("remove_inodes_thread: A");

	    if (entry)  {

		if (parent) {
		    struct simple_lock_s wlock;
		    struct directory_s *directory=get_directory(parent->inode);

		    // logoutput_info("remove_inodes_thread: B");

		    if (wlock_directory(directory, &wlock)==0) {
			//struct directory_s *directory=get_directory(parent->inode);
			unsigned int error=0;

			// logoutput_info("remove_inodes_thread: C");
			remove_entry(entry, &error); /* remove from skiplist (=directory) */
			// logoutput_info("remove_inodes_thread: D");
			unlock_directory(directory, &wlock);

		    }

		}

		entry->inode=NULL;
		inode->alias=NULL;
		destroy_entry(entry); /* free */

	    }

	}

	free_inode(inode);
	inode=inode_tobedeleted;

    }

    pthread_mutex_lock(&inode_tobedeleted_mutex);
    inode_remove_thread=0;
    pthread_cond_broadcast(&inode_tobedeleted_cond); /* 20181205: not used now */
    pthread_mutex_unlock(&inode_tobedeleted_mutex);

}

static void start_remove_inode_thread(struct context_interface_s *interface)
{
    unsigned int error=0;

    pthread_mutex_lock(&inode_tobedeleted_mutex);

    if (inode_remove_thread==0) {

	work_workerthread(NULL, 0, remove_inodes_thread, (void *)interface, &error);
	inode_remove_thread=1;

    }

    pthread_mutex_unlock(&inode_tobedeleted_mutex);
}

static void cb_dummy(void *data)
{
}

struct inode_s *forget_inode(struct context_interface_s *interface, uint64_t ino, uint64_t nlookup, void (*cb) (void *data), void *data, unsigned int flags)
{
    size_t hash=ino % SIZE_INODE_HASHTABLE;
    struct inode_s *inode=NULL;

    if (cb==NULL) cb=cb_dummy;
    if (ino==0 || interface==NULL) return NULL;

    pthread_mutex_lock(&inode_table_mutex);

    inode=inode_hash_table[hash];

    while(inode) {

	if (inode->st.st_ino==ino) {
	    struct inode_s *prev=NULL;
	    struct inode_s *next=NULL;

	    if (inode->nlookup < nlookup && nlookup>0) {

		inode->nlookup -= nlookup;
		inode=NULL;
		break;

	    }

	    inode->nlookup=0;
	    prev=inode->id_prev;
	    next=inode->id_next;

	    if (prev) {

		prev->id_next=next;

	    } else {

		/* no prev: it must be the first */

		inode_hash_table[hash]=next;

	    }

	    if (next) next->id_prev=prev;
	    inode->id_next=NULL;
	    inode->id_prev=NULL;
	    (* cb) (data);

	    /* move to to be deleted list */

	    inode->id_next=inode_tobedeleted;
	    inode->id_prev=NULL;
	    inode_tobedeleted=inode;
	    inode->flags |= FORGET_INODE_FLAG_DELETED;
	    if (flags & FORGET_INODE_FLAG_REMOVE_ENTRY) inode->flags |= FORGET_INODE_FLAG_REMOVE_ENTRY;

	    break;

	}

	inode=inode->id_next;

    }

    pthread_mutex_unlock(&inode_table_mutex);

    if (inode && (flags & FORGET_INODE_FLAG_QUEUE)) {

	start_remove_inode_thread(interface);
	inode=NULL;

    }

    return inode;

}

void remove_inode(struct context_interface_s *interface, struct inode_s *inode)
{
    size_t hash=inode->st.st_ino % SIZE_INODE_HASHTABLE;
    struct inode_s *prev=NULL;
    struct inode_s *next=NULL;

    if (inode==NULL || interface==NULL) return;

    pthread_mutex_lock(&inode_table_mutex);

    prev=inode->id_prev;
    next=inode->id_next;

    if (prev) {

	prev->id_next=next;

    } else {

	/* no prev: it must be the first */

	inode_hash_table[hash]=next;

    }

    if (next) next->id_prev=prev;
    inode->id_next=NULL;
    inode->id_prev=NULL;

    inode->id_next=inode_tobedeleted;
    inode->id_prev=NULL;
    inode_tobedeleted=inode;
    inode->flags |= FORGET_INODE_FLAG_DELETED;
    inode->flags |= FORGET_INODE_FLAG_REMOVE_ENTRY;
    inode->flags |= FORGET_INODE_FLAG_NOTIFY_VFS;

    pthread_mutex_unlock(&inode_table_mutex);

    if (inode) start_remove_inode_thread(interface);

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
