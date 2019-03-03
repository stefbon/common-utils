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

#include "global-defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>
#include <strings.h>

#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mount.h>

#include <fcntl.h>
#include <pthread.h>

#include <glib.h>

#include "beventloop.h"
#include "mountinfo.h"
#include "utils.h"
#include "logging.h"

static struct mountentry_s *rootmount=NULL;
static unsigned long uniquectr=1;
static pthread_mutex_t ctr_mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned long generation=0;

unsigned long get_uniquectr()
{
    unsigned long result=0;

    pthread_mutex_lock(&ctr_mutex);
    result=uniquectr;
    uniquectr++;
    pthread_mutex_unlock(&ctr_mutex);

    return result;
}

unsigned long generation_id()
{
    return generation;
}

void increase_generation_id()
{
    generation++;
}


/* compare entries 
* if b is "bigger" then a this function returns a positive value (+1)
* if a is "bigger" then it returns -1
* if equal: 0

  the first item to compare: mountpoint
  the second               : mountsource
  the third                : filesystem
*/

int compare_mount_entries(struct mountentry_s *a, struct mountentry_s *b)
{
    int diff=0;

    logoutput("compare_mount_entries: compare %s:%s:%s with %s:%s:%s", a->source, a->mountpoint, a->fs, b->source, b->mountpoint, b->fs);

    diff=g_strcmp0(a->mountpoint, b->mountpoint);

    if (diff==0) {

        diff=g_strcmp0(a->source, b->source);
        if (diff==0) diff=g_strcmp0(a->fs, b->fs);

    }

    return diff;

}

void check_mounted_by_autofs(struct mountentry_s *mountentry)
{
    // struct mountentry_s *parent=mountentry->parent;

    // if (parent) {

	// if (strcmp(parent->fs, "autofs")==0) mountentry->flags|=MOUNTENTRY_FLAG_BY_AUTOFS;

    // }

}

void set_rootmount(struct mountentry_s *mountentry)
{
    rootmount=mountentry;
}

struct mountentry_s *get_rootmount()
{
    return rootmount;
}

struct mountentry_s *get_containing_mountentry(struct list_element_s *list)
{
    if (! list) return NULL;
    return (struct mountentry_s *) ( ((char *) list) - offsetof(struct mountentry_s, list));
}
