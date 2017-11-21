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

#ifndef MOUNTINFO_MONITOR_H
#define MOUNTINFO_MONITOR_H

#define MOUNTINFO_FILE "/proc/self/mountinfo"

typedef int (* update_cb_t) (unsigned long generation, struct mountentry_s *(*next) (void **index, unsigned long generation, unsigned char type));
typedef unsigned char (* ignore_cb_t) (char *source, char *fs, char *path);

int open_mountmonitor(struct bevent_xdata_s *xdata, unsigned int *error);
void close_mountmonitor();

void set_updatefunc_mountmonitor(update_cb_t cb);
void set_ignorefunc_mountmonitor(ignore_cb_t cb);
void set_threadsqueue_mountmonitor(void *ptr);

struct mountentry_s *get_next_mountentry(void **index, unsigned long generation, unsigned char type);

#endif
