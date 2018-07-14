/*
  2010, 2011, 2012, 2013, 2014 Stef Bon <stefbon@gmail.com>

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
#include <sys/param.h>
#include <pthread.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "skiplist.h"
#include "logging.h"

static struct vector_dirnode_struct zero_vector;

void init_vector(struct vector_dirnode_struct *vector)
{
    vector->maxlevel=0;
    vector->minlevel=0;
    vector->lockset=0;
    vector->lane=NULL;
}

int add_vector_lanes(struct vector_dirnode_struct *vector, unsigned short level, unsigned int *error)
{
    struct vector_lane_struct *lane=NULL;
    int result=0;

    lane=malloc((level + 1) * sizeof(struct vector_lane_struct));

    if (lane) {
	unsigned short i=0;

	memset(lane, 0, (level + 1) * sizeof(struct vector_lane_struct));

	while (i<=level) {

	    lane[i].dirnode=NULL;
	    lane[i].lockset=0;
	    lane[i].step=0;

	    i++;

	}

	vector->lane=lane;

    } else {

	*error=ENOMEM;
	result=-1;

    }

    return result;

}

void destroy_vector_lanes(struct vector_dirnode_struct *vector)
{
    free(vector->lane);
    vector->lane=NULL;
}

void unlock_dirnode_vector(struct vector_dirnode_struct *vector)
{
    unsigned short ctr=vector->minlevel;
    struct dirnode_struct *dirnode=NULL;

    while(ctr<=vector->maxlevel) {

	dirnode=vector->lane[ctr].dirnode;

	if (dirnode) dirnode->junction[ctr].lock&= ~vector->lane[ctr].lockset;

	vector->lane[ctr].lockset=0;
	ctr++;

    }

}

int init_skiplist(struct skiplist_struct *sl, unsigned char prob, 
		    void *(* next)(void *data), void *(*prev)(void *data),
		    int (* compare) (void *a, void *b),
		    void (* insert_before) (void *data, void *before, struct skiplist_struct *sl),
		    void (* insert_after) (void *data, void *after, struct skiplist_struct *sl),
		    void (* delete) (void *data, struct skiplist_struct *sl),
		    void *(* create_rlock)(struct skiplist_struct *sl),
		    void *(* create_wlock)(struct skiplist_struct *sl),
		    int (* lock) (struct skiplist_struct *sl, void *ptr),
		    int (* unlock) (struct skiplist_struct *sl, void *ptr),
		    int (* upgradelock) (struct skiplist_struct *sl, void *ptr),
		    int (* prelock) (struct skiplist_struct *sl, void *ptr),
		    unsigned int (* count) (struct skiplist_struct *sl),
		    void *(* first) (struct skiplist_struct *sl),
		    void *(* last) (struct skiplist_struct *sl),
		    unsigned int *error)
{

    *error=0;

    if ( ! sl) {

	*error=EINVAL;

    } else if (prob<=0) {

	*error=EINVAL;

    } else if (! next || ! prev || ! compare || ! insert_before || ! insert_after || ! delete ||
		! create_rlock || ! create_wlock || ! lock || ! unlock || ! upgradelock || ! prelock ||
		! count || ! first || ! last) {

	*error=EINVAL;

    }

    if (*error==0) {

	sl->ops.next=next;
	sl->ops.prev=prev;
	sl->ops.compare=compare;
	sl->ops.insert_before=insert_before;
	sl->ops.insert_after=insert_after;
	sl->ops.delete=delete;
	sl->ops.create_rlock=create_rlock;
	sl->ops.create_wlock=create_wlock;
	sl->ops.lock=lock;
	sl->ops.unlock=unlock;
	sl->ops.upgradelock=upgradelock;
	sl->ops.prelock=prelock;
	sl->ops.count=count;
	sl->ops.first=first;
	sl->ops.last=last;

	sl->prob=prob;
	sl->dirnode=NULL;

	pthread_mutex_init(&sl->mutex, NULL);
	pthread_cond_init(&sl->cond, NULL);

	return 0;

    }

    return -1;

}


struct skiplist_struct *create_skiplist(unsigned int *error)
{
    struct skiplist_struct *sl=NULL;

    sl=malloc(sizeof(struct skiplist_struct));

    if (! sl) {

	*error=ENOMEM;

    } else {

	memset(sl, 0, sizeof(struct skiplist_struct));

	sl->prob=4; /* a non zero default value */
	sl->dirnode=NULL;

    }

    return sl;

}

void clear_skiplist(struct skiplist_struct *sl)
{

    /* remove the dirnodes */

    if (sl->dirnode) {
	struct dirnode_struct *dirnode=sl->dirnode, *next=NULL;

	/* walk every dirnode: take level 0 */

	dirnode=(dirnode->junction) ? dirnode->junction[0].next : NULL;

	while(dirnode && dirnode != sl->dirnode) {

	    next=(dirnode->junction) ? dirnode->junction[0].next : NULL;

	    destroy_dirnode(dirnode);

	    dirnode=next;

	}

	destroy_dirnode(sl->dirnode);
	sl->dirnode=NULL;

    }

}

void destroy_lock_skiplist(struct skiplist_struct *sl)
{
    pthread_mutex_destroy(&sl->mutex);
    pthread_cond_destroy(&sl->cond);
}

void destroy_skiplist(struct skiplist_struct *sl)
{
    if (sl->dirnode) clear_skiplist(sl);
    destroy_lock_skiplist(sl);
    free(sl);
}

struct dirnode_struct *create_dirnode(unsigned short level)
{
    struct dirnode_struct *dirnode=NULL;

    dirnode=malloc(sizeof(struct dirnode_struct));

    if (dirnode) {
	struct dirnode_junction_struct *junction=NULL;

	junction=malloc((level + 1) * sizeof(struct dirnode_junction_struct));

	if (junction) {
	    unsigned short i;

	    dirnode->data=NULL;
	    dirnode->type=0;
	    dirnode->lock=0;
	    dirnode->level=level;
	    dirnode->junction=junction;

	    memset(junction, 0, (level + 1) * sizeof(struct dirnode_junction_struct));

	    for (i=0;i<=level;i++) {

		junction[i].next=NULL;
		junction[i].prev=NULL;
		junction[i].count=0;
		junction[i].lock=0;

	    }

	} else {

	    free(dirnode);
	    dirnode=NULL;

	}

    }

    return dirnode;

}

struct dirnode_struct *create_head_dirnode(unsigned short level)
{
    struct dirnode_struct *dirnode=NULL;

    dirnode=malloc(sizeof(struct dirnode_struct));

    if (dirnode) {
	struct dirnode_junction_struct *junction=NULL;
	unsigned *dm_count=NULL;

	junction=malloc((level + 1) * sizeof(struct dirnode_junction_struct));
	dm_count=malloc((level + 1) * sizeof(unsigned));

	if (junction && dm_count) {
	    unsigned short i;

	    dirnode->data=(void *) dm_count;
	    dirnode->type=0;
	    dirnode->lock=0;
	    dirnode->level=level;
	    dirnode->junction=junction;

	    memset(junction, 0, (level + 1) * sizeof(struct dirnode_junction_struct));

	    for (i=0;i<=level;i++) {

		junction[i].next=NULL;
		junction[i].prev=NULL;
		junction[i].count=0;
		junction[i].lock=0;

		dm_count[i]=0;

	    }

	} else {

	    if (junction) free(junction);
	    if (dm_count) free(dm_count);

	    free(dirnode);
	    dirnode=NULL;

	}

    }

    return dirnode;

}



void destroy_dirnode(struct dirnode_struct *dirnode)
{

    if (dirnode->junction) {

	free(dirnode->junction);
	dirnode->junction=NULL;

    }

    if (dirnode->type & _DIRNODE_TYPE_START) {
	unsigned *dn_count=(unsigned *) dirnode->data;

	free(dn_count);
	dirnode->data=NULL;

    }

    free(dirnode);

}

unsigned short resize_head_dirnode(struct dirnode_struct *dirnode, unsigned short level)
{

    if (dirnode->level < level) {
	struct dirnode_junction_struct *junction=dirnode->junction;
	unsigned *dn_count = dirnode->data;

	/* level increased */

	junction = (struct dirnode_junction_struct *) realloc((void *) junction, (level + 1) * sizeof(struct dirnode_junction_struct));
	dn_count = (unsigned *) realloc((void *) dn_count, (level + 1) * sizeof(unsigned));

	if (junction && dn_count) {
	    unsigned short i;

	    for (i=dirnode->level+1;i<=level;i++) {

		junction[i].next=NULL;
		junction[i].prev=NULL;
		junction[i].count=0;
		junction[i].lock=0;

		dn_count[i]=0;

	    }

	    dirnode->junction=junction;
	    dirnode->data=(void *) dn_count;

	    dirnode->level=level;

	} else {

	    /* realloc failed */

	    level=dirnode->level;

	}

    } else if (dirnode->level > level) {
	struct dirnode_junction_struct *junction=dirnode->junction;
	unsigned *dn_count = dirnode->data;

	/* level decreased */

	junction=(struct dirnode_junction_struct *) realloc((void *) junction, (level + 1) * sizeof(struct dirnode_junction_struct));
	dn_count = (unsigned *) realloc((void *) dn_count, (level + 1) * sizeof(unsigned));

	if (junction) {

	    dirnode->junction=junction;
	    dirnode->data=(void *) dn_count;
	    dirnode->level=level;

	} else {

	    /* realloc failed */

	    level=dirnode->level;

	}

    }

    return level;

}

