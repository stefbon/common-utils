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

#include <glib.h>

#include "logging.h"
#include "beventloop.h"
#include "workerthreads.h"
#include "mountinfo.h"
#include "mountinfo-monitor.h"
#include "utils.h"

/* simple function which gets the device (/dev/sda1 for example) from the uuid  */

char *get_device_from_uuid(const char *uuid)
{
    int len0=256;
    char *path;
    char *device=NULL;

    path=malloc(len0);

    if (path) {
	size_t size=32;
	char *buff=NULL;

	snprintf(path, len0, "/dev/disk/by-uuid/%s", uuid);

	while(1) {

	    if (buff) {

		buff=realloc(buff, size);

	    } else {

		buff=malloc(size);

	    }

	    if (buff) {
		int res=0;

		res=readlink(path, buff, size);

		if (res==-1) {

		    free(buff);
		    break;

		} else if (res==size) {

		    size+=32;
		    continue;

		} else {

		    *(buff+res)='\0';

		    if ( ! strncmp(buff, "/", 1)==0) {

			/* relative symlink */

			if (strlen("/dev/disk/by-uuid/") + res + 1 > len0) {

			    len0=strlen("/dev/disk/by-uuid/") + res + 1;

			    path=realloc(path, len0);

			    if ( ! path ) {

				free(buff);
				goto out;

			    }

			}

			snprintf(path, len0, "/dev/disk/by-uuid/%s", buff);
			device=realpath(path, NULL);

		    } else {

			/* absolute symlink */

			device=realpath(buff, NULL);

		    }

		    free(buff);
		    break;

		}

	    } else {

		break;

	    }

	}

	free(path);

    }

    out:

    return device;

}

char *get_real_root_device(int major, int minor)
{
    int res, nreturn=0, len0=32;
    char path[len0];
    char *buff=NULL;
    int size=64;

    /* try first /dev/block/major:minor, which is a symlink to the device */

    snprintf(path, len0, "/dev/block/%i:%i", major, minor);

    resize:

    if (buff) {

	buff=realloc(buff, size);

    } else {

	buff=malloc(size);

    }

    if (buff) {

	res=readlink(path, buff, size);

	if (res>=size) {

	    /* huh?? still not big enough */
	    size+=64;
	    goto resize;

	} else if (res==-1) {

	    logoutput_error("get_real_root_device: error %i reading link %s", errno, path);

	} else {

	    *(buff+res)='\0';

	    /* possibly a relative symlink */

	    if ( ! strncmp(buff, "/", 1)==0) {
		char relsymlink[strlen("/dev/block/") + res + 1];

		snprintf(relsymlink, len0, "/dev/block/%s", buff);
		free(buff);

		/* get the real target, without the slashes and .. */

		buff=realpath(path, NULL);

	    } else {

		logoutput_notice("get_real_root_device: found %s", buff);

	    }

	}

    }

    out:

    return buff;

}

