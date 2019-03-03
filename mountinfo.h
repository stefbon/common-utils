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

#ifndef SB_COMMON_UTILS_MOUNTINFO_H
#define SB_COMMON_UTILS_MOUNTINFO_H

#include "simple-list.h"

#define MOUNTLIST_CURRENT		0
#define MOUNTLIST_ADDED			1
#define MOUNTLIST_REMOVED		2
#define MOUNTLIST_FSTAB			5

#define MOUNTENTRY_FLAG_AUTOFS_DIRECT	2
#define MOUNTENTRY_FLAG_AUTOFS_INDIRECT	4
#define MOUNTENTRY_FLAG_AUTOFS		6
#define MOUNTENTRY_FLAG_BY_AUTOFS	8
#define MOUNTENTRY_FLAG_REMOUNT		16
#define MOUNTENTRY_FLAG_PROCESSED	32

struct mountentry_s {
    unsigned long 			unique;
    unsigned long 			generation;
    char 				*mountpoint;
    char 				*rootpath;
    char 				*fs;
    char 				*source;
    char 				*options;
    int 				minor;
    int 				major;
    unsigned char 			flags;
    struct list_element_s		list;
    void 				*index;
    void 				*data;
};

/* prototypes */

unsigned long get_uniquectr();
void increase_generation_id();
unsigned long generation_id();

int compare_mount_entries(struct mountentry_s *a, struct mountentry_s *b);
void check_mounted_by_autofs(struct mountentry_s *mountentry);

struct mountentry_s *get_rootmount();
void set_rootmount(struct mountentry_s *mountentry);

struct mountentry_s *get_containing_mountentry(struct list_element_s *list);

#endif
