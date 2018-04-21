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

struct simple_lock_s {
    struct list_element_s		list;
    pthread_t				thread;
};

struct simple_locking_s {
    pthread_mutex_t			mutex;
    pthread_cond_t			cond;
    struct list_header_s		readlocks;
    unsigned int			readers;
    struct list_header_s		writelocks;
    unsigned int			writers;
};

/* prototypes */

int init_simple_locking(struct simple_locking_s *locking);
void clear_simple_locking(struct simple_locking_s *locking);

int simple_readlock(struct simple_locking_s *locking);
int simple_readunlock(struct simple_locking_s *locking);
int simple_writelock(struct simple_locking_s *locking);
int simple_writeunlock(struct simple_locking_s *locking);
int simple_upgrade_readlock(struct simple_locking_s *locking);

#endif
