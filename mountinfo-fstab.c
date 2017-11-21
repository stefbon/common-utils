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
#include <fcntl.h>
#include <pthread.h>
#include <mntent.h>

#include <glib.h>

#include "beventloop.h"
#include "mountinfo.h"
#include "mountinfo-monitor.h"
#include "utils.h"

struct fstabentry_s {
    struct mntent 		fstab_mntent;
    struct mountentry_s 	*mountentry;
    struct fstabentry_s 	*next;
    struct fstabentry_s 	*prev;
};

struct fstabentry_s *fstabentry_list=NULL;

#define FSTAB_FILE "/etc/fstab"

void read_fstab()
{
    FILE *fp=NULL;
    struct fstabentry_s *fstabentry;
    struct mntent *fstab_mntent;

    fp=setmntent(FSTAB_FILE, "r");

    if (fp) {

	while(1) {

	    fstab_mntent=getmntent(fp);

	    if (fstab_mntent) {

		fstabentry=malloc(sizeof(struct fstabentry_s));

		if (fstabentry) {

		    fstabentry->fstab_mntent.mnt_fsname=NULL;
		    fstabentry->fstab_mntent.mnt_dir=NULL;
		    fstabentry->fstab_mntent.mnt_type=NULL;
		    fstabentry->fstab_mntent.mnt_opts=NULL;
		    fstabentry->fstab_mntent.mnt_freq=0;
		    fstabentry->fstab_mntent.mnt_passno=0;

		    if (strncmp(fstab_mntent->mnt_fsname, "UUID=", 5)==0) {

			fstabentry->fstab_mntent.mnt_fsname=get_device_from_uuid(fstab_mntent->mnt_fsname+strlen("UUID="));

		    } else {

			fstabentry->fstab_mntent.mnt_fsname=strdup(fstab_mntent->mnt_fsname);

		    }

		    if ( ! fstabentry->fstab_mntent.mnt_fsname) goto error;

		    fstabentry->fstab_mntent.mnt_dir=strdup(fstab_mntent->mnt_dir);

		    if ( ! fstabentry->fstab_mntent.mnt_dir) goto error;

		    fstabentry->fstab_mntent.mnt_type=strdup(fstab_mntent->mnt_type);

		    if ( ! fstabentry->fstab_mntent.mnt_type) goto error;

		    fstabentry->fstab_mntent.mnt_opts=strdup(fstab_mntent->mnt_opts);

		    if ( ! fstabentry->fstab_mntent.mnt_opts) goto error;

		    fstabentry->next=NULL;
		    fstabentry->prev=NULL;
		    fstabentry->mountentry=NULL;

		    /* insert in list */

		    if (fstabentry_list) fstabentry_list->prev=fstabentry;
		    fstabentry->next=fstabentry_list;
		    fstabentry_list=fstabentry;

		} else {

		    goto error;

		}

	    } else {

		/* no mntent anymore */

		break;

	    }

	}

	endmntent(fp);

    }

    return;

    error:

    if (fp) endmntent(fp);

    if (fstabentry) {

	if (fstabentry->fstab_mntent.mnt_fsname) free(fstabentry->fstab_mntent.mnt_fsname);
	if (fstabentry->fstab_mntent.mnt_dir) free(fstabentry->fstab_mntent.mnt_dir);
	if (fstabentry->fstab_mntent.mnt_type) free(fstabentry->fstab_mntent.mnt_type);
	if (fstabentry->fstab_mntent.mnt_opts) free(fstabentry->fstab_mntent.mnt_opts);

	free(fstabentry);

    }

}

unsigned char match_entry_in_fstab(char *mountpoint, char *fs, char *source)
{
    struct fstabentry_s *fstabentry;

    fstabentry=fstabentry_list;

    while(fstabentry) {

	if (strcmp(fstabentry->fstab_mntent.mnt_dir, mountpoint)==0 && strcmp(fstabentry->fstab_mntent.mnt_type, fs)==0 &&
	    strcmp(fstabentry->fstab_mntent.mnt_fsname, source)==0) {

	    return 1;

	}

	fstabentry=fstabentry->next;

    }

    return 0;

}

unsigned char device_found_in_fstab(char *device)
{
    struct fstabentry_s *fstabentry=NULL;

    fstabentry=fstabentry_list;

    while(fstabentry) {

	if (strcmp(fstabentry->fstab_mntent.mnt_fsname, device)==0) {

	    break;

	}

	fstabentry=fstabentry->next;

    }

    return (fstabentry) ? 1 : 0;

}
