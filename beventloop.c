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

#ifndef _REENTRANT
#define _REENTRANT
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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
#include "beventloop-timer.h"
#include "beventloop-signal.h"
#include "utils.h"
#include "logging.h"

static struct beventloop_s beventloop_main;
static pthread_mutex_t global_mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned char init=0;
static char *name="beventloop";

int lock_beventloop(struct beventloop_s *loop)
{
    return pthread_mutex_lock(&global_mutex);
}

int unlock_beventloop(struct beventloop_s *beventloop)
{
    return pthread_mutex_unlock(&global_mutex);
}

static void signal_cb_dummy(struct beventloop_s *b, void *data, struct signalfd_siginfo *fdsi)
{
}

static void _run_expired_dummy(struct beventloop_s *loop)
{
}

static void clear_eventloop(struct beventloop_s *loop)
{
    struct timer_list_s *timers=&loop->timer_list;

    memset(loop, 0, sizeof(struct beventloop_s));

    loop->status=0;
    loop->options=0;
    init_list_header(&loop->xdata_list.header, SIMPLE_LIST_TYPE_EMPTY, NULL);
    loop->cb_signal=signal_cb_dummy;
    loop->fd=0;

    init_list_header(&timers->header, SIMPLE_LIST_TYPE_EMPTY, NULL);
    timers->fd=0;
    timers->run_expired=_run_expired_dummy;
    pthread_mutex_init(&timers->mutex, NULL);
    timers->threadid=0;
}

int init_beventloop(struct beventloop_s *loop, unsigned int *error)
{

    pthread_mutex_lock(&global_mutex);

    if (init==0) {

	clear_eventloop(&beventloop_main);
	init=1;

    }

    pthread_mutex_unlock(&global_mutex);

    if (! loop) loop=&beventloop_main;

    /* create an epoll instance */

    loop->fd=epoll_create(MAX_EPOLL_NRFDS);

    if (loop->fd==-1) {

	logoutput("init_beventloop: error %i creating epoll instance (%s)", errno, strerror(errno));

	*error=errno;
	goto error;

    }

    loop->status=BEVENTLOOP_STATUS_SETUP;
    init_list_header(&loop->xdata_list.header, SIMPLE_LIST_TYPE_EMPTY, NULL);
    loop->xdata_list.header.name=name;
    init_list_header(&loop->timer_list.header, SIMPLE_LIST_TYPE_EMPTY, NULL);
    return 0;

    error:

    if (loop) {

	if (loop->fd>0) {

	    close(loop->fd);
	    loop->fd=0;

	}

	loop->status=BEVENTLOOP_STATUS_DOWN;

    }

    return -1;

}

int start_beventloop(struct beventloop_s *loop)
{
    struct epoll_event epoll_events[MAX_EPOLL_NREVENTS];
    int count=0;
    struct bevent_xdata_s *xdata;
    int result=0;

    if (! loop) loop=&beventloop_main;
    loop->status=BEVENTLOOP_STATUS_UP;

    while (loop->status==BEVENTLOOP_STATUS_UP) {

        count=epoll_wait(loop->fd, epoll_events, MAX_EPOLL_NREVENTS, -1);

        if (count<0) {

	    loop->status=BEVENTLOOP_STATUS_DOWN;
            break; /* good way to handle this ??*/

        }

        for (unsigned int i=0; i<count; i++) {

            xdata=(struct bevent_xdata_s *) epoll_events[i].data.ptr;
            logoutput("start_beventloop: xdata name %s", xdata->name);
	    result=(*xdata->callback) (xdata->fd, xdata->data, epoll_events[i].events);

        }

    }

    loop->status=BEVENTLOOP_STATUS_DOWN;

    if (loop->fd>0) {

	close(loop->fd);
	loop->fd=0;

    }

    out:

    return 0;

}

void stop_beventloop(struct beventloop_s *loop)
{
    if (!loop) loop=&beventloop_main;
    loop->status=BEVENTLOOP_STATUS_DOWN;
}

void clear_beventloop(struct beventloop_s *loop)
{
    struct list_element_s *list=NULL;
    int res;

    if (! loop) loop=&beventloop_main;

    lock_beventloop(loop);

    getxdata:

    list=get_list_head(&loop->xdata_list.header, 0);

    if (list) {
	struct bevent_xdata_s *xdata=get_containing_xdata(list);

	if (xdata->fd>0) {

	    if ((xdata->status & BEVENT_OPTION_ADDED) && loop->fd>0) epoll_ctl(loop->fd, EPOLL_CTL_DEL, xdata->fd, NULL);
	    xdata->fd=0; /* not closing fd here: associated system should do this, only removing from loop here */

	}

	if (xdata->status & BEVENT_OPTION_ALLOCATED) {

	    free(xdata);
	    xdata=NULL;

	}

	goto getxdata;

    }

    if (loop->fd>0) {

	close(loop->fd);
	loop->fd=0;

    }

    /* free any timer still in queue */

    pthread_mutex_lock(&loop->timer_list.mutex);

    gettimer:

    list=get_list_head(&loop->timer_list.header, 0);

    if (list) {
	struct timerentry_s *entry=get_containing_timerentry(list);

	free(entry);
	goto gettimer;

    }

    pthread_mutex_unlock(&loop->timer_list.mutex);
    pthread_mutex_destroy(&loop->timer_list.mutex);

    unlock_beventloop(loop);

}

struct beventloop_s *get_mainloop()
{
    return &beventloop_main;
}
