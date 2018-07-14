/*
  2010, 2011, 2012, 2013 Stef Bon <stefbon@gmail.com>

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

#include <math.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "skiplist.h"
#include "skiplist-insert.h"
#include "logging.h"

/*

    determine the level of a new dirnode when an entry is added

    this function tests the number of dirnodes is sufficient when one entry is added

    return:

    level=-1: no dirnode required
    level>=0: a dirnode required with level, 
    it's possible this is +1 the current level, in that case an extra lane is required

*/

static int do_levelup(struct skiplist_struct *sl)
{
    int level=-1;
    unsigned hlp=0;

    pthread_mutex_lock(&sl->mutex);

    hlp=sl->ops.count(sl) + 1; /* plus one */

    if ( hlp > sl->prob ) {

	/* enough to get a dirnode in the first place */

	if (sl->dirnode) {
	    struct dirnode_struct *dirnode=sl->dirnode;
	    unsigned *dn_count=(unsigned *) dirnode->data;
	    unsigned ctr=0;

	    while(ctr<=dirnode->level) {

		hlp=hlp / sl->prob;

		/*
		    in ideal case is the number of dirnodes per lane

		    hlp == dn_count[ctr] + 1

		    plus one cause the head dirnode

		    in any case it's required to add a dirnode in this lane when

		    hlp > dn_count[ctr] + 1

		*/

		if (hlp < dn_count[ctr] + 1) {

		    break;

		} else {

		    level=ctr;

		}

		ctr++;

	    }

	    if (level==dirnode->level) {

		/* test an extra lane is required */

		if ( hlp > sl->prob ) level++;

	    }

	} else {

	    level=0;

	}

    }

    unlock:

    pthread_mutex_unlock(&sl->mutex);

    return level;

}

static unsigned int update_counters(struct vector_dirnode_struct *vector, unsigned short ctr_left)
{

    if (vector->lane) {
	unsigned short level=vector->minlevel;
	struct dirnode_struct *dirnode=NULL;

	while(level<=vector->maxlevel) {

	    dirnode=vector->lane[level].dirnode;

	    dirnode->junction[level].count++;
	    ctr_left+=vector->lane[level].step;

	    /* correct the lock if this has been set */

	    dirnode->junction[level].lock &= ~vector->lane[level].lockset;

	    vector->lane[level].lockset=0;
	    vector->lane[level].dirnode=NULL;
	    vector->lane[level].step=0;

	    level++;

	}

    }

    return ctr_left;

}

static unsigned int add_dirnode_sl(struct skiplist_struct *sl, struct vector_dirnode_struct *vector, unsigned int ctr_left, void *data, int newlevel)
{

    if ( ! sl->dirnode) {
	struct dirnode_struct *head_dirnode=NULL;

	/* add a new directory node and a in between node */

	head_dirnode=create_head_dirnode(newlevel);

	if (head_dirnode) {
	    struct dirnode_struct *new_dirnode=NULL;
	    unsigned *dn_count=(unsigned *) head_dirnode->data;
	    unsigned int count=sl->ops.count(sl);

	    sl->dirnode=head_dirnode;
	    head_dirnode->type=_DIRNODE_TYPE_START;

	    new_dirnode=create_dirnode(newlevel);

	    if (new_dirnode) {
		unsigned short level=0;
		struct dirnode_junction_struct *dirnode_junction=new_dirnode->junction;

		new_dirnode->data=data;
		new_dirnode->type=_DIRNODE_TYPE_BETWEEN;

		while (level<=newlevel) {

		    /* insert in the new fast lane */

		    new_dirnode->junction[level].next=head_dirnode;
		    new_dirnode->junction[level].prev=head_dirnode;

		    head_dirnode->junction[level].next=new_dirnode;
		    head_dirnode->junction[level].prev=new_dirnode;

		    /* increase dirnode count */

		    dn_count[level]++;

		    /* assign counters */

		    new_dirnode->junction[level].count=count - ctr_left + 1;
		    head_dirnode->junction[level].count=ctr_left;

		    level++;

		}

	    }

	}

    } else {
	struct dirnode_struct *dirnode=sl->dirnode;
	unsigned short currentlevel=dirnode->level;
	unsigned *dn_count=(unsigned *) dirnode->data;
	unsigned int count=sl->ops.count(sl);

	if (newlevel>currentlevel) {
	    struct dirnode_struct *new_dirnode=NULL;

	    newlevel=resize_head_dirnode(dirnode, newlevel);
	    new_dirnode=create_dirnode(newlevel);

	    if (new_dirnode) {
	    	unsigned short level=0;
	    	struct dirnode_struct *next_dirnode=NULL;

		new_dirnode->data=data;
		new_dirnode->type=_DIRNODE_TYPE_BETWEEN;

		/*
		    the existing lanes where a dirnode is inserted
		*/

		while (level<=currentlevel) {

		    dirnode=vector->lane[level].dirnode;
		    next_dirnode=dirnode->junction[level].next;

		    /* insert between dirnode and next */

		    new_dirnode->junction[level].prev=dirnode;
		    new_dirnode->junction[level].next=next_dirnode;

		    dirnode->junction[level].next=new_dirnode;
		    next_dirnode->junction[level].prev=new_dirnode;

		    /* increase dirnode count */

		    dn_count[level]++;

		    /* change the counters */

		    if (dirnode->junction[level].count>0) {

			new_dirnode->junction[level].count=dirnode->junction[level].count - ctr_left + 1;

		    } else {

			/* count is zero: first time here */

			new_dirnode->junction[level].count=count - ctr_left + 1;

		    }

		    dirnode->junction[level].count=ctr_left;

		    /* update the counter left with the steps taken */

		    ctr_left+=vector->lane[level].step;

		    /* correct the lock if this has been set */

		    dirnode->junction[level].lock &= ~vector->lane[level].lockset;

		    vector->lane[level].lockset=0;
		    vector->lane[level].dirnode=NULL;

		    level++;

		}

		dirnode=sl->dirnode;

		while (level<=newlevel) {

		    /* insert in the new fast lane */

		    new_dirnode->junction[level].next=dirnode;
		    new_dirnode->junction[level].prev=dirnode;

		    dirnode->junction[level].next=new_dirnode;
		    dirnode->junction[level].prev=new_dirnode;

		    /* increase dirnode count */

		    dn_count[level]++;

		    /* assign counters */

		    new_dirnode->junction[level].count=count - ctr_left + 1;
		    dirnode->junction[level].count=ctr_left;

		    level++;

		}

	    }

	} else {
	    struct dirnode_struct *new_dirnode=NULL;

	    /* newlevel <= currentlevel */

	    new_dirnode=create_dirnode(newlevel);

	    if (new_dirnode) {
	    	unsigned short level=0;
	    	struct dirnode_struct *next_dirnode=NULL;

		new_dirnode->data=data;
		new_dirnode->type=_DIRNODE_TYPE_BETWEEN;

		/*
		    the existing lanes where a dirnode is inserted
		*/

		while (level<=newlevel) {

		    dirnode=vector->lane[level].dirnode;
		    next_dirnode=dirnode->junction[level].next;

		    /* insert between dirnode and next */

		    new_dirnode->junction[level].prev=dirnode;
		    new_dirnode->junction[level].next=next_dirnode;

		    dirnode->junction[level].next=new_dirnode;
		    next_dirnode->junction[level].prev=new_dirnode;

		    /* increase dirnode count */

		    dn_count[level]++;

		    /* change the counters */

		    if (dirnode->junction[level].count>0) {

			new_dirnode->junction[level].count=dirnode->junction[level].count - ctr_left + 1;

		    } else {

			/* count is zero: first time here */

			new_dirnode->junction[level].count=count - ctr_left + 1;

		    }

		    dirnode->junction[level].count=ctr_left;

		    /* update the counter left with the steps taken */

		    ctr_left+=vector->lane[level].step;

		    /* correct the lock if this has been set */

		    dirnode->junction[level].lock &= ~vector->lane[level].lockset;

		    vector->lane[level].lockset=0;
		    vector->lane[level].dirnode=NULL;

		    level++;

		}

		/*
		    remaining lanes
		*/

		while (level <= currentlevel) {

		    dirnode=vector->lane[level].dirnode;
		    dirnode->junction[level].count++;

		    ctr_left+=vector->lane[level].step;

		    dirnode->junction[level].lock &= ~vector->lane[level].lockset;

		    vector->lane[level].lockset=0;
		    vector->lane[level].dirnode=NULL;

		    level++;

		}

	    }

	}

    }

    return ctr_left;

}

static void *insert_nonempty_sl(struct skiplist_struct *sl, void *lookupdata, unsigned int *row, unsigned int *error, void *data, unsigned int flags, int newlevel)
{
    void *search=NULL, *found=NULL;
    int diff=0;
    int level;
    struct vector_dirnode_struct vector;
    unsigned int search_row=0;
    unsigned int ctr=0;
    void *ptr=NULL;

    /* what kind of lock (read or write) this is is determined by the caller */

    ptr=sl->ops.create_rlock(sl);
    init_vector(&vector);
    *error=ENOENT;

    if (sl->ops.count(sl)==0) {

	/* when empty just add it and leave
	    make sure it's a write lock */

	if (sl->ops.upgradelock(sl, ptr)==0) {

	    sl->ops.insert_after(data, NULL, sl);

	    if (row) *row=1;
	    found=data;
	    *error=0;

	    goto unlock;

	} else {

	    *error=EAGAIN;
	    goto unlock;

	}

    }

    if (sl->dirnode==NULL || newlevel>sl->dirnode->level) {

	/* it's required to add a new lane, make sure the lock is already pre write to block any readers */

	if (sl->ops.prelock(sl, ptr)==-1) {

	    logoutput_warning("insert_nonempty_sl: newlevel %i prepare lock exclusive failed", newlevel);

	    *error=EAGAIN;
	    goto unlock;

	} else {

	    logoutput_debug("insert_nonempty_sl: newlevel %i prepare lock exclusive success", newlevel);

	}

    }

    search=sl->ops.first(sl);
    search_row=1;
    ctr=1;

    /*
	are there fast lanes ??
	- if there are use these of course
    */

    if (sl->dirnode) {
	struct dirnode_struct *dirnode, *next_dirnode;
	void *next=NULL;

	/* head of the skiplist */

	dirnode=sl->dirnode;
	level=dirnode->level;

	if (add_vector_lanes(&vector, level, error)==-1) goto unlock;

	vector.maxlevel=level;
	vector.lane[level].dirnode=dirnode;

	while(level>=0) {

	    pthread_mutex_lock(&sl->mutex);

	    dirnode=vector.lane[level].dirnode;
	    next_dirnode=dirnode->junction[level].next;

	    diff=1;

	    if (next_dirnode->type & _DIRNODE_TYPE_BETWEEN) {

		next=next_dirnode->data;
		diff=sl->ops.compare(next, lookupdata);

	    }

	    if (diff>0) {

		/*
		    next dirnode is too far
		    here try to lock the region in this lane
		*/

		if (next_dirnode->lock & _DIRNODE_RMLOCK) {

		    /* this dirnode is about to be removed: break */

		    *error=EAGAIN;
		    vector.minlevel=level+1;
		    unlock_dirnode_vector(&vector);
		    pthread_cond_broadcast(&sl->cond);
		    pthread_mutex_unlock(&sl->mutex);
		    goto unlock;

		} else if (dirnode->junction[level].lock & _DIRNODE_WRITELOCK) {

		    /* this region in this fast lane is already locked */

		    *error=EAGAIN;
		    vector.minlevel=level+1;
		    unlock_dirnode_vector(&vector);
		    pthread_cond_broadcast(&sl->cond);
		    pthread_mutex_unlock(&sl->mutex);
		    goto unlock;

		} else {

		    dirnode->junction[level].lock|=_DIRNODE_WRITELOCK;
		    vector.lane[level].lockset=_DIRNODE_WRITELOCK;
		    vector.lockset|=_DIRNODE_WRITELOCK;

		    if (dirnode->junction[level].lock>>3 > 0) {

			/* there are readers active here: wait for them to finish */

			while(dirnode->junction[level].lock>>3 > 0) {

			    pthread_cond_wait(&sl->cond, &sl->mutex);

			}

		    }

		}

		level--;
		if (level>=0) vector.lane[level].dirnode=dirnode;

	    } else if (diff==0) {

		if (next_dirnode->lock&_DIRNODE_RMLOCK) {

		    /* the entry to insert is about to be removed: try again later */

		    *error=EAGAIN;

		} else {

		    *error=EEXIST;
		    search_row+=dirnode->junction[level].count - 1;
		    found=next;

		}

		vector.minlevel=level+1;
		unlock_dirnode_vector(&vector);
		pthread_cond_broadcast(&sl->cond);
		pthread_mutex_unlock(&sl->mutex);

		goto unlock;

	    } else {

		next=sl->ops.next(next);

		/* difference<0: skip, stay on the same fast lane */

		vector.lane[level].step+=dirnode->junction[level].count;
		vector.lane[level].dirnode=next_dirnode;

		if (next) {

		    search=next;
		    search_row+=dirnode->junction[level].count;
		    ctr=1;

		} else {

		    search=next_dirnode->data;
		    search_row+=dirnode->junction[level].count - 1;
		    ctr=0;

		}

	    }

	    pthread_mutex_unlock(&sl->mutex);

	}

    }

    /*
	when here, the algoritm has reached the lowest level, and that is the
	linked list of entries
	check first there is not already an exact match found.....
    */

    if (*error==ENOENT) {

	while (search) {

	    diff=sl->ops.compare(search, lookupdata);

	    if (diff<0) {

		/* before name still */

		search=sl->ops.next(search);
		search_row++;
		ctr++;
		continue;

	    } else if (diff==0) {

		/* exact match */

		*error=EEXIST;
		found=search;
		break;

	    } else {

		/* next entry is bigger: insert here */

		break;

	    }

	}

	if (*error==ENOENT) {

	    sl->ops.lock(sl, ptr);

	    found=data;

	    *error=0; /* success */

	    if (search) {

		sl->ops.insert_before(data, search, sl);

		/* only create a dirnode when in between */

		if (newlevel>=0) {
		    struct dirnode_struct *dirnode=(vector.lane) ? vector.lane[0].dirnode : sl->dirnode;
		    unsigned int count=(dirnode) ? dirnode->junction[0].count : sl->ops.count(sl);

		    if (ctr > 1 && ctr < count - 1) {

			/* only add dirnodes when not too close to another */

			ctr=add_dirnode_sl(sl, &vector, ctr, data, newlevel);

		    } else {

			ctr=update_counters(&vector, ctr);

		    }

		} else {

		    ctr=update_counters(&vector, ctr);

		}

	    } else {
		unsigned int count=0;

		/*
		    special case:

		    a. no search entry can mean the search ended at the last entry
		    b. or no entry at all: directory is empty

		*/

		sl->ops.insert_after(data, NULL, sl);
		count=sl->ops.count(sl);

		if (count==1) {

		    search_row=1;

		} else {

		    ctr=update_counters(&vector, ctr);
		    search_row=count;

		}

	    }

	}

    }

    unlock:

    sl->ops.unlock(sl, ptr);

    if (vector.lockset>0) {

	pthread_mutex_lock(&sl->mutex);
	unlock_dirnode_vector(&vector);
	pthread_cond_broadcast(&sl->cond);
	pthread_mutex_unlock(&sl->mutex);

    }

    destroy_vector_lanes(&vector);
    if (row) *row=search_row;

    return found;

}

void *insert_sl(struct skiplist_struct *sl, void *lookupdata, unsigned int *row, unsigned int *error, void *data, unsigned short flags)
{
    int newlevel=-1;
    unsigned int counter=0;
    void *found=NULL;

    if (row) *row=0;

    // logoutput("insert_entry_sl");

    if (! (flags & _SL_INSERT_FLAG_NOLANE)) {

	/*
	    determine the level of the fast lane in which this entry will be present 
	    (very important to set it once outside the while loop)
	*/

	newlevel=do_levelup(sl);

    }

    while(counter<20) {

	*error=ENOENT;

	found=insert_nonempty_sl(sl, lookupdata, row, error, data, flags, newlevel);

	if (*error==0 || *error==EEXIST) {

	    break;

	} else if (*error==EAGAIN) {

	    logoutput_warning("insert_entry_sl: operation blocked, try again");

	} else {

	    logoutput_error("insert_entry_sl: error %i", *error);
	    break;

	}

	counter++;

    }

    return found;

}

static void *insert_nonempty_sl_batch(struct skiplist_struct *sl, void *lookupdata, unsigned int *row, unsigned int *error, void *data, unsigned int flags, int newlevel)
{
    void *search=NULL, *found=NULL;
    int diff=0;
    int level;
    struct vector_dirnode_struct vector;
    unsigned char exclusive=0;
    unsigned int search_row=0;
    unsigned int ctr=0;

    init_vector(&vector);
    *error=ENOENT;

    if (sl->ops.count(sl)==0) {

	/* when empty just add it and leave */

	sl->ops.insert_after(data, NULL, sl);

	if (row) *row=1;
	found=data;
	*error=0;

	return found;

    }

    search=sl->ops.first(sl);
    search_row=1;
    ctr=1;

    /*
	are there fast lanes ??
	- if there are use these of course
    */

    if (sl->dirnode) {
	struct dirnode_struct *dirnode, *next_dirnode;
	void *next=NULL;

	/* head of the skiplist */

	dirnode=sl->dirnode;
	level=dirnode->level;

	if (add_vector_lanes(&vector, level, error)==-1) goto out;

	vector.maxlevel=level;
	vector.lane[level].dirnode=dirnode;

	while(level>=0) {

	    dirnode=vector.lane[level].dirnode;
	    next_dirnode=dirnode->junction[level].next;

	    diff=1;

	    if (next_dirnode->type & _DIRNODE_TYPE_BETWEEN) {

		next=next_dirnode->data;
		diff=sl->ops.compare(next, lookupdata);

	    }

	    if (diff>0) {

		/*
		    next dirnode is too far
		*/

		level--;
		if (level>=0) vector.lane[level].dirnode=dirnode;

	    } else if (diff==0) {


		/*
		    exact match: exist
		*/

		*error=EEXIST;
		search_row+=dirnode->junction[level].count - 1;
		found=next;

		vector.minlevel=level+1;
		goto out;

	    } else {

		next=sl->ops.next(next);

		/* difference<0: skip, stay on the same fast lane */

		vector.lane[level].step+=dirnode->junction[level].count;
		vector.lane[level].dirnode=next_dirnode;

		if (next) {

		    search=next;
		    search_row+=dirnode->junction[level].count;
		    ctr=1;

		} else {

		    search=next_dirnode->data;
		    search_row+=dirnode->junction[level].count - 1;
		    ctr=0;

		}

	    }

	}

    }

    /*
	when here, the algoritm has reached the lowest level, and that is the
	linked list of entries
	check first there is not already an exact match found..
	what counts is that entry->name <= name
    */

    if (*error==ENOENT) {

	while (search) {

	    diff=sl->ops.compare(search, lookupdata);

	    if (diff<0) {

		/* before name still */

		search=sl->ops.next(search);
		search_row++;
		ctr++;
		continue;

	    } else if (diff==0) {

		/* exact match */

		*error=EEXIST;
		found=search;
		break;

	    } else {

		/* next entry is bigger: insert here */

		break;

	    }

	}

	if (*error==ENOENT) {

	    found=data;
	    *error=0; /* success */

	    if (search) {

		sl->ops.insert_before(data, search, sl);

		if (newlevel>=0) {
		    struct dirnode_struct *dirnode=(vector.lane) ? vector.lane[0].dirnode : sl->dirnode;
		    unsigned int count=(dirnode) ? dirnode->junction[0].count : sl->ops.count(sl);

		    if (ctr > 1 && ctr < count - 1) {

			/* only add dirnodes when not too close to another */

			ctr=add_dirnode_sl(sl, &vector, ctr, data, newlevel);

		    } else {

			ctr=update_counters(&vector, ctr);

		    }

		} else {

		    ctr=update_counters(&vector, ctr);

		}

	    } else {
		unsigned int count=0;

		sl->ops.insert_after(data, NULL, sl);
		count=sl->ops.count(sl);

		if (count==0) {

		    search_row=1;

		} else {

		    ctr=update_counters(&vector, ctr);

		    search_row=count;

		}

	    }

	}

    }

    out:

    if (row) *row=search_row;

    destroy_vector_lanes(&vector);

    return found;

}


void *insert_sl_batch(struct skiplist_struct *sl, void *lookupdata, unsigned int *row, unsigned int *error, void *data, unsigned short flags)
{
    int newlevel=-1;

    if (row) *row=0;

    if (! (flags & _SL_INSERT_FLAG_NOLANE)) {

	/*
	    determine the level of the fast lane in which this entry will be present 
	    (very important to set it once outside the while loop)
	*/

	newlevel=do_levelup(sl);

    }

    *error=0;

    return insert_nonempty_sl_batch(sl, lookupdata, row, error, data, flags, newlevel);

}

