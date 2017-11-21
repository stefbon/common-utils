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

#include <sys/stat.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>

#include "utils.h"

void init_common_buffer(struct common_buffer_s *c_buffer)
{
    c_buffer->ptr=NULL;
    c_buffer->pos=NULL;
    c_buffer->len=0;
    c_buffer->size=0;
}

void free_common_buffer(struct common_buffer_s *c_buffer)
{

    if (c_buffer->ptr) {

	free(c_buffer->ptr);
	c_buffer->ptr=NULL;

    }

    init_common_buffer(c_buffer);

}

void unslash(char *p)
{
    char *q = p;
    char *pkeep = p;

    while ((*q++ = *p++) != 0) {

	if (q[-1] == '/') {

	    while (*p == '/') {

		p++;
	    }

	}
    }

    if (q > pkeep + 2 && q[-2] == '/') q[-2] = '\0';
}

/* function to determine time a is seconds later than time b */

int is_later(struct timespec *ats, struct timespec *bts, int sec, long nsec)
{
    struct timespec result;

    result.tv_nsec = bts->tv_nsec + nsec;
    result.tv_sec = bts->tv_sec;

    if (result.tv_nsec > 1000000000) {

	result.tv_nsec -= 1000000000;
	result.tv_sec++;

    }

    if ( ats->tv_sec > result.tv_sec) {

	return 1;

    } else if ( ats->tv_sec==result.tv_sec) {

	if ( ats->tv_nsec > result.tv_nsec ) {

	    return 1;

	} else if ( ats->tv_nsec == result.tv_nsec ) {

	    return 0;

	}

    }

    return -1;

}

void get_current_time(struct timespec *rightnow)
{
    int res=clock_gettime(CLOCK_REALTIME, rightnow);
}


int compare_stat_time(struct stat *ast, struct stat *bst, unsigned char ntype)
{
    if ( ntype==1 ) {

	if ( ast->st_atime > bst->st_atime ) {

	    return 1;

	} else if ( ast->st_atime == bst->st_atime ) {

#ifdef  __USE_MISC

	    // time defined as timespec

	    if ( ast->st_atim.tv_nsec > bst->st_atim.tv_nsec ) return 1;

#else

	    if ( ast->st_atimensec > bst->st_atimensec ) return 1;

#endif

	}

    } else if ( ntype==2 ) {

	if ( ast->st_mtime > bst->st_mtime ) {

	    return 1;

	} else if ( ast->st_mtime == bst->st_mtime ) {

#ifdef  __USE_MISC

	    // time defined as timespec

	    if ( ast->st_mtim.tv_nsec > bst->st_mtim.tv_nsec ) return 1;

#else

	    if ( ast->st_mtimensec > bst->st_mtimensec ) return 1;

#endif

	}

    } else if ( ntype==3 ) {

	if ( ast->st_ctime > bst->st_ctime ) {

	    return 1;

	} else if ( ast->st_ctime == bst->st_ctime ) {

#ifdef  __USE_MISC

	    // time defined as timespec

	    if ( ast->st_ctim.tv_nsec > bst->st_ctim.tv_nsec ) return 1;

#else

	    if ( ast->st_ctimensec > bst->st_ctimensec ) return 1;

#endif

	}

    }

    return 0;

}

void copy_stat_times(struct stat *st_to, struct stat *st_from)
{

    st_to->st_atime=st_from->st_atime;
    st_to->st_mtime=st_from->st_mtime;
    st_to->st_ctime=st_from->st_ctime;

#ifdef  __USE_MISC

    /* time defined as timespec */

    st_to->st_atim.tv_nsec=st_from->st_atim.tv_nsec;
    st_to->st_mtim.tv_nsec=st_from->st_mtim.tv_nsec;
    st_to->st_ctim.tv_nsec=st_from->st_ctim.tv_nsec;

#else

    /* n sec defined as extra field */

    st_to->st_atimensec=st_from->st_atimensec;
    st_to->st_mtimensec=st_from->st_mtimensec;
    st_to->st_ctimensec=st_from->st_ctimensec;

#endif

}


void copy_stat(struct stat *st_to, struct stat *st_from)
{


    if ( st_to && st_from) {

	st_to->st_mode=st_from->st_mode;
	st_to->st_nlink=st_from->st_nlink;
	st_to->st_uid=st_from->st_uid;
	st_to->st_gid=st_from->st_gid;

	st_to->st_rdev=st_from->st_rdev;
	st_to->st_size=st_from->st_size;

	st_to->st_blksize=st_from->st_blksize;
	st_to->st_blocks=st_from->st_blocks;

    }

}

/* function to test path1 is a subdirectory of path2 */

unsigned char issubdirectory(const char *path1, const char *path2, unsigned char maybethesame)
{
    int lenpath2=strlen(path2);
    int lenpath1=strlen(path1);
    unsigned char issubdir=0;

    if ( maybethesame==1 ) {

	if ( lenpath1 < lenpath2 ) goto out;

    } else {

	if ( lenpath1 <= lenpath2 ) goto out;

    }

    if ( strncmp(path2, path1, lenpath2)==0 ) {

	if ( lenpath1>lenpath2 ) {

	    if ( strcmp(path2, "/")==0 ) {

		/* path2 is / */

		issubdir=2;

	    } else if ( strncmp(path1+lenpath2, "/", 1)==0 ) {

		/* is a real subdirectory */
		issubdir=2;

	    }

	} else {

	    /* here: lenpath1==lenpath2, since the case lenpath1<lenpath2 is checked earlier */

	    /* directories are the same here... and earlier tested this is only a subdir when maybethesame==1 */

	    issubdir=1;

	}

    }

    out:

    return issubdir;

}

char *check_path(char *path)
{
    char tmppath[PATH_MAX];
    char *returnpath=NULL;

    if ( realpath(path, tmppath) ) {
	int len=strlen(tmppath)+1;

	returnpath=malloc(len);

	if ( returnpath ) {

	    memset(returnpath, '\0', len);
	    memcpy(returnpath, tmppath, len);

	}

    }

    return returnpath;

}

void convert_to(char *string, int flags)
{
    char *p=string, *q=string;

    if (flags==0) return;

    for (p=string; *p != '\0'; ++p) {

	if ( flags & UTILS_CONVERT_SKIPSPACE ) {

	    if ( isspace(*p)) continue;

	}

	if ( flags & UTILS_CONVERT_TOLOWER ) {

	    *q=tolower(*p);

	} else {

	    *q=*p;

	}

	q++;

    }

    *q='\0';

}


/* a way to check two pids belong to the same process 
    to make this work process_id has to be the main thread of a process
    and thread_id is a process id of some thread of a process
    under linux then the directory
    /proc/<process_id>/task/<thread_id> 
    has to exist

    this does not work when both processes are not mainthreads
    20120426: looking for a better way to do this
*/

unsigned char belongtosameprocess(pid_t process_id, pid_t thread_id)
{
    char tmppath[40];
    unsigned char sameprocess=0;
    struct stat st;

    snprintf(tmppath, 40, "/proc/%i/task/%i", process_id, thread_id);

    if (lstat(tmppath, &st)==0) sameprocess=1;

    return sameprocess;
}

/* function to get the process id (PID) where the TID is given
   this is done by first looking /proc/tid exist
   if this is the case, then the tid is the process id
   if not, check any pid when walking back if this is
   the process id*/

pid_t getprocess_id(pid_t thread_id)
{
    pid_t process_id=thread_id;
    char path[40];
    DIR *dp=NULL;

    snprintf(path, 40, "/proc/%i/task", thread_id);

    dp=opendir(path);

    if (dp) {
	struct dirent *de=NULL;

	/* walk through directory /proc/threadid/task/ and get the lowest number: thats the process id */

	while((de=readdir(dp))) {

	    if (strcmp(de->d_name, ".")==0 || strcmp(de->d_name, "..")==0) continue;

	    thread_id=atoi(de->d_name);

	    if (thread_id>0 && thread_id<process_id) process_id=thread_id;

	}

	closedir(dp);

    }

    out:

    return process_id;

}

int custom_fork()
{
    int nullfd;
    int result=0;
    pid_t pid=fork();

    switch(pid) {

	case -1:

	    // logoutput("custom_fork: error %i:%s forking", errno, strerror(errno));
	    return -1;

	case 0:

	    break;

	default:

	    return (int) pid;

    }

    if (setsid() == -1) {

	// logoutput("custom_fork: error %i:%s setsid", errno, strerror(errno));
	return -1;
    }

    (void) chdir("/");

    nullfd = open("/dev/null", O_RDWR, 0);

    if (nullfd != -1) {

	(void) dup2(nullfd, 0);
	(void) dup2(nullfd, 1);
	(void) dup2(nullfd, 2);

	if (nullfd > 2) close(nullfd);

    }

    return 0;

}
