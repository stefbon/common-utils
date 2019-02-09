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

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "skiplist.h"
#include "skiplist-delete.h"
#define LOGGING
#include "logging.h"

static void unlock_dirnode_vector_deletefailed(struct vector_dirnode_struct *vector, struct dirnode_struct *dirnode_rm)
{

    if (dirnode_rm && (vector->lockset & _DIRNODE_RMLOCK)) {

	dirnode_rm->lock &= ~ _DIRNODE_RMLOCK;

    }

    unlock_dirnode_vector(vector);

}

/*
    function to remove a dirnode from a fast lane
    - dirnode, the dirnode in question
    - level, the number of the fast lane

    correct the next, prev and counters

*/

static void correct_dirnodes_sl(struct dirnode_struct *dirnode, unsigned short level)
{
    struct dirnode_struct *prev=dirnode->junction[level].prev;
    struct dirnode_struct *next=dirnode->junction[level].next;

    prev->junction[level].next=next;
    next->junction[level].prev=prev;

    prev->junction[level].count+=dirnode->junction[level].count - 1;

}

/*

    function to remove the dirnodes which are attached to a specific entry
    and correct the counts in the different fast lanes
    typically called when an entry is removed

*/

static void remove_dirnodes_sl(struct skiplist_struct *sl, struct vector_dirnode_struct *vector, void *data)
{
    struct dirnode_struct *dirnode=NULL;

    // logoutput_warning("remove_dirnodes_sl");

    dirnode=sl->dirnode;

    if (dirnode) {
	unsigned short currentlevel=dirnode->level;
	int level=vector->maxlevel;
	unsigned *dn_count=(unsigned *) dirnode->data;

	while(level>=0) {

	    dirnode=vector->lane[level].dirnode;

	    if (dirnode) {
		struct dirnode_struct *next=dirnode->junction[level].next;

		if (next->data == data) {

		    /* next dirnode points to entry, remove also */

		    while(level>=0) {

			dirnode=vector->lane[level].dirnode;

			if (level==currentlevel) {

			    /* test this has been the only dirnode left in this lane */

			    if (dirnode==sl->dirnode && dirnode==next->junction[level].next) currentlevel--;

			}

			correct_dirnodes_sl(next, level);

			dn_count[level]--;

			next->junction[level].prev=NULL;
			next->junction[level].next=NULL;
			next->junction[level].count=0;
			next->junction[level].lock=0;

			dirnode->junction[level].lock &= ~vector->lane[level].lockset;

			vector->lane[level].lockset=0;
			vector->lane[level].dirnode=NULL;

			level--;

		    }

		    destroy_dirnode(next);

		    /* if lanes have become empty resize */

		    dirnode=sl->dirnode;

		    if (currentlevel<dirnode->level) {

			if (currentlevel<0) {

			    /* no more lanes */

			    destroy_dirnode(dirnode);
			    sl->dirnode=NULL;

			} else {

			    resize_head_dirnode(dirnode, currentlevel);

			}

		    }

		    break;

		} else {

		    /* entry is in between dirnodes in this lane: no dirnode points to entry */

		    dirnode->junction[level].count--;
		    dirnode->junction[level].lock &= ~vector->lane[level].lockset;

		    vector->lane[level].lockset=0;
		    vector->lane[level].dirnode=NULL;

		}

	    }

	    level--;

	}

    }

}

static void delete_nonempty_sl(struct skiplist_struct *sl, void *lookupdata, unsigned int *row, unsigned int *error)
{
    void *search=NULL, *found=NULL;
    struct vector_dirnode_struct vector;
    int diff=0;
    unsigned int search_row=0;
    void *ptr=NULL;
    int level=0;

    // logoutput_warning("delete_nonempty_sl: sl %s", (sl) ? "defined" : "not defined");
    // if (sl) {

	// logoutput_warning("delete_nonempty_sl: create_rlock %s", (sl->ops.create_rlock) ? "defined" : "not defined");

    // } else {

	// logoutput_warning("delete_nonempty_sl: create_rlock sl not defined");

    // }

    ptr=sl->ops.create_rlock(sl);
    if (sl->ops.count(sl)==0) goto unlock;
    init_vector(&vector);
    search=sl->ops.first(sl);
    search_row=1;
    *error=ENOENT;

    // logoutput("delete_nonempty_sl");

    /* when there are fastlanes, take them first */

    if (sl->dirnode) {
	struct dirnode_struct *dirnode=sl->dirnode;
	struct dirnode_struct *next_dirnode=NULL;
	void *next=NULL;

	/* head of the skiplist */

	level=dirnode->level;
	if (add_vector_lanes(&vector, level, error)==-1) goto unlock;

	vector.maxlevel=level;
	vector.lane[level].dirnode=dirnode;

	while (level>=0) {

	    pthread_mutex_lock(&sl->mutex);

	    dirnode=vector.lane[level].dirnode;
	    next_dirnode=dirnode->junction[level].next;

	    diff=1;

	    if (next_dirnode->type & _DIRNODE_TYPE_BETWEEN) {

		next=next_dirnode->data;
		diff=sl->ops.compare(next, lookupdata);

	    }

	    if (diff==0) {

		/* next dirnode points to entry */

		/* dirnode is already to be removed */

		if (next_dirnode->lock & _DIRNODE_RMLOCK) goto eagain;
		search=next;
		search_row+=dirnode->junction[level].count - 1;

		/* here try to lock the region in this lane */

		if (dirnode->junction[level].lock & _DIRNODE_WRITELOCK) {

		    goto eagain;

		} else if (dirnode->junction[level].lock>>3 > 0) {

		    dirnode->junction[level].lock|=_DIRNODE_WRITELOCK;
		    vector.lane[level].lockset|=_DIRNODE_WRITELOCK;
		    vector.lockset|=_DIRNODE_WRITELOCK;

		    while (dirnode->junction[level].lock>>3 > 0) {

			pthread_cond_wait(&sl->cond, &sl->mutex);

		    }

		} else {

		    dirnode->junction[level].lock+=_DIRNODE_WRITELOCK;
		    vector.lane[level].lockset+=_DIRNODE_WRITELOCK;
		    vector.lockset|=_DIRNODE_WRITELOCK;

		}

		next_dirnode->lock|=_DIRNODE_RMLOCK;
		vector.lockset|=_DIRNODE_RMLOCK;
		found=next;
		*error=0;

		level--;
		if (level>=0) vector.lane[level].dirnode=dirnode;

		pthread_mutex_unlock(&sl->mutex);

		while (level>=0) {

		    pthread_mutex_lock(&sl->mutex);

		    dirnode=vector.lane[level].dirnode;

		    if (dirnode->junction[level].next==next_dirnode) {

			/* go one level down */

			if (dirnode->junction[level].lock & _DIRNODE_WRITELOCK) {

			    goto eagain;

			} else if (dirnode->junction[level].lock>>3 > 0) {

			    dirnode->junction[level].lock|=_DIRNODE_WRITELOCK;
			    vector.lane[level].lockset|=_DIRNODE_WRITELOCK;
			    vector.lockset|=_DIRNODE_WRITELOCK;

			    while(dirnode->junction[level].lock>>3 > 0) {

				pthread_cond_wait(&sl->cond, &sl->mutex);

			    }

			} else {

			    dirnode->junction[level].lock|=_DIRNODE_WRITELOCK;
			    vector.lane[level].lockset|=_DIRNODE_WRITELOCK;
			    vector.lockset|=_DIRNODE_WRITELOCK;

			}

			level--;
			if (level>=0) vector.lane[level].dirnode=dirnode;

		    } else {

			/* step */

			vector.lane[level].step+=dirnode->junction[level].count;
			vector.lane[level].dirnode=dirnode->junction[level].next;

		    }

		    pthread_mutex_unlock(&sl->mutex);

		}

		goto found;

	    } else if (diff>0) {

		if (next_dirnode->lock & _DIRNODE_RMLOCK) {

		    /* this dirnode is about to be removed: break */

		    goto eagain;

		} else if (dirnode->junction[level].lock & _DIRNODE_WRITELOCK) {

		    /* this region in this fast lane is already locked */

		    goto eagain;

		} else {

		    dirnode->junction[level].lock|=_DIRNODE_WRITELOCK;
		    vector.lane[level].lockset|=_DIRNODE_WRITELOCK;
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

	    } else {

		/* step */

		next=sl->ops.next(next);

		vector.lane[level].step+=dirnode->junction[level].count;
		vector.lane[level].dirnode=next_dirnode;

		if (next) {

		    search=next;
		    search_row+=dirnode->junction[level].count;

		} else {

		    search=next_dirnode->data;
		    search_row+=dirnode->junction[level].count - 1;

		}

	    }

	    pthread_mutex_unlock(&sl->mutex);

	}

    } else {

	/* no fast lanes */

	if (sl->ops.prelock(sl, ptr)==-1) {

	    *error=EAGAIN;
	    goto unlock;

	}

    }


    /*
	when here, the algoritm has reached the lowest level, and that is the
	linked list of entries
	check first there is not already an exact match found.
    */

    if (*error==ENOENT) {

	while (search) {

	    diff=sl->ops.compare(search, lookupdata);

	    if (diff<0) {

		/* before name still */

		search=sl->ops.next(search);
		search_row++;

	    } else if (diff==0) {

		/* exact match */

		*error=0;
		found=search;
		break;

	    } else {

		/* past name: not found */

		search=NULL;
		break;

	    }

	}

    }

    found:

    if (found && *error==0) {

	/* correct the counters and eventually remove the dirnode */

	remove_dirnodes_sl(sl, &vector, found);

	/* remove from the linked list */

	sl->ops.delete(found, sl);

    }

    unlock:

    sl->ops.unlock(sl, ptr);
    destroy_vector_lanes(&vector);

    if (row) *row=search_row;
    return;

    eagain:

    *error=EAGAIN;
    vector.minlevel=level+1;
    unlock_dirnode_vector(&vector);
    pthread_cond_broadcast(&sl->cond);
    pthread_mutex_unlock(&sl->mutex);

    sl->ops.unlock(sl, ptr);
    destroy_vector_lanes(&vector);

    if (row) *row=search_row;

}

void delete_sl(struct skiplist_struct *sl, void *lookupdata, unsigned int *row, unsigned int *error)
{
    unsigned int count=0;

    while(count<10) {

	delete_nonempty_sl(sl, lookupdata, row, error);

	if (*error==0 || *error==EALREADY) {

	    break;

	} else if (*error==EAGAIN) {

	    logoutput_warning("delete_sl: operation blocked, try again");

	} else {

	    logoutput_error("delete_sl: error %i", *error);
	    break;

	}

	count++;

    }

}

static void delete_nonempty_sl_batch(struct skiplist_struct *sl, void *lookupdata, unsigned int *row, unsigned int *error)
{
    void *search=NULL, *found=NULL;
    struct vector_dirnode_struct vector;
    int diff=0;
    int ctr=0;
    unsigned int search_row=0;

    init_vector(&vector);

    search=sl->ops.first(sl);
    search_row=1;
    *error=ENOENT;

    /*
	when there are fastlanes, take them first
    */

    if (sl->dirnode) {
	struct dirnode_struct *dirnode=sl->dirnode;
	struct dirnode_struct *next_dirnode=NULL;
	int level=dirnode->level;
	void *next=NULL;

	/* head of the skiplist */

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

	    if (diff==0) {

		search=next;
		ctr+=dirnode->junction[level].count;
		search_row+=dirnode->junction[level].count;
		*error=0;
		found=search;

		level--;
		if (level>=0) vector.lane[level].dirnode=dirnode;

		while (level>=0) {

		    dirnode=vector.lane[level].dirnode;

		    if (dirnode->junction[level].next==next_dirnode) {

			/* go one level down */

			level--;
			if (level>=0) vector.lane[level].dirnode=dirnode;

		    } else {

			/* step */

			vector.lane[level].step+=dirnode->junction[level].count;
			vector.lane[level].dirnode=dirnode->junction[level].next;

		    }

		}

		goto found;

	    } else if (diff>0) {

		level--;
		if (level>=0) vector.lane[level].dirnode=dirnode;

	    } else {

		/* difference<0: step */

		vector.lane[level].step+=dirnode->junction[level].count;
		vector.lane[level].dirnode=next_dirnode;

		next=sl->ops.next(next);

		if (next) {

		    search=next;
		    search_row+=dirnode->junction[level].count;

		} else {

		    search=next_dirnode->data;
		    search_row=dirnode->junction[level].count - 1;

		}

	    }

	}

    }

    /*
	when here, the algoritm has reached the lowest level, and that is the
	linked list of entries
	check first there is not already an exact match found..
    */

    if (*error==ENOENT) {

	while (search) {

	    diff=sl->ops.compare(search, lookupdata);

	    if (diff<0) {

		/* before name still */

		search=sl->ops.next(search);
		ctr++;
		search_row++;

	    } else if (diff==0) {

		/* exact match */

		*error=0;
		found=search;
		break;

	    } else {

		/* past name: not found */

		break;

	    }

	}

    }

    found:

    if (found) {

	/*
	    correct the counters and eventually
	    remove the dirnode
	*/

	remove_dirnodes_sl(sl, &vector, found);
	sl->ops.delete(found, sl);

    }

    out:

    if (row) *row=search_row;

}

void delete_sl_batch(struct skiplist_struct *sl, void *lookupdata, unsigned int *row, unsigned int *error)
{

    delete_nonempty_sl_batch(sl, lookupdata, row, error);

}
