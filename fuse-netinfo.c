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
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <pthread.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "fuse-netinfo.h"
static struct simple_hash_s hash_netinfo;

static unsigned int calculate_netinfo_hash(uint64_t unique)
{
    return unique % hash_netinfo.len;
}

static unsigned int netinfo_hashfunction(void *data)
{
    struct fuse_ownerwatch_s *watch=(struct fuse_ownerwatch_s *) data;
    return calculate_netinfo_hash(ownerwatch->unique);
}

static struct fuse_ownerwatch_s *get_next_fuse_ownerwatch_netinfo_hash(uint64_t unique, void **index)
{
    unsigned int hashvalue=calculate_netinfo_hash(unique, 0);
    struct fuse_ownerwatch_s *ownerwatch=(struct fuse_ownerwatch_s *) get_next_hashed_value(&hash_netinfo, index, hashvalue);

    while (ownerwatch) {

	if (ownerwatch->ino==unique) break;
	ownerwatch=(struct fuse_ownerwatch_s *) get_next_hashed_value(&hash_netinfo, index, hashvalue);

    }

    return ownerwatch;

}

static struct fuse_ownerwatch_s *lookup_fuse_watch_netinfo_hash_owner(uint64_t unique, struct fs_connection *owner)
{
    unsigned int hashvalue=calculate_netinfo_hash(unique);
    void *index=NULL;
    struct fuse_ownerwatch_s *ownerwatch=(struct fuse_ownerwatch_s *) get_next_hashed_value(&hash_netinfo, &index, hashvalue);

    while(ownerwatch) {

	if (ownerwatch->unique==unique && ownerwatch->owner==owner) break;
	ownerwatch=(struct fuse_ownerwatch_s *) get_next_hashed_value(&hash_netinfo, &index, hashvalue);

    }

    return ownerwatch;

}

void add_watch_netinfo_hash(struct fuse_ownerwatch_s *watch)
{
    add_data_to_hash(&hash_netinfo, (void *) watch);
}

void remove_watch_netinfo_hash(struct fuse_ownerwatch_s *watch)
{
    remove_data_from_hash(&hash_netinfo, (void *) watch);
}

int init_netinfo_hashtable()
{
    unsigned int error=0;
    int result=initialize_group(&hash_netinfo, netinfo_hashfunction, 256, &error);

    if (result<0) {

    	logoutput("init_netinfo_hashtable: error (%i:%s) creating netinfo hash table", *error, strerror(*error));
	goto error;

    }

}

void free_netinfo_hashtable()
{
    free_group(&hash_netinfo, NULL);
}

static struct fuse_ownerwatch_s *get_containing_ownerwatch(struct list_element_s *list)
{
    return (struct fuse_ownerwatch_s *) ( ((char *) list) - offsetof(struct fuse_ownerwatch_s, list));
}

typedef unsigned int (* write_fsnotify_cb)(struct fuse_netevent_s *event, char *pos);

static unsigned int _write_user_cb1(struct fuse_netevent_s *event, char *pos)
{
    return 4+event->info.fsnotify.user.len;
}

static unsigned int _write_user_cb2(struct fuse_netevent_s *event, char *pos)
{
    store_uint32(pos, event->info.fsnotify.user.len);
    memcpy(pos+4, event->info.fsnotify.user.ptr, event->info.fsnotify.user.len);
    return 4+event->info.fsnotify.user.len;
}

static unsigned int _write_host_cb1(struct fuse_netevent_s *event, char *pos)
{
    return 4+event->info.fsnotify.host.len;
}

static unsigned int _write_host_cb2(struct fuse_netevent_s *event, char *pos)
{
    store_uint32(pos, event->info.fsnotify.host.len);
    memcpy(pos+4, event->info.fsnotify.host.ptr, event->info.fsnotify.host.len);
    return 4+event->info.fsnotify.host.len;
}

static unsigned int _write_file_cb1(struct fuse_netevent_s *event, char *pos)
{
    return 4+event->info.fsnotify.file.len;
}

static unsigned int _write_file_cb2(struct fuse_netevent_s *event, char *pos)
{
    store_uint32(pos, event->info.fsnotify.file.len);
    memcpy(pos+4, event->info.fsnotify.file.ptr, event->info.fsnotify.file.len);
    return 4+event->info.fsnotify.file.len;
}

static write_fsnotify_cb write_info_a[][3] = {
	{_write_zero, _write_user_cb1, _write_user_cb2},
	{_write_zero, _write_host_cb1, _write_host_cb2},
	{_write_zero, _write_file_cb1, _write_file_cb2}};

/*
    fsnotify message to userspace client looks like:
    - valid uint32					4
    - unique id uint64					8
    - mask uint32					4
    - who string					4 + lenname
    - host string					4 + lenname
    - file string					4 + lenname
*/

unsigned int get_datalen_fsnotify_event(struct fuse_netevent_s *event)
{
    unsigned int len=0;

    /* calculate the maximum length */

    len+=4; /* valid */
    len+=8; /* unique id per watch */
    len+=4; /* fsnotify mask */
    len+=4 + event->info.fsnotify.user.len; /* who */
    len+=4 + event->info.fsnotify.host.len; /* host */
    len+=4 + event->info.fsnotify.file.len; /* filename */

    return len;
}

/* send fs event to owner direct to userspace cause there is a connection with this client via a socket */

static signed char send_fsnotify_event_userspace(struct fuse_ownerwatch_s *ownerwatch, struct fuse_netevent_s *event)
{
    struct fs_connection_s *client=ownerwatch->owner;
    uint32_t mask = event->info.fsnotify.mask & ownerwatch->mask;

    if (mask==0) return;

    if (client->fd>0) {
	unsigned int len=get_datalen_fsnotify_event(event);
	unsigned int valid=event->info.fsnotify.valid;
	char data[len];
	unsigned int pos=0;
	ssize_t size=0;

	/* send only this fields available and those the client wants */

	valid = event->info.fsnotify.valid & ownerwatch->valid;

	store_uint32(&data[pos], valid);
	pos+=4;
	store_uint64(&data[pos], event->info.fsnotify.unique);
	pos+=8;
	store_uint32(&data[pos], mask);
	pos+=4;

	/* write the various fields */

	pos+=(* write_info_a[0][(valid & FUSE_NETINFO_VALID_USER) >> FUSE_NETINFO_INDEX_USER])(event, &data[pos]);
	pos+=(* write_info_a[1][(valid & FUSE_NETINFO_VALID_HOST) >> FUSE_NETINFO_INDEX_HOST])(event, &data[pos]);
	pos+=(* write_info_a[2][(valid & FUSE_NETINFO_VALID_FILE) >> FUSE_NETINFO_INDEX_FILE])(event, &data[pos]);

	size=write(client->fd, data, pos);

	if (size==-1) {

	    logoutput("send_fsnotify_event_userspace: error %i sending %i bytes", errno, strerror(errno));
	    return -1;

	}

    } else {

	logoutput("send_fsnotify_event_userspace: client not connected");
	return -1;

    }

    return 0;

}

static signed char _send_fsnotify_event_vfs(struct workspace_mount_s *workspace, struct fuse_netevent_s *event)
{
    struct context_interface_s *interface=&workspace->context.interface;
    unsigned int valid=event->info.fsnotify.valid;

    /* check already send to VFS */
    if (event->info.fsevent.flags&FUSE_FSNOTIFY_FLAG_VFS) return;

    if (valid & FUSE_NETINFO_VALID_FILE) {
	struct name_s xname;

	xname.name=event->info.fsnotify.file.ptr;
	xname.len=event->info.fsnotify.file.len;
	xname.index=0;

	notify_VFS_fsnotify_child(interface->ptr, event->info.fsnotify.unique, event->info.fsnotify.mask, &xname);

    } else {

	notify_VFS_fsnotify_child(interface->ptr, event->info.fsnotify.unique, event->info.fsnotify.mask);

    }

    event->info.fsevent.flags|=FUSE_FSNOTIFY_FLAG_VFS;
    return 0;

}

static signed char send_fsnotify_event_vfs(struct fuse_ownerwatch_s *ownerwatch, struct fuse_netevent_s *event)
{
    struct workspace_mount_s *workspace=ownerwatch->watch->workspace;
    return _send_fsnotify_event_vfs(workspace, event);
}

static struct fuse_ownerwatch_s *_create_fuse_ownerwatch(struct fs_connection_s *owner, uint64_t unique, struct fuse_watch_s *watch, uint32_t mask, unsigned int valid)
{
    struct fuse_ownerwatch_s *ownerwatch=NULL;

    ownerwatch=malloc(sizeof(struct fuse_ownerwatch_s));

    if (ownerwatch) {

	memset(ownerwatch, 0, sizeof(struct fuse_ownerwatch_s));

	ownerwatch->owner=owner;
	ownerwatch->unique=unique;
	ownerwatch->watch=watch;
	ownerwatch->mask=mask;
	ownerwatch->valid=valid;
	ownerwatch->flags=0;

	if (owner) {

	    ownerwatch->report_event=send_fsnotify_event_userspace;
	    watch->flags|=FUSE_FSNOTIFY_FLAG_USERSPACE;

	} else {

	    ownerwatch->report_event=send_fsnotify_event_vfs;
	    watch->flags|=FUSE_FSNOTIFY_FLAG_VFS;

	}

	watch->refount++;

    }

    return ownerwatch;

}

uint64_t create_fuse_ownerwatch(struct workspace_mount_s *workspace, uint64_t unique, uint32_t mask, unsigned int valid, struct fs_connection_s *owner)
{
    struct fuse_ownerwatch_s *ownerwatch=NULL;
    uint64_t result=0;
    void *index=NULL;

    if (mask==0) return NULL;
    valid=valid & (FUSE_NETINFO_VALID_USER | FUSE_NETINFO_VALID_HOST);

    writelock_hashtable(&hash_netinfo);

    ownerwatch=get_next_fuse_watch_netinfo_hash(unique, &index);

    if (ownerwatch) {
	struct fuse_watch_s *watch=ownerwatch->watch;
	uint32_t old=watch->mask;

	/* there is already a watch for this unique, same owner? */

	while (ownerwatch) {

	    if (ownerwatch->owner==owner) {

		ownerwatch->mask=mask;
		ownerwatch->valid=valid;
		unlock_hashtable(&hash_netinfo);
		return unique;

	    }

	    ownerwatch=get_next_fuse_watch_netinfo_hash(unique, &index);

	}

	ownerwatch=_create_fuse_ownerwatch(owner, unique, watch, mask, owner);
	if (ownerwatch) result=unique;
	watch->mask|=mask;
	if (watch->mask!=old) {

	    /* correct existing watch */

	}

    } else {

	/* no other watch found */

	watch=malloc(sizeof(struct fuse_watch_s));

	if (watch) {

	    watch->workspace=workspace;
	    watch->mask=mask;
	    watch->flags=0;
	    watch->refcount=0;

	} else {

	    unlock_hashtable(&hash_netinfo);
	    return 0;

	}

	ownerwatch=create_fuse_ownerwatch(owner, unique, watch, mask, owner);

	if (ownerwatch) {

	    add_data_to_hash(&hash_netinfo, (void *) ownerwatch);
	    result=unique;

	    /* set new watch on backend */

	} else {

	    free(watch);
	    unlock_hashtable(&hash_netinfo);
	    return 0;

	}

    }

    unlock_hashtable(&hash_netinfo);
    return result;

}

/* send event to all owners using this watch via userspace and/or vfs */

signed char send_fuse_fsnotify_event(uint64_t unique, struct fuse_netevent_s *event)
{
    struct fuse_ownerwatch_s *ownerwatch=NULL;
    void *index=NULL;

    readlock_hashtable(&hash_netinfo);

    ownerwatch=get_next_fuse_ownerwatch_netinfo_hash(unique, &index);

    if (ownerwatch) {
	struct fuse_watch_s *watch=ownerwatch->watch;

	if (watch->flags & FUSE_FSNOTIFY_FLAG_USERSPACE) {

	    while (ownerwatch) {

		(* ownerwatch->report_event)(ownerwatch, event);
		ownerwatch=get_next_fuse_ownerwatch_netinfo_hash(unique, &index);

	    }

	} else {

	    _send_fsnotify_event_vfs(watch->workspace, event);

	}

    }

    unlock_hashtable(&hash_netinfo);
    return 0;

}

void remove_fuse_fsnotify_ownerwatch(struct fs_connection_s *owner, uint64_t unique)
{
    struct fuse_ownerwatch_s *ownerwatch=NULL;
    struct fuse_watch_s *watch=NULL;
    void *index=NULL;
    uint32_t mask=0;

    writelock_hashtable(&hash_netinfo);

    ownerwatch=get_next_fuse_ownerwatch_netinfo_hash(unique, &index);

    while (ownerwatch) {

	if (ownerwatch->owner==owner) {

	    watch=ownerwatch->watch;
	    remove_data_from_hash(&hash_netinfo, (void *) ownerwatch);
	    free(ownerwatch);

	    watch->refcount--;
	    if (watch->refcount==0) {

		free(watch);
		watch=NULL;
		break;

	    }

	}
	mask|=ownerwatch->mask;
	ownerwatch=get_next_fuse_ownerwatch_netinfo_hash(unique, &index);

    }

    if (watch && watch->mask!=mask) {

	/* modify existing watch */

	

    }

    unlock_hashtable(&hash_netinfo);

}
