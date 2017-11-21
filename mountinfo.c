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

/* one global lock */

#define MOUNTINFO_STATUS_NONE			0
#define MOUNTINFO_STATUS_READ			1
#define MOUNTINFO_STATUS_WRITE			2

struct mountmanager_struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned char status;
    unsigned char readers;
};

static struct mountmanager_struct mountmanager={PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0};
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

    diff=g_strcmp0(a->mountpoint, b->mountpoint);

    if (diff==0) {

        diff=g_strcmp0(a->source, b->source);

        if (diff==0) diff=g_strcmp0(a->fs, b->fs);

    }

    return diff;

}

static int lock_mountlist_read(unsigned int *error)
{
    pthread_mutex_lock(&mountmanager.mutex);

    while(mountmanager.status & MOUNTINFO_STATUS_WRITE) {

	pthread_cond_wait(&mountmanager.cond, &mountmanager.mutex);

    }

    mountmanager.readers++;

    *error=0;

    pthread_cond_signal(&mountmanager.cond);
    pthread_mutex_unlock(&mountmanager.mutex);

    return 0;

}

static int unlock_mountlist_read(unsigned int *error)
{
    pthread_mutex_lock(&mountmanager.mutex);

    mountmanager.readers--;

    *error=0;

    pthread_cond_signal(&mountmanager.cond);
    pthread_mutex_unlock(&mountmanager.mutex);

    return 0;

}

static int lock_mountlist_write(unsigned int *error)
{

    pthread_mutex_lock(&mountmanager.mutex);

    *error=0;

    while ((mountmanager.status & MOUNTINFO_STATUS_WRITE) || mountmanager.readers>0) {

	pthread_cond_wait(&mountmanager.cond, &mountmanager.mutex);

    }

    mountmanager.status|=MOUNTINFO_STATUS_WRITE;

    unlock:

    pthread_mutex_unlock(&mountmanager.mutex);

    return 0;

}

static int unlock_mountlist_write(unsigned int *error)
{
    pthread_mutex_lock(&mountmanager.mutex);

    if (mountmanager.status & MOUNTINFO_STATUS_WRITE) mountmanager.status-=MOUNTINFO_STATUS_WRITE;

    pthread_cond_signal(&mountmanager.cond);
    pthread_mutex_unlock(&mountmanager.mutex);

    return 0;

}

int lock_mountlist(const char *what, unsigned int *error)
{

    *error=0;

    if (strcmp(what, "read")==0) {

	return lock_mountlist_read(error);

    } else if (strcmp(what, "write")==0) {

	return lock_mountlist_write(error);

    }

    *error=EINVAL;
    return -1;

}

int unlock_mountlist(const char *what, unsigned int *error)
{

    *error=0;

    if (strcmp(what, "read")==0) {

	return unlock_mountlist_read(error);

    } else if (strcmp(what, "write")==0) {

	return unlock_mountlist_write(error);

    }

    *error=EINVAL;
    return -1;

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
