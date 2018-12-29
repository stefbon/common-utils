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
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <inttypes.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <pwd.h>
#include <pthread.h>
#include <systemd/sd-login.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "utils.h"
#include "logging.h"

#include "pathinfo.h"
#include "beventloop.h"
#include "fuse-dentry.h"
#include "fuse-directory.h"
#include "fuse-utils.h"

#include "fschangenotify.h"
#include "monitorsessions.h"
#include "workerthreads.h"

static uid_t *current_uids=NULL;
static unsigned int current_uids_len=0;
static void (* session_change)(uid_t uid, char *what, signed char change);
static struct notifywatch_s *monitor_watch=NULL;

static void sort_array_uids(uid_t *uid, int len)
{
    uid_t tuid;
    int i, j;

    for (i=1;i<len;i++) {

	j=i-1;

	while(j>=0) {

	    /* compare element j and j+1 
               if element j has a bigger value, than swap and continue 
               if not than stop */

	    if ( uid[j] > uid[j+1] ) {

		tuid=uid[j];
		uid[j]=uid[j+1];
		uid[j+1]=tuid;

	    } else {

		break;

	    }

	    j--;

	}

    }

}

static void session_change_dummy(uid_t uid, char *what, signed char change)
{
}

static void session_monitor_cb(uid_t uid, signed char change)
{
    char *what=NULL;

    if (sd_uid_get_state(uid, &what)==0) {

	session_change(uid, what, change);
	free(what);

    }

}

static void compare_uids(uid_t *new, unsigned int len)
{

    if (current_uids_len==0) {

	for (unsigned int i; i<len; i++) session_monitor_cb(new[i], 1);

	current_uids=new;
	current_uids_len=len;

    } else if (len==0) {

	for (unsigned int i; i<current_uids_len; i++) session_monitor_cb(current_uids[i], -1);

	free(current_uids);
	current_uids=NULL;
	current_uids_len=0;

    } else {
	unsigned i=0,j=0;

	/* walk through both lists, i is index in current, j in new */

	while(1) {

	    if (i<current_uids_len && j<len) {

		if ( new[j]==current_uids[i]) {

		    session_monitor_cb(new[j], 0);
		    i++;
		    j++;
		    continue;

		} else if (new[j]<current_uids[i]) {

		    session_monitor_cb(new[j], 1);
		    j++;
		    continue;

		} else {

		    session_monitor_cb(current_uids[i], -1);
		    i++;
		    continue;

		}

	    } else {

		if ( i<current_uids_len) {

		    session_monitor_cb(current_uids[i], -1);
		    i++;
		    continue;

		} else if ( j<len ) {

		    session_monitor_cb(new[j], 1);
		    j++;
		    continue;

		} else {

		    /* both i and j are at the limit */

		    break;

		}

	    }

	}

	free(current_uids);
	current_uids=new;
	current_uids_len=len;

    }

}

static void process_sessions_change(void *ptr)
{
    uid_t *new=NULL;
    int len=0;

    len=sd_get_uids(&new);
    if (len>=0) sort_array_uids(new, len);

    compare_uids(new, (unsigned int) len);

}

static void start_process_sessions_change(struct notifywatch_s *watch, struct fsevent_s *fsevent, struct name_s *xname, unsigned int mask)
{

    if (watch==monitor_watch) {

	if (xname->len>0) {
	    unsigned int error=0;

	    if (mask & NOTIFYWATCH_MASK_DELETE) {
		uid_t uid=atoi(xname->name);

		logoutput("start_process_sessions_change: deleted %i", (unsigned int) uid);

	    } else if (mask & NOTIFYWATCH_MASK_CREATE) {
		uid_t uid=atoi(xname->name);

		logoutput("start_process_sessions_change: added %i", (unsigned int) uid);

	    } else if (mask & (NOTIFYWATCH_MASK_ATTRIB | NOTIFYWATCH_MASK_MODIFY)) {
		uid_t uid=atoi(xname->name);

		logoutput("start_process_sessions_change: changed %i", (unsigned int) uid);

	    }

	    work_workerthread(NULL, 0, process_sessions_change, NULL, &error);

	}

    }

}

int init_sessions_monitor(void (*cb)(uid_t uid, char *what, signed char change), struct beventloop_s *beventloop)
{
    int len=0;
    unsigned int mask=0;
    struct pathinfo_s pathinfo=PATHINFO_INIT;
    unsigned int error=0;

    session_change=session_change_dummy;
    len=sd_get_uids(&current_uids);

    if (len>0) {

	current_uids_len=(unsigned int) len;
	sort_array_uids(current_uids, current_uids_len);

    }

    session_change=cb;

    /* set watch */

    mask=NOTIFYWATCH_MASK_ATTRIB | NOTIFYWATCH_MASK_MODIFY | NOTIFYWATCH_MASK_CREATE | NOTIFYWATCH_MASK_DELETE;
    pathinfo.path="/run/systemd/users";
    pathinfo.len=strlen(pathinfo.path);

    monitor_watch=add_notifywatch(mask, &pathinfo, start_process_sessions_change, NULL, &error);

    return (monitor_watch) ? 0 : -1;

}

void close_sessions_monitor()
{

    if (monitor_watch) {

	remove_notifywatch(monitor_watch);
	monitor_watch=NULL;

    }

    if (current_uids) {

	free(current_uids);
	current_uids=NULL;
	current_uids_len=0;

    }

}

void process_current_sessions()
{
    process_sessions_change(NULL);
}
