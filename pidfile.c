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
#include <dirent.h>

#include "logging.h"
#include "utils.h"
#include "pathinfo.h"

void create_pid_file(struct pathinfo_s *pathinfo)
{
    char path[pathinfo->len + 32]; /* must be enough to hold the durectory and the %pid%.pid file */
    char *slash=NULL;

    memcpy(path, pathinfo->path, pathinfo->len + 1);
    unslash(path);

    slash=strrchr(path, '/');

    if (slash) {

	/* there must be a slash */

	sprintf(slash+1, "%i.pid", (int) getpid());

	if (mknod(path, S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 0)==0) {

	    logoutput("create_pid_file: created pid file %s", path);

	} else {

	    logoutput("create_pid_file: error %i:%s creating pid file %s", errno, strerror(errno), path);

	}

    }

}

void remove_pid_file(struct pathinfo_s *pathinfo, pid_t pid)
{
    char path[pathinfo->len + 32]; /* must be enough to hold the durectory and the %pid%.pid file */
    char *slash=NULL;

    memcpy(path, pathinfo->path, pathinfo->len + 1);
    unslash(path);

    slash=strrchr(path, '/');

    if (slash) {

	/* there must be a slash */

	sprintf(slash+1, "%i.pid", pid);
	unlink(path);

    }

}

unsigned int check_pid_file(struct pathinfo_s *pathinfo)
{
    char path[pathinfo->len + 1];
    char *slash=NULL;
    pid_t pid=0;

    memcpy(path, pathinfo->path, pathinfo->len + 1);
    unslash(path);

    slash=strrchr(path, '/');

    if (slash) {
	DIR *dp=NULL;

	*slash='\0';
	dp=opendir(path);

	if (dp) {
	    struct dirent *de=NULL;
	    char *sep=NULL;

	    de=readdir(dp);

	    while(de) {

		if (strcmp(de->d_name, ".")==0 || strcmp(de->d_name, "..")==0) goto next;

		sep=strrchr(de->d_name, '.');

		if (sep && strcmp(sep, ".pid")==0) {
		    unsigned int len=(int) (sep - de->d_name);
		    char name[len + 1];

		    memcpy(name, de->d_name, len + 1);
		    name[len]='\0';
		    pid=(pid_t) atoi(name);
		    break;

		}

		next:
		de=readdir(dp);

	    }

	    closedir(dp);

	}

    }

    if (pid>0) logoutput("check_pid_file: found pid file %i.pid", (int) pid);
    return (unsigned int) pid;

}
