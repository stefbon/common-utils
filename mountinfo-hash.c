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
#include <pthread.h>

#include "beventloop.h"
#include "mountinfo.h"
#include "mountinfo-monitor.h"
#include "utils.h"
#include "simple-hash.h"

static struct simple_hash_s group_mounts;

/*
    functions to lookup a mount entry using the unique id
*/

static unsigned int calculate_mount_hash(unsigned long long unique)
{
    return unique % group_mounts.len;
}

static int mount_hashfunction(void *data)
{
    struct mountentry_s *mountentry=(struct mountentry_s *) data;
    return calculate_mount_hash(mountentry->unique);
}

struct mountentry_s *lookup_mount_unique(unsigned long long unique)
{
    unsigned int hashvalue=calculate_mount_hash(unique);
    void *index=NULL;
    struct mountentry_s *mountentry=NULL;

    lock_list_group(&group_mounts);

    mountentry=(struct mountentry_s *) get_next_hashed_value(&group_mounts, &index, hashvalue);

    while(mountentry) {

	if (mountentry->unique==unique) break;
	mountentry=(struct mountentry_s *) get_next_hashed_value(&group_mounts, &index, hashvalue);

    }

    unlock_list_group(&group_mounts);

    return mountentry;

}

void add_mount_hashtable(struct mountentry_s *mountentry)
{
    add_element_to_hash(&group_mounts, (void *) mountentry);
}

void remove_mount_hashtable(struct mountentry_s *mountentry)
{
    remove_element_from_hash(&group_mounts, (void *) mountentry);
}

int get_mountpoint(unsigned long unique, char *path, size_t len)
{
    struct mountentry_s *mountentry=lookup_mount_unique(unique);
    int result=0;

    if (mountentry) {
	size_t lenmountpoint=strlen(mountentry->mountpoint);

	if (path) {

	    if (lenmountpoint<len) {

		memcpy(path, mountentry->mountpoint, lenmountpoint);
		*(path+lenmountpoint)='\0';
		result=(int) lenmountpoint;

	    } else {

		result=-ENAMETOOLONG;

	    }

	} else {

	    result=(int) lenmountpoint;

	}

    } else {

	result=-ENOENT;

    }

    return result;

}

struct mountentry_s *get_mountentry_unique(unsigned long unique)
{
    return lookup_mount_unique(unique);
}

int init_mountinfo_hash()
{
    return initialize_group(&group_mounts, mount_hashfunction, 256, error);
}

void free_mountinfo_hash()
{
    free_group(&group_mounts, free_mountentry);
}
