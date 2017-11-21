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

#include "entry-management.h"

#ifndef SIZE_INODE_HASHTABLE
#define SIZE_INODE_HASHTABLE				10240
#endif

#include "logging.h"

extern const char *dotname;

static struct inode_s **inode_hash_table=NULL;
static pthread_mutex_t inode_table_mutex=PTHREAD_MUTEX_INITIALIZER;

static uint64_t inoctr=FUSE_ROOT_ID;
static pthread_mutex_t inodectrmutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned long long nrinodes=0;

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
    inode->ino=inoctr;

    hash = inoctr % SIZE_INODE_HASHTABLE;

    inode->id_next = inode_hash_table[hash];
    inode_hash_table[hash] = inode;

    (* cb) (data);

    pthread_mutex_unlock(&inode_table_mutex);

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
	memcpy(name, xname->name, xname->len + 1);

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

    memset(inode, 0, sizeof(struct inode_s));

    inode->nlookup=0;
    inode->ino=0;
    inode->id_next=NULL;

    inode->alias=NULL;

    inode->mode=0;
    inode->nlink=0;
    inode->uid=(uid_t) -1;
    inode->gid=(gid_t) -1;
    inode->size=0;

    inode->mtim.tv_sec=0;
    inode->mtim.tv_nsec=0;
    inode->ctim.tv_sec=0;
    inode->ctim.tv_nsec=0;
    inode->atim.tv_sec=0;
    inode->atim.tv_nsec=0;
    inode->stim.tv_sec=0;
    inode->stim.tv_nsec=0;

    inode->fs=NULL;
    inode->link_type=0;

}

void fill_inode_stat(struct inode_s *inode, struct stat *st)
{
    //inode->mode=st->st_mode;
    //inode->nlink=st->st_nlink;

    inode->uid=st->st_uid;
    inode->gid=st->st_gid;

    //inode->size=st->st_size;

    inode->mtim.tv_sec=st->st_mtim.tv_sec;
    inode->mtim.tv_nsec=st->st_mtim.tv_nsec;

    inode->ctim.tv_sec=st->st_ctim.tv_sec;
    inode->ctim.tv_nsec=st->st_ctim.tv_nsec;

    inode->atim.tv_sec=st->st_atim.tv_sec;
    inode->atim.tv_nsec=st->st_atim.tv_nsec;

}

struct inode_s *create_inode()
{
    struct inode_s *inode=NULL;

    inode = malloc(sizeof(struct inode_s));

    if (inode) init_inode(inode);

    return inode;

}

struct inode_s *find_inode(uint64_t ino)
{
    size_t hash=ino % SIZE_INODE_HASHTABLE;
    struct inode_s *inode=NULL;

    pthread_mutex_lock(&inode_table_mutex);

    inode=inode_hash_table[hash];

    while(inode) {

	if (inode->ino==ino) break;
	inode=inode->id_next;

    }

    pthread_mutex_unlock(&inode_table_mutex);

    return inode;

}

struct inode_s *forget_inode(uint64_t ino, void (*cb) (void *data), void *data)
{
    size_t hash=ino % SIZE_INODE_HASHTABLE;
    struct inode_s *inode=NULL, *prev=NULL;

    pthread_mutex_lock(&inode_table_mutex);

    inode=inode_hash_table[hash];

    while(inode) {

	if (inode->ino==ino) {

	    if (prev) {

		prev->id_next=inode->id_next;

	    } else {

		/* no prev: it's the first */

		inode_hash_table[hash]=inode->id_next;

	    }

	    inode->id_next=NULL;

	    (* cb) (data);

	    break;

	}

	prev=inode;
	inode=inode->id_next;

    }

    pthread_mutex_unlock(&inode_table_mutex);

    return inode;

}

void remove_inode(struct inode_s *inode)
{
    uint64_t ino=inode->ino;
    size_t hash=ino % SIZE_INODE_HASHTABLE;
    struct inode_s *tmp_inode=NULL, *prev=NULL;

    pthread_mutex_lock(&inode_table_mutex);

    tmp_inode=inode_hash_table[hash];

    while(inode) {

	if (tmp_inode==inode) {

	    if (prev) {

		prev->id_next=tmp_inode->id_next;

	    } else {

		/* no prev: it's the first */

		inode_hash_table[hash]=tmp_inode->id_next;

	    }

	    inode->id_next=NULL;
	    break;

	}

	prev=tmp_inode;
	tmp_inode=tmp_inode->id_next;

    }

    pthread_mutex_unlock(&inode_table_mutex);

}
