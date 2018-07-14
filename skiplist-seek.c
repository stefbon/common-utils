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
#include "skiplist-seek.h"
#include "logging.h"

static void *seek_nonempty_sl(struct skiplist_struct *sl, void *lookupdata, unsigned int *error)
{
    void *search=NULL, *found=NULL;
    int level, diff=0;
    struct vector_dirnode_struct vector;
    void *ptr=NULL;

    ptr=sl->ops.create_rlock(sl);

    if (sl->ops.count(sl)==0) goto unlock;

    init_vector(&vector);

    /*
	first check the extreme cases: before the first or the last

	TODO: do something with the information the name to lookup
	is closer to the last than the first

    */

    search=sl->ops.last(sl);
    diff=sl->ops.compare(search, lookupdata);

    if (diff<0 || diff==0) {

	/* after the last: not found */

	*error=ENOENT;
	goto out;

    }

    search=sl->ops.first(sl);

    diff=sl->ops.compare(search, lookupdata);

    if (diff>0) {

	/* before the first: take that */

	found=search;
	goto out;

    } else if (diff==0) {

	/* exact match: take next */

	found=sl->ops.next(search);
	goto out;

    }

    *error=ENOENT;


    /*
	check there are fast lanes
    */

    if (sl->dirnode) {
	struct dirnode_struct *dirnode, *next_dirnode;
	void *next=NULL;

	/* head of the skiplist: there are fast lanes */

	dirnode=sl->dirnode;
	level=dirnode->level;

	if (add_vector_lanes(&vector, level, error)==-1) goto unlock;

	vector.maxlevel=level;
	vector.lane[level].dirnode=dirnode;

	while(level>=0) {

	    /* lock mutex directory */

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
		    res>0: this entry is too far, go one level down
		    check first it's possible to lock the region (it's not write locked)
		*/

		if (next_dirnode->lock & _DIRNODE_RMLOCK) {

		    /* this dirnode is about to be removed: break*/

		    *error=EAGAIN;
		    vector.minlevel=level+1;
		    unlock_dirnode_vector(&vector);
		    pthread_cond_broadcast(&sl->cond);
		    pthread_mutex_unlock(&sl->mutex);
		    goto out;

		} else if (dirnode->junction[level].lock & _DIRNODE_WRITELOCK) {

		    /* this region is write locked: no readers allowed */

		    *error=EAGAIN;
		    vector.minlevel=level+1;
		    unlock_dirnode_vector(&vector);
		    pthread_cond_broadcast(&sl->cond);
		    pthread_mutex_unlock(&sl->mutex);
		    goto out;

		}

		vector.lane[level].lockset|=_DIRNODE_READLOCK;
		vector.lockset|=_DIRNODE_READLOCK;

		dirnode->junction[level].lock+=_DIRNODE_READLOCK;

		level--;

		if (level>=0) vector.lane[level].dirnode=dirnode;

	    } else if (diff==0) {

		/* exact match: just ready here ?? */

		if (next_dirnode->lock & _DIRNODE_RMLOCK) {

		    *error=ENOENT;

		} else {

		    found=sl->ops.next(next);
		    *error=0;

		}

		vector.minlevel=level+1;
		unlock_dirnode_vector(&vector);
		pthread_cond_broadcast(&sl->cond);
		pthread_mutex_unlock(&sl->mutex);
		goto unlock;

	    } else {

		/* res<0: next_entry is smaller than name: skip */

		vector.lane[level].dirnode=next_dirnode;

		next=sl->ops.next(next);

		if (next) {

		    search=next;

		} else {

		    search=next_dirnode->data;

		}

	    }

	    pthread_mutex_unlock(&sl->mutex);

	}

    }

    /*
	walk the linked list, the closest entry is found, go from there
    */

    if (*error==ENOENT) {

	while (search) {

	    diff=sl->ops.compare(search, lookupdata);

	    if (diff<0) {

		/* before name still */

		search=sl->ops.next(search);

	    } else if (diff==0) {

		/* exact match */
		found=sl->ops.next(search);
		*error=0;
		break;

	    } else {

		/* past name, no exact match */
		found=search;
		break;

	    }

	}

    }

    out:

    if (vector.lockset>0) {

	pthread_mutex_lock(&sl->mutex);
	unlock_dirnode_vector(&vector);
	pthread_cond_broadcast(&sl->cond);
	pthread_mutex_unlock(&sl->mutex);

    }

    unlock:

    sl->ops.unlock(sl, ptr);

    return found;

}

void *seek_sl(struct skiplist_struct *sl, void *lookupdata, unsigned int *error)
{
    void *data=NULL;
    unsigned char count=0;

    while(count<10) {

	data=seek_nonempty_sl(sl, lookupdata, error);

	if (*error==EAGAIN) {

	    /* blocking lock: try again, here some timeout? */

	    logoutput("seek_sl: operation blocked, try again");
	    *error=0;

	} else {

	    break;

	}

	count++;

    }

    out:

    return data;

}

void *seek_sl_batch(struct skiplist_struct *sl, void *lookupdata, unsigned int *error)
{
    void *found=NULL, *search=NULL;
    int level, diff=0;
    struct vector_dirnode_struct vector;

    if (sl->ops.count(sl)==0) goto out;
    init_vector(&vector);

    /*
	first check the extreme cases: before the first or the last

	TODO: do something with the information the name to lookup
	is closer to the last than the first

    */

    search=sl->ops.first(sl);
    diff=sl->ops.compare(search, lookupdata);

    if (diff>0) {

	/* before the first: */

	found=search;
	goto out;

    } else if (diff==0) {

	/* exact match: take next */

	found=sl->ops.next(search);
	goto out;

    }

    search=sl->ops.last(sl);

    diff=sl->ops.compare(search, lookupdata);

    if (diff<0 || diff==0) {

	/* after the last: not found */

	*error=ENOENT;
	goto out;

    }

    search=sl->ops.first(sl);
    *error=ENOENT;

    /*
	check there are fast lanes
    */

    if (sl->dirnode) {
	struct dirnode_struct *dirnode, *next_dirnode;
	void *next=NULL;

	/* head of the skiplist: there are fast lanes */

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
		    res>0: this entry is too far, go one level down
		    check first it's possible to lock the region (it's not write locked)
		*/

		level--;
		if (level>=0) vector.lane[level].dirnode=dirnode;

	    } else if (diff==0) {

		/* exact match: just ready here ?? */

		vector.minlevel=level+1;
		found=sl->ops.next(next);
		goto out;

	    } else {

		/* res<0: next_entry is smaller than name: skip */

		vector.lane[level].dirnode=next_dirnode;

		next=sl->ops.next(next);

		if (next) {

		    search=next;

		} else {

		    search=next_dirnode->data;

		}

	    }

	}

    }

    /*
	walk the linked list, the closest entry is found, go from there
    */

    if (! found) {

	while (search) {

	    diff=sl->ops.compare(search, lookupdata);

	    if (diff<0) {

		/* before name still */

		search=sl->ops.next(search);

	    } else if (diff==0) {

		/* exact match */

		found=sl->ops.next(search);
		*error=0;
		break;

	    } else {

		/* past name, no exact match */

		found=search;
		break;

	    }

	}

    }

    out:

    return found;

}
