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
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <pthread.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#define WATCHES_TABLESIZE          1024

#include "workerthreads.h"
#include "pathinfo.h"
#include "beventloop.h"

#include "entry-management.h"
#include "directory-management.h"
#include "entry-utils.h"

#include "options.h"
#include "utils.h"
#include "simple-list.h"
#include "simple-hash.h"

#include "fuse-fs.h"
#include "workspaces.h"
#include "fschangenotify.h"

#include "logging.h"

struct list_watches_s {
    struct list_element_s	*head;
    struct list_element_s	*tail;
    pthread_mutex_t		mutex;
};

static struct list_watches_s list_watches;

static struct fsevent_s 		*fseventqueue_first=NULL;
static struct fsevent_s 		*fseventqueue_last=NULL;
static pthread_mutex_t 			fseventqueue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t			fseventqueue_threadid=0;

#ifdef __gnu_linux__

#ifdef HAVE_INOTIFY
#include <sys/inotify.h>

#include "fschangenotify-linux-inotify.c"
#define _FSCHANGENOTIFY_BACKEND_INOTIFY

#ifdef HAVE_FANOTIFY
#include <sys/fanotify.h>

#include "fschangenotify-linux-fanotify.c"
#define _FSCHANGENOTIFY_BACKEND_FANOTIFY

int set_watch_backend_os_specific(struct notifywatch_s *watch, unsigned int mask)
{
    unsigned int tmp=mask;
    uint32_t fanotify_mask=0;
    uint32_t inotify_mask=0;
    int result=-EINVAL;

    fanotify_mask=translate_mask_fsnotify_to_fanotify(&tmp);

    if (fanotify_mask>0) {

	result=set_watch_backend_fanotify(watch, fanotify_mask);

	if (result<0) {

	    logoutput_error("set_watch_backend_os_specific: error (%i:%s) setting fanotify", abs(result), strerror(abs(result)));
	    tmp=mask; /* reset to original mask */

	}

    }

    inotify_mask=translate_mask_fsnotify_to_inotify(&tmp);

    if (inotify_mask>0) {

	result=set_watch_backend_inotify(watch, inotify_mask);

	if (result<0) {

	    logoutput_error("set_watch_backend_os_specific: error (%i:%s) setting inotify", abs(result), strerror(abs(result)));

	}

    }

    return result;

}

void remove_watch_backend_os_specific(struct notifywatch_s *watch)
{
    unsigned int tmp=watch->mask;
    uint32_t fanotify_mask=0;
    uint32_t inotify_mask=0;

    fanotify_mask=translate_mask_fsnotify_to_fanotify(&tmp);

    if (fanotify_mask>0) {

        remove_watch_backend_fanotify(watch);

    }

    inotify_mask=translate_mask_fsnotify_to_inotify(&tmp);

    if (inotify_mask>0) {

	remove_watch_backend_inotify(watch);

    }

}

void initialize_fsnotify_backend(struct beventloop_s *loop, unsigned int *error)
{
    initialize_fanotify(loop, error);
    initialize_inotify(loop, error);
}

void close_fsnotify_backend()
{
    close_fanotify();
    close_inotify();
}

void disable_fsevent_file(struct notifywatch_s *watch, struct name_s *xname)
{
    disable_fanotify_file(watch, xname);
    disable_inotify_file(watch, xname);
}

static struct watchbackend_s os_watchbackend={
    .type=NOTIFYWATCH_BACKEND_LINUX_INOTIFY | NOTIFYWATCH_BACKEND_LINUX_FANOTIFY,
    .set_watch=set_watch_backend_os_specific,
    .remove_watch=remove_watch_backend_os_specific,
    .init=initialize_fsnotify_backend,
    .disable=disable_fsevent_file,
    .close=close_fsnotify_backend,
};

#else /* HAVE_FANOTIFY */

int set_watch_backend_os_specific(struct notifywatch_s *watch, unsigned int mask)
{
    uint32_t inotify_mask=0;
    int result=-EINVAL;

    inotify_mask=translate_mask_fsnotify_to_inotify(&mask);

    if (inotify_mask>0) {

	result=set_watch_backend_inotify(watch, inotify_mask);

	if (result<0) {

	    logoutput_error("set_watch_backend_os_specific: error (%i:%s) setting inotify", abs(result), strerror(abs(result)));

	}

    }

    return result;
}

void remove_watch_backend_os_specific(struct notifywatch_s *watch)
{
    remove_watch_backend_inotify(watch);
}

void initialize_fsnotify_backend(struct beventloop_s *loop, unsigned int *error)
{
    initialize_inotify(loop, error);
}

void close_fsnotify_backend()
{
    close_inotify();
}

void disable_fsevent_file(struct notifywatch_s *watch, struct name_s *xname)
{
    disable_inotify_file(watch, xname);
}

static struct watchbackend_s os_watchbackend={
    .type=NOTIFYWATCH_BACKEND_LINUX_INOTIFY,
    .set_watch=set_watch_backend_os_specific,
    .remove_watch=remove_watch_backend_os_specific,
    .init=initialize_fsnotify_backend,
    .disable=disable_fsevent_file,
    .close=close_fsnotify_backend,
};

#endif /* HAVE_FANOTIFY */


#else /* HAVE_INOTIFY */

static inline int set_watch_backend_os_specific(struct notifywatch_s *watch, unsigned int mask,)
{
    return -ENOSYS;
}

static inline void remove_watch_backend_os_specific(struct notifywatch_s *watch)
{
    return;
}

static inline void initialize_fsnotify_backend(unsigned int *error)
{
    return;
}

static inline void close_fsnotify_backend()
{
    return;

}

static void disable_fsevent_file(struct notifywatch_s *watch, struct name_s *xname)
{
}

static struct watchbackend_s os_watchbackend={
    .type=0,
    .set_watch=set_watch_backend_os_specific,
    .remove_watch=remove_watch_backend_os_specific,
    .init=initialize_fsnotify_backend,
    .disable=disable_fsevent_file,
    .close=close_fsnotify_backend,
};

#endif

#endif /*ifdef __gnu_linux */

void lock_watch(struct notifywatch_s *watch)
{
    pthread_mutex_lock(&watch->mutex);
}

void unlock_watch(struct notifywatch_s *watch)
{
    pthread_mutex_unlock(&watch->mutex);
}

static struct notifywatch_s *get_container_watch(struct list_element_s *list)
{
    return (struct notifywatch_s *) ( ((char *) list) - offsetof(struct notifywatch_s, list));
}

void add_watch_list_watches(struct notifywatch_s *watch)
{
    pthread_mutex_lock(&list_watches.mutex);
    add_list_element_first(&list_watches.head, &list_watches.tail, &watch->list);
    pthread_mutex_unlock(&list_watches.mutex);
}

void remove_watch_list_watches(struct notifywatch_s *watch)
{
    pthread_mutex_lock(&list_watches.mutex);
    remove_list_element(&list_watches.head, &list_watches.tail, &watch->list);
    pthread_mutex_unlock(&list_watches.mutex);
}

struct notifywatch_s *lookup_watch(struct pathinfo_s *pathinfo)
{
    struct notifywatch_s *watch=NULL;
    struct list_element_s *list=NULL;

    pthread_mutex_lock(&list_watches.mutex);
    list=list_watches.head;

    while(list) {

	watch=get_container_watch(list);
	if (strcmp(pathinfo->path, watch->pathinfo.path)==0) break;
	list=list->next;
	watch=NULL;

    }

    pthread_mutex_unlock(&list_watches.mutex);
    return watch;

}

static void assign_watchbackend(struct notifywatch_s *watch, unsigned int mask)
{
    int result=0;

    result=(* os_watchbackend.set_watch)(watch, mask);

    if (result>=0) {

	/* success */

	watch->backend=&os_watchbackend;

    } else {

	logoutput_error("assign_watchbackend: error %i setting os watch on %s", abs(result), watch->pathinfo.path);

    }

}

struct notifywatch_s *add_notifywatch( unsigned int mask,
					    struct pathinfo_s *pathinfo,
					    void (* cb) (struct notifywatch_s *watch, struct fsevent_s *fsevent, struct name_s *xname, unsigned int mask),
					    void *data,
					    unsigned int *error)
{
    struct notifywatch_s *watch=NULL;
    int result=0;

    logoutput("add_notifywatch: on %s mask %i", pathinfo->path, (int) mask);

    watch=lookup_watch(pathinfo);

    if (watch) {

	/* existing watch found on this path */

	*error=EEXIST;
	goto out;

    }

    watch=malloc(sizeof(struct notifywatch_s));

    if (watch) {

	watch->flags=0;
	watch->mask=mask;
	watch->ignore=0;

	pthread_mutex_init(&watch->mutex, NULL);

	watch->backend=NULL;
	watch->process_fsevent=cb;
	watch->data=data;
	watch->list.next=NULL;
	watch->list.prev=NULL;

	watch->pathinfo.path=malloc(pathinfo->len+1);

	if (watch->pathinfo.path) {

	    memset(watch->pathinfo.path, '\0', pathinfo->len+1);

	    memcpy(watch->pathinfo.path, pathinfo->path, pathinfo->len);
	    watch->pathinfo.len=pathinfo->len;
	    watch->pathinfo.flags=PATHINFO_FLAGS_ALLOCATED;

	} else {

	    logoutput_error("add_notifywatch: error allocating memory for path %s", pathinfo->path);
	    free(watch);
	    watch=NULL;
	    *error=ENOMEM;
	    goto out;

	}

	watch->mask=mask;
	add_watch_list_watches(watch);

    } else {

	logoutput_error("add_notifywatch: unable to allocate a watch");
	*error=ENOMEM;
	goto out;

    }

    pthread_mutex_lock(&watch->mutex);

    assign_watchbackend(watch, mask);

    unlock:

    pthread_mutex_unlock(&watch->mutex);

    out:

    return watch;

}

/*
    remove a watch
*/

void remove_notifywatch(struct notifywatch_s *watch)
{

    logoutput("remove_notifywatch");

    pthread_mutex_lock(&watch->mutex);
    (* os_watchbackend.remove_watch)(watch);
    remove_watch_list_watches(watch);
    pthread_mutex_unlock(&watch->mutex);

    pthread_mutex_destroy(&watch->mutex);
    free_path_pathinfo(&watch->pathinfo);
    free(watch);

}

void set_ignore_fsevents(struct notifywatch_s *watch, unsigned char ignore)
{
    pthread_mutex_lock(&watch->mutex);
    watch->ignore=ignore & NOTIFYWATCH_IGNORE_SYSTEM;
    pthread_mutex_unlock(&watch->mutex);
}

unsigned char test_watch_ignore(struct notifywatch_s *watch, char *name)
{
    if ((watch->ignore & NOTIFYWATCH_IGNORE_SYSTEM) && *name == '.') return 1;
    return 0;
}

void remove_list_watches()
{
    struct notifywatch_s *watch=NULL;
    struct list_element_s *list=NULL;

    pthread_mutex_lock(&list_watches.mutex);

    list=list_watches.head;

    while (list) {

	watch=get_container_watch(list);
	remove_list_element(&list_watches.head, &list_watches.tail, &watch->list);

	pthread_mutex_lock(&watch->mutex);
	watch->mask=0;
	(* watch->backend->remove_watch) (watch);
	pthread_mutex_unlock(&watch->mutex);

	pthread_mutex_destroy(&watch->mutex);
	free_path_pathinfo(&watch->pathinfo);
	free(watch);

	list=list_watches.head;

    }

    pthread_mutex_unlock(&list_watches.mutex);

}


int init_fschangenotify(struct beventloop_s *loop, unsigned int *error)
{
    list_watches.head=NULL;
    list_watches.tail=NULL;
    pthread_mutex_init(&list_watches.mutex, NULL);
    initialize_fsnotify_backend(loop, error);
    return (*error>0) ? -1 : 0;
}

void end_fschangenotify()
{
    remove_list_watches();
    close_fsnotify_backend();
}

/*
    function to process the fsevent queue

    typical usage is that it is processing the fseventqueue which is filled with fsevents,
    which are created by fs events in the folders to backup

    after these fsevents are created, they are put on a list (fseventsqueue)
    and a seperate thread is started. This thread will run this function.

*/

static void process_fseventqueue(void *data)
{
    struct fsevent_s *fsevent=NULL;

    processone:

    pthread_mutex_lock(&fseventqueue_mutex);

    if (fseventqueue_last) {

	fsevent=fseventqueue_last;
	fseventqueue_last=fsevent->prev;

	if (! fseventqueue_last) {

	    fseventqueue_first=NULL;

	} else {

	    fseventqueue_last->next=NULL;

	}

	fsevent->next=NULL;
	fsevent->prev=NULL;

	fseventqueue_threadid=pthread_self();

    } else {

	fseventqueue_threadid=0;

    }

    pthread_mutex_unlock(&fseventqueue_mutex);

    if (fsevent) {
	struct name_s xname={NULL, 0, 0};
	unsigned int mask=0;

	if ((*fsevent->functions->complete) (fsevent, &mask, &xname)==0) {
	    struct notifywatch_s *watch=fsevent->watch;

	    (* watch->process_fsevent)(watch, fsevent, &xname, mask);

	}

	(* fsevent->functions->free)(fsevent);

	free(fsevent);
	fsevent=NULL;

	goto processone;

    }

}

void queue_fsevent(struct fsevent_s *fsevent)
{

    pthread_mutex_lock(&fseventqueue_mutex);

    if (fseventqueue_first) {

	fsevent->next=fseventqueue_first;
	fseventqueue_first->prev=fsevent;

	fseventqueue_first=fsevent;

    } else {

	fseventqueue_first=fsevent;
	fseventqueue_last=fsevent;

    }

    if (fseventqueue_threadid==0) {
	unsigned int error=0;

	work_workerthread(NULL, 0, process_fseventqueue, NULL, &error);

	if (error>0) {

	    logoutput("queue_fsevent: error %i starting thread to process fsevent queue", error);

	}

    }

    pthread_mutex_unlock(&fseventqueue_mutex);

}

unsigned int get_fsevent_pid(struct fsevent_s *fsevent, unsigned int *error)
{
    return (* fsevent->functions->get_pid)(fsevent, error);
}

signed char fsevent_is_dir(struct fsevent_s *fsevent, unsigned int *error)
{
    return (* fsevent->functions->is_dir)(fsevent, error);
}

signed char fsevent_is_move(struct fsevent_s *fsevent, unsigned int *cookie, unsigned int *error)
{
    return (* fsevent->functions->is_move)(fsevent, cookie, error);
}
