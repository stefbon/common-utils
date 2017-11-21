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

#ifndef GENERIC_WORKERTHREADS_H
#define GENERIC_WORKERTHREADS_H

// Prototypes

void work_workerthread(void *queue, int timeout, void (*cb) (void *data), void *data, unsigned int *error);

void init_workerthreads(void *queue);
void stop_workerthreads(void *queue);
void terminate_workerthreads(void *queue, unsigned int timeout);

void set_max_numberthreads(void *queue, unsigned int m);
unsigned get_numberthreads(void *queue);
unsigned get_max_numberthreads(void *queue);

#endif
