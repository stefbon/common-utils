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

#ifndef SB_COMMON_UTILS_FSCHANGENOTIFY_H
#define SB_COMMON_UTILS_FSCHANGENOTIFY_H

#include "simple-list.h"

#define _FSEVENT_STATUS_OK				0
#define _FSEVENT_STATUS_PROCESSING			1
#define _FSEVENT_STATUS_QUEUE				2
#define _FSEVENT_STATUS_DONE				4

#define NOTIFYWATCH_BACKEND_OS				1
#define NOTIFYWATCH_BACKEND_FSSYNC			2
#define NOTIFYWATCH_BACKEND_LINUX_INOTIFY		4
#define NOTIFYWATCH_BACKEND_LINUX_FANOTIFY		8

#define NOTIFYWATCH_FLAG_SYSTEM				1
#define NOTIFYWATCH_FLAG_NOTIFY				2

#define NOTIFYWATCH_MASK_ATTRIB				1
#define NOTIFYWATCH_MASK_MODIFY				2
#define NOTIFYWATCH_MASK_OPEN				4
#define NOTIFYWATCH_MASK_CLOSE_NOWRITE			8
#define NOTIFYWATCH_MASK_CLOSE_WRITE			16
#define NOTIFYWATCH_MASK_CREATE				32
#define NOTIFYWATCH_MASK_DELETE				64
#define NOTIFYWATCH_MASK_MOVE_SELF			128

#define NOTIFYWATCH_IGNORE_SYSTEM			1

struct fsevent_s {
    struct notifywatch_s		*watch;
    struct fsevent_s			*next;
    struct fsevent_s			*prev;
    struct fseventbackend_s		*functions;
    union {
	struct linuxinotify_s	{
	    uint32_t			cookie;
	    uint32_t			mask;
	    unsigned int 		lenname;
	    char			*name;
	} linuxinotify;
	struct linuxfanotify_s	{
	    unsigned int 		pathlen;
	    char			*path;
	    uint64_t			mask;
	    uint32_t			pid;
	} linuxfanotify;
    } backend;
};

struct notifywatch_s {
    unsigned char 			flags;
    unsigned char			ignore;
    struct pathinfo_s 			pathinfo;
    uint32_t 				mask;
    pthread_mutex_t 			mutex;
    void 				(* process_fsevent) (struct notifywatch_s *watch, struct fsevent_s *fsevent, struct name_s *xname, unsigned int mask);
    struct watchbackend_s 		*backend;
    void 				*data;
    struct list_element_s 		list;
};

struct watchbackend_s {
    unsigned char 			type;
    int 				(* set_watch) (struct notifywatch_s *watch, uint32_t mask);
    void 				(* remove_watch) (struct notifywatch_s *watch);
    void 				(* init)(struct beventloop_s *loop, unsigned int *error);
    void 				(* disable)(struct notifywatch_s *watch, struct name_s *xname);
    void 				(* close)();
};

struct fseventbackend_s {
    unsigned char			type;
    int 				(* complete)(struct fsevent_s *fsevent, unsigned int *mask, struct name_s *xname);
    signed char				(* is_move)(struct fsevent_s *fsevent, unsigned int *cookie, unsigned int *error);
    signed char				(* is_dir)(struct fsevent_s *fsevent, unsigned int *error);
    unsigned int			(* get_pid)(struct fsevent_s *fsevent, unsigned int *error);
    void				(* free)(struct fsevent_s *fsevent);
};

// Prototypes

void lock_watch(struct notifywatch_s *watch);
void unlock_watch(struct notifywatch_s *watch);

struct notifywatch_s *add_notifywatch(unsigned int mask, struct pathinfo_s *pathinfo, void (* cb) (struct notifywatch_s *watch, struct fsevent_s *fsevent, struct name_s *xname, unsigned int mask), void *data, unsigned int *error);
void remove_notifywatch(struct notifywatch_s *watch);

void set_ignore_fsevents(struct notifywatch_s *watch, unsigned char ignore);
unsigned char test_watch_ignore(struct notifywatch_s *watch, char *name);

int init_fschangenotify(struct beventloop_s *loop, unsigned int *error);
void end_fschangenotify();

void queue_fsevent(struct fsevent_s *fsevent);

unsigned int get_fsevent_pid(struct fsevent_s *fsevent, unsigned int *error);
signed char fsevent_is_dir(struct fsevent_s *fsevent, unsigned int *error);
signed char fsevent_is_move(struct fsevent_s *fsevent, unsigned int *cookie, unsigned int *error);

void disable_fsevent_file(struct notifywatch_s *watch, struct name_s *xname);

#endif
