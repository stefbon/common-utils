/*

  2018 Stef Bon <stefbon@gmail.com>

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

#ifndef GENERAL_SIMPLE_LOCKING_H
#define GENERAL_SIMPLE_LOCKING_H

#include "simple-list.h"

#define SIMPLE_LOCK_TYPE_NONE		0
#define SIMPLE_LOCK_TYPE_READ		1
#define SIMPLE_LOCK_TYPE_WRITE		2

#define SIMPLE_LOCK_FLAG_LIST		1
#define SIMPLE_LOCK_FLAG_WAITING	2
#define SIMPLE_LOCK_FLAG_EFFECTIVE	4
#define SIMPLE_LOCK_FLAG_ALLOCATED	8

struct simple_locking_s {
    pthread_mutex_t			mutex;
    pthread_cond_t			cond;
    struct list_header_s		readlocks;
    unsigned int			readers;
    struct list_header_s		writelocks;
    unsigned int			writers;
};

struct simple_lock_s {
    unsigned char			type;
    struct list_element_s		list;
    pthread_t				thread;
    unsigned char			flags;
    struct simple_locking_s		*locking;
    int					(* lock)(struct simple_lock_s *l);
    int					(* unlock)(struct simple_lock_s *l);
    int					(* upgrade)(struct simple_lock_s *l);
    int					(* prelock)(struct simple_lock_s *l);
};

/* prototypes */

void init_simple_readlock(struct simple_locking_s *locking, struct simple_lock_s *rlock);
void init_simple_writelock(struct simple_locking_s *locking, struct simple_lock_s *wlock);

int init_simple_locking(struct simple_locking_s *locking);
void clear_simple_locking(struct simple_locking_s *locking);

int simple_lock(struct simple_lock_s *lock);
int simple_unlock(struct simple_lock_s *lock);
int simple_prelock(struct simple_lock_s *lock);
int simple_upgradelock(struct simple_lock_s *lock);

#endif
