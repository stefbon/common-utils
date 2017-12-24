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
#include "utils.h"
#include "logging.h"

extern int lock_beventloop(struct beventloop_s *loop);
extern int unlock_beventloop(struct beventloop_s *loop);

struct bevent_xdata_s *get_containing_xdata(struct list_element_s *list)
{
    return (struct bevent_xdata_s *) ( ((char *) list) - offsetof(struct bevent_xdata_s, list));
}

unsigned int set_bevent_name(struct bevent_xdata_s *xdata, char *name, unsigned int *error)
{
    unsigned int copied=0;
    unsigned int len=strlen(name);

    memset(&xdata->name, '\0', BEVENT_NAME_LEN);
    if (error) *error=0;

    if (len < BEVENT_NAME_LEN) {

	strcpy(&xdata->name[0], name);
	copied=len;

    } else {

	strncpy(&xdata->name[0], name, BEVENT_NAME_LEN - 1);
	copied=BEVENT_NAME_LEN - 1;
	if (error) *error=ENAMETOOLONG;

    }

    return copied;

}

char *get_bevent_name(struct bevent_xdata_s *xdata)
{
    return xdata->name;
}

int strcmp_bevent(struct bevent_xdata_s *xdata, char *name, unsigned int *error)
{
    unsigned int len=strlen(name);

    if (len < BEVENT_NAME_LEN) {

	return strcmp(&xdata->name[0], name);

    } else {

	*error=ENAMETOOLONG;
	return strncmp(&xdata->name[0], name, BEVENT_NAME_LEN - 1);

    }

    return 0;

}

void init_xdata(struct bevent_xdata_s *xdata)
{
    unsigned int error=0;

    xdata->fd=0;
    xdata->data=NULL;
    xdata->status=0;
    xdata->callback=NULL;
    xdata->list.next=NULL;
    xdata->list.prev=NULL;
    xdata->loop=NULL;

    memset(&xdata->name, '\0', BEVENT_NAME_LEN);
    set_bevent_name(xdata, "unknown", &error);

}

static void add_xdata_to_list(struct bevent_xdata_s *xdata)
{
    struct beventloop_s *loop=xdata->loop;

    if ( ! loop) {

	loop=get_mainloop();
	xdata->loop=loop;

    }

    /* add at tail */

    xdata->list.prev=NULL;
    xdata->list.next=NULL;
    add_list_element_last(&loop->xdata_list.head, &loop->xdata_list.tail, &xdata->list);

}

static void remove_xdata_from_list(struct bevent_xdata_s *xdata)
{
    struct beventloop_s *loop=xdata->loop;

    if (loop) remove_list_element(&loop->xdata_list.head, &loop->xdata_list.tail, &xdata->list);
    xdata->list.prev=NULL;
    xdata->list.next=NULL;

}

struct bevent_xdata_s *add_to_beventloop(int fd, uint32_t events, bevent_cb callback, void *data, struct bevent_xdata_s *xdata, struct beventloop_s *loop)
{
    struct epoll_event e_event;

    if ( ! loop) loop=get_mainloop();

    lock_beventloop(loop);

    if ( ! xdata ) {

	xdata=malloc(sizeof(struct bevent_xdata_s));

	if (xdata) {

	    init_xdata(xdata);
	    xdata->status|=BEVENT_OPTION_ALLOCATED;

	} else {

	    goto unlock;

	}

    }

    xdata->fd=fd;
    xdata->data=data;
    xdata->callback=callback;
    xdata->list.next=NULL;
    xdata->list.prev=NULL;
    xdata->loop=loop;

    e_event.events=events;
    e_event.data.ptr=(void *) xdata;

    if (epoll_ctl(loop->fd, EPOLL_CTL_ADD, fd, &e_event)==-1) {

        if (xdata->status & BEVENT_OPTION_ALLOCATED) {

	    free(xdata);
    	    xdata=NULL;

	}

	goto unlock;

    }

    add_xdata_to_list(xdata);
    xdata->status|=BEVENT_OPTION_ADDED;

    unlock:

    unlock_beventloop(loop);
    return xdata;

}

int remove_xdata_from_beventloop(struct bevent_xdata_s *xdata)
{
    struct beventloop_s *loop=xdata->loop;

    if (! loop) return;;

    lock_beventloop(loop);

    if (xdata->fd>0) {

	if (loop->fd>0 && (xdata->status & BEVENT_OPTION_ADDED)) {

	    epoll_ctl(loop->fd, EPOLL_CTL_DEL, xdata->fd, NULL);
	    xdata->status-=BEVENT_OPTION_ADDED;
	}

	xdata->fd=0;

    }

    remove_xdata_from_list(xdata);
    unlock_beventloop(loop);

    return 0;
}

struct bevent_xdata_s *get_next_xdata(struct beventloop_s *loop, struct bevent_xdata_s *xdata)
{
    struct list_element_s *list=NULL;

    if (xdata) {

	list=xdata->list.next;

    } else {

	if ( ! loop) loop=get_mainloop();
	list=loop->xdata_list.head;

    }

    return (list) ? get_containing_xdata(list) : NULL;

}
