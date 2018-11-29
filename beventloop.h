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

#ifndef SB_COMMON_UTILS_BEVENTLOOP_H
#define SB_COMMON_UTILS_BEVENTLOOP_H

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#include "simple-list.h"

#define MAX_EPOLL_NREVENTS 			32
#define MAX_EPOLL_NRFDS				32

#define BEVENTLOOP_OK				0
#define BEVENTLOOP_EXIT				-1

#define BEVENTLOOP_STATUS_NOTSET		0
#define BEVENTLOOP_STATUS_SETUP			1
#define BEVENTLOOP_STATUS_UP			2
#define BEVENTLOOP_STATUS_DOWN			3

#define BEVENTLOOP_OPTION_TIMER			1
#define BEVENTLOOP_OPTION_SIGNAL		2

#define TIMERENTRY_STATUS_NOTSET		0
#define TIMERENTRY_STATUS_ACTIVE		1
#define TIMERENTRY_STATUS_INACTIVE		2
#define TIMERENTRY_STATUS_BUSY			3

#define BEVENT_OPTION_ALLOCATED			1
#define BEVENT_OPTION_ADDED			2
#define BEVENT_OPTION_TIMER			4
#define BEVENT_OPTION_SIGNAL			8

#define BEVENT_NAME_LEN				32

typedef int (*bevent_cb)(int fd, void *data, uint32_t events);

#define TIMERID_TYPE_PTR			1
#define TIMERID_TYPE_UNIQUE			2

struct timerid_s {
    void					*context;
    union					{
	void					*ptr;
	uint64_t				unique;
    } id;
    unsigned char 				type;
};

struct timerentry_s {
    struct timespec 				expire;
    unsigned char 				status;
    unsigned long 				ctr;
    void 					(*eventcall) (struct timerid_s *id, struct timespec *now);
    struct timerid_s				id;
    struct beventloop_s 			*loop;
    struct list_element_s			list;
};

struct beventloop_s;

struct timer_list_s {
    struct list_header_s			header;
    pthread_mutex_t 				mutex;
    pthread_t					threadid;
    unsigned int				fd;
    void					(* run_expired)(struct beventloop_s *loop);
};

/* struct to identify the fd when epoll signals activity on that fd */

struct bevent_xdata_s {
    int 					fd;
    void 					*data;
    unsigned char 				status;
    bevent_cb 					callback;
    char 					name[BEVENT_NAME_LEN];
    struct list_element_s			list;
    struct beventloop_s 			*loop;
};

struct xdata_list_s {
    struct list_header_s			header;
};

/* eventloop */

struct beventloop_s {
    unsigned char 				status;
    unsigned int				options;
    struct xdata_list_s				xdata_list;
    void 					(*cb_signal) (struct beventloop_s *loop, void *data, struct signalfd_siginfo *fdsi);
    int 					fd;
    struct timer_list_s				timer_list;
};

/* Prototypes */

int init_beventloop(struct beventloop_s *b, unsigned int *error);
int start_beventloop(struct beventloop_s *b);
void stop_beventloop(struct beventloop_s *b);
void clear_beventloop(struct beventloop_s *b);

struct beventloop_s *get_mainloop();

#endif
