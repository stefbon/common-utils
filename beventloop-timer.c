/*
  2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017 Stef Bon <stefbon@gmail.com>

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

#define _REENTRANT
#define _GNU_SOURCE

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

#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <syslog.h>

#include "global-defines.h"

#include "beventloop.h"
#include "beventloop-xdata.h"
#include "utils.h"
#include "logging.h"
#include "workerthreads.h"

#define TIMERENTRY_STATUS_NOTSET		0
#define TIMERENTRY_STATUS_ACTIVE		1
#define TIMERENTRY_STATUS_INACTIVE		2
#define TIMERENTRY_STATUS_QUEUE			3

extern int lock_beventloop(struct beventloop_s *loop);
extern int unlock_beventloop(struct beventloop_s *loop);

static unsigned long timerctr=0;

struct timerentry_s *get_containing_timerentry(struct list_element_s *list)
{
    return (struct timerentry_s *) ( ((char *) list) - offsetof(struct timerentry_s, list));
}

static int set_timer(struct beventloop_s *loop)
{
    struct timerentry_s *timerentry=NULL;
    struct itimerspec new_value;
    int fd=0;
    int result=-1;
    struct list_element_s *list=NULL;

    if (! loop) loop=get_mainloop();

    fd=loop->timer_list.fd;
    list=loop->timer_list.head;

    if (list) {

	timerentry=get_containing_timerentry(list);
	new_value.it_value.tv_sec=timerentry->expire.tv_sec;
	new_value.it_value.tv_nsec=timerentry->expire.tv_nsec;

    } else {

	new_value.it_value.tv_sec=0;
	new_value.it_value.tv_nsec=0;

    }

    new_value.it_interval.tv_sec=0;
    new_value.it_interval.tv_nsec=0;

    /* (re) set the timer
    note:
    - the expired time is in absolute format (required to compare timerentries with each other )
    - when the timerentry is empty, then this still does what it should do: 
      in that case it disarms the timer */

    if (fd>0) result=timerfd_settime(fd, TFD_TIMER_ABSTIME, &new_value, NULL);
    if (timerentry) timerentry->status=(result==-1) ? TIMERENTRY_STATUS_INACTIVE : TIMERENTRY_STATUS_ACTIVE;

    return result;

}

static void disable_timer(struct beventloop_s *loop)
{
    struct itimerspec value;
    int fd=0;

    if ( ! loop) loop=get_mainloop();

    fd=loop->timer_list.fd;

    memset(&value, 0, sizeof(struct itimerspec));
    value.it_value.tv_sec=0;
    value.it_value.tv_nsec=0;

    value.it_interval.tv_sec=0;
    value.it_interval.tv_nsec=0;

    if (fd>0) timerfd_settime(fd, TFD_TIMER_ABSTIME, &value, NULL);

}

static unsigned char insert_timerentry(struct beventloop_s *loop, struct timerentry_s *new)
{
    struct timerentry_s *timerentry=NULL;
    struct list_element_s *list=NULL;
    unsigned char reset=0;

    list=loop->timer_list.head;

    while (list) {

	timerentry=get_containing_timerentry(list);
	if (timerentry->expire.tv_sec> new->expire.tv_sec || (timerentry->expire.tv_sec==new->expire.tv_sec && timerentry->expire.tv_nsec> new->expire.tv_nsec)) break;

	list=list->next;
	timerentry=NULL;

    }

    if (timerentry) {

	add_list_element_before(&loop->timer_list.head, &loop->timer_list.tail, &new->list, &timerentry->list);
	reset=(loop->timer_list.head==&new->list) ? 1 : 0;

    } else {

	add_list_element_last(&loop->timer_list.head, &loop->timer_list.tail, &new->list);

    }

    return reset;

}

static void dummy_eventcall(void *data)
{
}

static void init_timerentry(struct timerentry_s *timerentry, struct timespec *expire)
{

    timerentry->status=TIMERENTRY_STATUS_NOTSET;
    timerentry->ctr=0;

    if (expire) {

	timerentry->expire.tv_sec=expire->tv_sec;
	timerentry->expire.tv_nsec=expire->tv_nsec;

    } else {

	timerentry->expire.tv_sec=0;
	timerentry->expire.tv_nsec=0;

    }

    timerentry->eventcall=dummy_eventcall;
    timerentry->data=NULL;
    timerentry->list.next=NULL;
    timerentry->list.prev=NULL;

}

struct timerentry_s *create_timerentry(struct timespec *expire, void (*cb) (void *data), void *data, struct beventloop_s *loop)
{
    struct timerentry_s *entry=malloc(sizeof(struct timerentry_s));

    if (entry) {

	init_timerentry(entry, expire);
	entry->eventcall=cb;
	entry->data=data;
	entry->loop=loop;
	if (loop==NULL) loop=get_mainloop();
	entry->status=TIMERENTRY_STATUS_QUEUE;

	pthread_mutex_lock(&loop->timer_list.mutex);
	if (insert_timerentry(loop, entry)==1) set_timer(loop);
	pthread_mutex_unlock(&loop->timer_list.mutex);

    }

    return entry;

}

static void _run_expired_thread(void *ptr)
{
    struct beventloop_s *loop=(struct beventloop_s *) ptr;
    struct list_element_s *list=NULL;
    struct timerentry_s *timerentry=NULL;
    struct timespec rightnow;

    pthread_mutex_lock(&loop->timer_list.mutex);
    loop->timer_list.threadid=pthread_self();

    disable_timer(loop);

    getnexttimer:

    /* get the next timer */

    list=loop->timer_list.head;

    if (! list) {

	pthread_mutex_unlock(&loop->timer_list.mutex);
	set_timer(loop);
	return;

    }

    timerentry=get_containing_timerentry(list);

    get_current_time(&rightnow);

    if (timerentry->expire.tv_sec>rightnow.tv_sec || (timerentry->expire.tv_sec==rightnow.tv_sec && timerentry->expire.tv_nsec>rightnow.tv_nsec)) {

	/* timer is in future */
	loop->timer_list.threadid=0;
	pthread_mutex_unlock(&loop->timer_list.mutex);
	return;

    }

    remove_list_element(&loop->timer_list.head, &loop->timer_list.tail, list);
    pthread_mutex_unlock(&loop->timer_list.mutex);

    if (timerentry->status==TIMERENTRY_STATUS_INACTIVE) {

	free(timerentry);
	timerentry=NULL;
	pthread_mutex_lock(&loop->timer_list.mutex);
	goto getnexttimer;

    }

    (* timerentry->eventcall) (timerentry->data);
    free(timerentry);
    timerentry=NULL;
    pthread_mutex_lock(&loop->timer_list.mutex);
    goto getnexttimer;

}

static void run_expired(struct beventloop_s *loop)
{
    pthread_mutex_lock(&loop->timer_list.mutex);
    /* only start a thread if not already */
    if (loop->timer_list.threadid==0) {
	unsigned int error=0;

	work_workerthread(NULL, 0, _run_expired_thread, (void *)loop, &error);

    }
    pthread_mutex_unlock(&loop->timer_list.mutex);
}

void remove_timerentry(struct timerentry_s *entry)
{
    unsigned char reset=0;
    struct beventloop_s *loop=entry->loop;
    struct timer_list_s *timers=&loop->timer_list;

    entry->status=TIMERENTRY_STATUS_INACTIVE;

    pthread_mutex_lock(&timers->mutex);
    if (&entry->list==timers->head) reset=1;
    remove_list_element(&timers->head, &timers->tail, &entry->list);
    if (reset==1) set_timer(loop);
    pthread_mutex_unlock(&timers->mutex);
}

static int default_timer_cb(int fd, void *data, uint32_t events)
{
    struct beventloop_s *loop=(struct beventloop_s *) data;
    uint64_t expirations;

    if (read(fd, &expirations, sizeof(uint64_t))>0) (* loop->timer_list.run_expired)(loop);

    return 0;

}

int enable_beventloop_timer(struct beventloop_s *loop, unsigned int *error)
{
    struct bevent_xdata_s *xdata=NULL;
    int fd=0;

    if (! loop) loop=get_mainloop();

    fd=timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);

    *error=errno;
    if (fd == -1) goto error;

    xdata=add_to_beventloop(fd, EPOLLIN, default_timer_cb, NULL, NULL, loop);

    *error=errno;
    if ( !xdata ) goto error;

    *error=0;
    loop->timer_list.run_expired=run_expired;
    loop->timer_list.fd=fd;
    set_bevent_name(xdata, "TIMER", error);
    xdata->status|=BEVENT_OPTION_TIMER;
    loop->options|=BEVENTLOOP_OPTION_TIMER;
    return 0;

    error:

    if (fd>0) close(fd);
    if (xdata) free(xdata);
    return -1;

}
