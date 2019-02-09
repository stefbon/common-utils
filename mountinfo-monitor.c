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

#undef LOGGING

#include "logging.h"
#include "simple-list.h"
#include "beventloop.h"
#include "workerthreads.h"

#include "mountinfo.h"
#include "mountinfo-monitor.h"
#include "mountinfo-utils.h"

#include "utils.h"

#define MOUNT_MONITOR_FLAG_INIT			1

struct mountinfo_monitor_s {
    int 					(*update) (unsigned long generation_id, struct mountentry_s *(*next) (void **index, unsigned long g, unsigned char type));
    unsigned char 				(*ignore) (char *source, char *fs, char *path);
    struct bevent_xdata_s 			*xdata;
    pthread_mutex_t				mutex;
    pthread_t					threadid;
    unsigned char				changed;
    char					*buffer;
    unsigned int				size;
    void 					*threadsqueue;
    unsigned int				flags;
};

/* private structs for internal use only */

struct mountinfo_s {
    int 					mountid;
    int 					parentid;
    unsigned char 				added;
    struct list_element_s			list;
    struct mountentry_s 			*mountentry;
};

/* struct when reading the mountinfo file:
    mountinfo is a list of the lines as read from mountinfo
    mountentry is the same list but then sorted
*/

struct mountinfo_list_s {
    struct list_header_s			mountinfo;
    struct list_header_s			mountentry;
};

/* these procedures maintain the current mounts,
    and run a specific cb for every mount added or removed */

/* added, removed and keep mount entries */

static struct list_header_s removed_mounts;
static struct list_header_s current_mounts;
static struct mountinfo_monitor_s mount_monitor;
static int process_mountinfo_event(int fd, void *data, uint32_t events);

struct mountinfo_s *get_containing_mountinfo(struct list_element_s *list)
{
    if (! list) return NULL;
    return (struct mountinfo_s *) ( ((char *) list) - offsetof(struct mountinfo_s, list));
}

void free_mountentry(void *data)
{
    struct mountentry_s *mountentry=(struct mountentry_s *) data;

    if (mountentry) {

	if (mountentry->mountpoint) free(mountentry->mountpoint);
	if (mountentry->fs) free(mountentry->fs);
	if (mountentry->source) free(mountentry->source);
	if (mountentry->options) free(mountentry->options);
	if (mountentry->rootpath) free(mountentry->rootpath);

	free(mountentry);

    }

}

/*dummy callbacks*/

static unsigned char dummy_ignore(char *source, char *fs, char *path)
{
    return 0;
}

static int dummy_update(unsigned long generation, struct mountentry_s *(*next) (void **index, unsigned long generation, unsigned char type))
{
    return 1;
}

int open_mountmonitor(struct bevent_xdata_s *xdata, unsigned int *error)
{

    if (! xdata) {

	*error=EINVAL;
	return -1;

    }

    init_list_header(&current_mounts, SIMPLE_LIST_TYPE_EMPTY, NULL);
    init_list_header(&removed_mounts, SIMPLE_LIST_TYPE_EMPTY, NULL);

    mount_monitor.update=dummy_update;
    mount_monitor.ignore=dummy_ignore;
    mount_monitor.xdata=xdata;
    pthread_mutex_init(&mount_monitor.mutex, NULL);
    mount_monitor.changed=0;
    mount_monitor.threadid=0;
    mount_monitor.buffer=NULL;
    mount_monitor.size=4096;
    mount_monitor.threadsqueue=NULL;
    mount_monitor.flags=0;

    xdata->fd=open(MOUNTINFO_FILE, O_RDONLY);

    if (xdata->fd==-1) {

    	logoutput_error("open_mountmonitor: unable to open file %s", MOUNTINFO_FILE);
    	*error=errno;
	goto error;

    }

    logoutput("open_mountmonitor: fd %i", xdata->fd);
    xdata->callback=process_mountinfo_event;
    return 0;

    error:

    if (xdata->fd>0) {

	close(xdata->fd);
	xdata->fd=0;

    }

    return -1;

}

void set_updatefunc_mountmonitor(update_cb_t cb)
{
    if (cb) mount_monitor.update=cb;
}

void set_ignorefunc_mountmonitor(ignore_cb_t cb)
{
    if (cb) mount_monitor.ignore=cb;
}

void set_threadsqueue_mountmonitor(void *ptr)
{
    if (ptr) mount_monitor.threadsqueue=ptr;
}

void close_mountmonitor()
{
    if (mount_monitor.xdata) {

	if (mount_monitor.xdata->fd>0) {

	    close(mount_monitor.xdata->fd);
 	    mount_monitor.xdata->fd=0;

	}

    }

    if (mount_monitor.buffer) {

	free(mount_monitor.buffer);
	mount_monitor.buffer=NULL;

    }

}

/* walk through the current mounts to get the added */

struct mountentry_s *get_next_mountentry_added(void **pindex, unsigned long generation)
{
    void *index=*pindex;
    struct mountentry_s *entry=NULL;

    if (index) {

	entry=(struct mountentry_s *) index;
	entry=get_containing_mountentry(get_next_element(&entry->list));

    } else {

	entry=get_containing_mountentry(current_mounts.head);

    }

    /* only take those with newer generation */

    while (entry) {

	if (entry->generation>=generation) break;
	entry=get_containing_mountentry(get_next_element(&entry->list));

    }

    index=(void *) entry;
    *pindex=index;

    return entry;

}

static void add_mount_removed(struct mountentry_s *mountentry)
{
    add_list_element_last(&removed_mounts, &mountentry->list);
}

static void clear_removed_mounts()
{
    struct list_element_s *list=get_list_head(&removed_mounts, SIMPLE_LIST_FLAG_REMOVE);

    while (list) {

	struct mountentry_s *entry=get_containing_mountentry(list);
	free_mountentry(entry);
	list=get_list_head(&removed_mounts, SIMPLE_LIST_FLAG_REMOVE);

    }

}

struct mountentry_s *get_next_mountentry_removed(void **pindex)
{
    struct mountentry_s *entry=NULL;
    void *index=*pindex;

    if (index) {

	entry=(struct mountentry_s *) index;
	entry=get_containing_mountentry(get_next_element(&entry->list));

    } else {

	entry=get_containing_mountentry(removed_mounts.head);

    }

    index=(void *) entry;
    *pindex=index;

    return entry;
}

struct mountentry_s *get_next_mountentry_current(void **pindex)
{
    struct mountentry_s *entry=NULL;
    void *index=*pindex;

    if (index) {

	entry=(struct mountentry_s *) index;
	entry=get_containing_mountentry(get_next_element(&entry->list));

    } else {

	entry=get_containing_mountentry(current_mounts.head);

    }

    index=(void *) entry;
    *pindex=index;

    return entry;

}

struct mountentry_s *get_next_mountentry(void **index, unsigned long generation, unsigned char type)
{

    if (type==MOUNTLIST_CURRENT) {

	return get_next_mountentry_current(index);

    } else if (type==MOUNTLIST_ADDED) {

	return get_next_mountentry_added(index, generation);

    } else if (type==MOUNTLIST_REMOVED) {

	return get_next_mountentry_removed(index);

    }

    return NULL;

}

static void read_mountinfo_values(char *buffer, unsigned int size, struct mountinfo_list_s *list)
{
    int mountid=0;
    int parentid=0;
    int major=0;
    int minor=0;
    char *mountpoint=NULL;
    char *root=NULL;
    char *source=NULL;
    char *options=NULL;
    char *fs=NULL;
    char *sep=NULL;
    char *pos=NULL;
    struct mountentry_s *mountentry=NULL;
    struct mountinfo_s *mountinfo=NULL;
    int left=size;
    int error=0;

    // logoutput("read_mountinfo_values");

    pos=buffer;

    if (sscanf(pos, "%i %i %i:%i", &mountid, &parentid, &major, &minor) != 4) {

        logoutput_error("read_mountinfo_values: error sscanf, reading %s", pos);
	goto dofree;

    }

    /* root */

    sep=memchr(pos, '/', left);
    if (! sep) goto dofree;
    left-=(unsigned int) (sep-pos);
    pos=sep;

    sep=memchr(pos, ' ', left);
    if (! sep) goto dofree;

    /* unescape */

    *sep='\0';
    root=g_strcompress(pos);
    if (! root) goto dofree;
    *sep=' ';

    left-=(unsigned int) (sep-pos);
    pos=sep;

    /* mountpoint */

    sep=memchr(pos, '/', left);
    if (! sep) goto dofree;
    left-=(unsigned int) (sep-pos);
    pos=sep;

    sep=memchr(pos, ' ', left);
    if (! sep) goto dofree;

    /* unescape */

    *sep='\0';
    mountpoint=g_strcompress(pos);
    if (! mountpoint) goto dofree;
    *sep=' ';

    left-=(unsigned int) (sep-pos);
    pos=sep;

    /* skip rest here, and start at the seperator - */

    sep=strstr(pos, " - ");
    if ( ! sep ) goto dofree;
    sep+=3;

    left-=(unsigned int) (sep-pos);
    pos=sep;

    /* filesystem */

    sep=memchr(pos, ' ', left);
    if ( ! sep ) goto dofree;

    *sep='\0';
    fs=g_strcompress(pos);
    if (! fs) goto dofree;
    *sep=' ';
    sep++;

    left-=(unsigned int) (sep-pos);
    pos=sep;

    /* source */

    sep=memchr(pos, ' ', left);

    if (!sep) {

	sep=buffer + size - 1;

    } else {

	*sep='\0';

    }

    if (strcmp(pos, "/dev/root")==0) {

	source=get_real_root_device(major, minor);

    } else {

	source=g_strcompress(pos);

    }

    if (! source) goto dofree;
    if ( (*mount_monitor.ignore) (source, fs, mountpoint)==1) goto dofree;

    left-=(unsigned int) (sep-pos);

    /* options */

    if (left>1) {

	*sep=' ';
	pos=sep+1;
	left--;

	sep=memchr(pos, ' ', left);

	if (sep) {

	    *sep='\0';
	    options=g_strcompress(pos);
	    *sep=' ';

	} else {

	    options=g_strcompress(pos);

	}

	if (! options) goto dofree;

    }

    /* get a new mountinfo */

    logoutput("read_mountinfo_values: found %s at %s with %s", source, mountpoint, fs);

    mountinfo=malloc(sizeof(struct mountinfo_s));
    mountentry=malloc(sizeof(struct mountentry_s));

    if ( ! mountinfo || ! mountentry) goto dofree;

    mountinfo->mountid=mountid;
    mountinfo->parentid=parentid;
    mountinfo->added=0;
    init_list_element(&mountinfo->list, NULL);

    add_list_element_last(&list->mountinfo, &mountinfo->list);

    mountentry->unique=0;
    mountentry->generation=0;
    mountentry->flags=0;
    mountentry->mountpoint=mountpoint;
    mountentry->rootpath=root;
    mountentry->fs=fs;
    mountentry->options=options;
    mountentry->source=source;
    init_list_element(&mountentry->list, NULL);

    mountinfo->mountentry=mountentry;
    mountentry->index=(void *) mountinfo;

    mountentry->major=major;
    mountentry->minor=minor;
    mountentry->data=NULL;

    if (strcmp(fs, "autofs")==0) {

        if (strstr(options, "indirect")) {

	    mountentry->flags|=MOUNTENTRY_FLAG_AUTOFS_INDIRECT;

	} else {

	    mountentry->flags|=MOUNTENTRY_FLAG_AUTOFS_DIRECT;

	}

    }

    if (list->mountentry.head==NULL) {

	list->mountentry.head=&mountentry->list;
	list->mountentry.tail=&mountentry->list;

    } else {
	struct mountentry_s *prev=get_containing_mountentry(list->mountentry.tail);
	int diff=0;

	/* walk back starting at the last and compare */

        while(1) {

            diff=compare_mount_entries(prev, mountentry);

    	    if (diff>0) {

		struct list_element_s *p=get_prev_element(&prev->list);

        	if (p) {

            	    prev=get_containing_mountentry(p);

                } else {

                    /* there is no prev: at the first */
		    break;

                }

            } else {

                /* not bigger */
                break;

            }

        }

        if ( diff>0 ) {

	    add_list_element_first(&list->mountentry, &mountentry->list);

        } else {

	    add_list_element_after(&list->mountentry, &prev->list, &mountentry->list);

        }

    }

    return;

    dofree:

    if (mountpoint) {

	free(mountpoint);
	mountpoint=NULL;

    }

    if (root) {

	free(root);
	root=NULL;

    }

    if (fs) {

	free(fs);
	fs=NULL;

    }

    if (source) {

	free(source);
	source=NULL;

    }

    if (options) {

	free(options);
	options=NULL;

    }

    if (mountinfo) {

	free(mountinfo);
	mountinfo=NULL;

    }

    if (mountentry) {

	free(mountentry);
	mountentry=NULL;

    }

}

static int get_mountlist(struct mountinfo_list_s *list)
{
    char *pos=NULL;
    char *sep=NULL;
    int error=0;
    int bytesread=0;

    if (! mount_monitor.xdata) {

	return -EIO;

    } else if (mount_monitor.xdata->fd<=0) {

	return -EIO;

    }

    if ( ! mount_monitor.buffer) {

	mount_monitor.buffer=malloc(mount_monitor.size);
	if (! mount_monitor.buffer) return -ENOMEM;

    }

    readmountinfo:

    memset(mount_monitor.buffer, '\0', mount_monitor.size);
    bytesread=pread(mount_monitor.xdata->fd, mount_monitor.buffer, mount_monitor.size, 0);

    if (bytesread<0) {

	/* error */

	logoutput_error("get_mountlist: error %i reading mountinfo", errno);
	return -errno;

    } else if (bytesread>=mount_monitor.size) {

	/* buffer is too small */

	mount_monitor.size+=2048;
	mount_monitor.buffer=realloc(mount_monitor.buffer, mount_monitor.size);

	if (mount_monitor.buffer) {

	    goto readmountinfo;

	} else {

	    return -ENOMEM;

	}

    }

    pos=mount_monitor.buffer;

    while (pos<mount_monitor.buffer+bytesread) {

	sep=memchr(pos, 10, bytesread - (unsigned int)(pos - mount_monitor.buffer));

	if (! sep) {

	    break;

	} else if ((unsigned int)(sep-pos)<15) {

	    /* 15 is the bare minimum for a valid line containing mountinfo */
	    error=EIO;
	    break;

	}

	*sep='\0';
	read_mountinfo_values(pos, (unsigned int)(sep-pos), list);
	pos=sep+1;

    }

    out:

    return (error>0) ? -1 : 0;

}

void handle_change_mounttable(unsigned char init)
{
    struct mountinfo_list_s new_list;
    struct mountinfo_s *mountinfo=NULL;
    unsigned long generation=0;
    unsigned int error=0;
    unsigned int count_added=0;

    logoutput("handle_change_mounttable");

    init_list_header(&new_list.mountinfo, SIMPLE_LIST_TYPE_EMPTY, NULL);
    init_list_header(&new_list.mountentry, SIMPLE_LIST_TYPE_EMPTY, NULL);

    lock_mountlist("write", &error);

    generation=generation_id();
    increase_generation_id();

    /* get a new list and compare */

    if (get_mountlist(&new_list)==0) {
	struct list_element_s *list_c=NULL;
	struct list_element_s *list_n=NULL;
	struct mountentry_s *entry_c=NULL;
	struct mountentry_s *entry_n=NULL;
	int diff=0;

	list_c=get_list_head(&current_mounts, 0);
	list_n=get_list_head(&new_list.mountentry, 0);

        entry_c=get_containing_mountentry(list_c);
        entry_n=get_containing_mountentry(list_n);

        while (entry_c && entry_n) {

            if (entry_n) {

		if (entry_c) {

            	    diff=compare_mount_entries(entry_c, entry_n);

        	} else {

            	    diff=1;

		}

            } else {

		if (entry_c) {

            	    diff=-1;

        	} else {

		    /* no more entries on both lists */

            	    break;

        	}

	    }

            if (diff==0) {
		struct list_element_s *next=get_next_element(list_n);
		struct mountinfo_s *mountinfo=(struct mountinfo_s *) entry_n->index;

		/* the same, step on both lists */

		free_mountentry((void *) entry_n);

		mountinfo->mountentry=entry_c;
		mountinfo->added=0;

		list_c=get_next_element(list_c);
		list_n=next;

            } else if ( diff<0 ) {
		struct list_element_s *next=get_next_element(list_c);

                /* current is smaller: move to removed list */

		entry_c->index=NULL;
		remove_list_element(list_c);
                add_list_element_last(&removed_mounts, list_c);

                list_c=next;

            } else { /* diff>0 */
		struct list_element_s *next=get_next_element(list_n);

                /* new is "smaller" then current : added */

		remove_list_element(list_n);
		init_list_element(list_n, NULL);

		if (list_c) {

		    add_list_element_before(&current_mounts, list_c, list_n);

		} else {

		    add_list_element_last(&current_mounts, list_n);

		}

		entry_n->unique=get_uniquectr();
		entry_n->generation=generation;

		mountinfo=(struct mountinfo_s *) entry_n->index;
		mountinfo->added=1;

		list_n=next;

            }

        }

    }

    /* set the parents: only required for the new ones
    test it's mounted by autofs
    and set the unique ctr */

    mountinfo=get_containing_mountinfo(new_list.mountinfo.head);

    while (mountinfo) {

	if (mountinfo->mountentry) {
	    struct mountentry_s *entry=mountinfo->mountentry;

	    if (entry->generation==generation) {

		logoutput("handle_change_mounttable: added %s", entry->mountpoint);
		check_mounted_by_autofs(entry);
		count_added++;

	    }

	    entry->index=NULL;

	}

	new_list.mountinfo.head=get_next_element(&mountinfo->list);
	free(mountinfo);

	if (new_list.mountinfo.head) {

	    mountinfo=get_containing_mountinfo(new_list.mountinfo.head);

	} else {

	    break;

	}

    }

    if (init==0 && (count_added>0 || removed_mounts.count>0)) {

	logoutput("handle_change_mounttable: run the cb");

	if ((*mount_monitor.update) (generation, get_next_mountentry)==1) {

	    clear_removed_mounts();

	}

    }

    unlock_mountlist("write", &error);

}

static void thread_process_mountinfo(void *ptr)
{

    logoutput("thread_process_mountinfo");

    pthread_mutex_lock(&mount_monitor.mutex);

    process:

    mount_monitor.changed=0;
    mount_monitor.threadid=pthread_self();
    pthread_mutex_unlock(&mount_monitor.mutex);

    handle_change_mounttable(! (mount_monitor.flags & MOUNT_MONITOR_FLAG_INIT));

    pthread_mutex_lock(&mount_monitor.mutex);
    if (mount_monitor.changed==1) goto process;
    mount_monitor.threadid=0;
    pthread_mutex_unlock(&mount_monitor.mutex);

}

/* process an event the mountinfo */

static int process_mountinfo_event(int fd, void *data, uint32_t events)
{

    logoutput("process_mountinfo_event");

    pthread_mutex_lock(&mount_monitor.mutex);

    if (mount_monitor.threadid>0) {

	/* there is already a thread processing */
	mount_monitor.changed=1;

    } else if ((mount_monitor.flags & MOUNT_MONITOR_FLAG_INIT)==0) {

	handle_change_mounttable(1);
	mount_monitor.flags|=MOUNT_MONITOR_FLAG_INIT;

    } else {
	unsigned int error=0;

	/* get a thread to do the work */

	logoutput("process_mountinfo_event: start thread");

	work_workerthread(NULL, 0, thread_process_mountinfo, NULL, &error);
	if (error>0) logoutput("process_mountinfo_event: error %i:%s starting thread", error, strerror(error));
	logoutput("process_mountinfo_event: thread started");

    }

    pthread_mutex_unlock(&mount_monitor.mutex);

    out:

    return 0;

}
