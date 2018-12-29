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
#include <sys/fsuid.h>

#include <pthread.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "workerthreads.h"
#include "pathinfo.h"
#include "utils.h"
#include "fuse-dentry.h"
#include "fuse-directory.h"
#include "fuse-utils.h"
#include "beventloop.h"
#include "beventloop-xdata.h"
#include "fuse-interface.h"
#include "fuse-fs.h"
#include "workspaces.h"
#include "simple-hash.h"

#include "logging.h"

static dev_t unique=0;
static struct simple_hash_s context_hash = {
    .len					= 128,
    .hash					= NULL,
};

static unsigned int calculate_context_hash(struct service_context_s *c)
{
    return c->unique % context_hash.len;
}

static unsigned int context_hash_function(void *data)
{
    struct service_context_s *c=(struct service_context_s *) data;
    return calculate_context_hash(c);
}

int initialize_context_hashtable()
{
    unsigned int error=0;
    return initialize_group(&context_hash, context_hash_function, 128, &error);
}

void free_service_context(struct service_context_s *context);

static void _free_service_context(void *d)
{
    free_service_context((struct service_context_s *) d);
}

void free_context_hashtable()
{
    free_group(&context_hash, _free_service_context);
}

struct service_context_s *search_service_context(dev_t unique)
{
    struct simple_lock_s lock;
    struct service_context_s *c=NULL;
    unsigned int hashvalue=unique % context_hash.len;
    void *index=NULL;

    init_rlock_hashtable(&context_hash, &lock);
    lock_hashtable(&lock);

    c=(struct service_context_s *) get_next_hashed_value(&context_hash, &index, hashvalue);

    while (c) {

	if (c->unique==unique) break;
	c=(struct service_context_s *) get_next_hashed_value(&context_hash, &index, hashvalue);

    }

    unlock_hashtable(&lock);

    return c;
}

void add_service_context_hash(struct service_context_s *c)
{
    c->unique=unique;
    unique++;
    add_data_to_hash(&context_hash, (void *) c);
}
void remove_service_context_hash(struct service_context_s *c)
{
    remove_data_from_hash(&context_hash, (void *) c);
}
void init_rlock_service_context_hash(struct simple_lock_s *l)
{
    init_rlock_hashtable(&context_hash, l);
}
void init_wlock_service_context_hash(struct simple_lock_s *l)
{
    init_wlock_hashtable(&context_hash, l);
}
int lock_service_context_hash(struct simple_lock_s *l)
{
    return lock_hashtable(l);
}
int unlock_service_context_hash(struct simple_lock_s *l)
{
    return unlock_hashtable(l);
}
struct service_context_s *get_service_context(struct context_interface_s *interface)
{
    return (struct service_context_s *) ( ((char *) interface) - offsetof(struct service_context_s, interface));
}

struct bevent_xdata_s *add_context_eventloop(struct context_interface_s *interface, unsigned int fd, int (*read_incoming_data)(int fd, void *ptr, uint32_t events), void *ptr, const char *name, unsigned int *error)
{
    struct service_context_s *context=get_service_context(interface);
    struct workspace_mount_s *workspace=context->workspace;
    struct service_context_s *root_context=workspace->context;
    struct bevent_xdata_s *xdata=NULL;

    xdata=add_to_beventloop(fd, EPOLLIN, read_incoming_data, ptr, &context->xdata, root_context->xdata.loop);

    if (xdata) {

	if (name) set_bevent_name(xdata, (char *) name, error);

    } else {

	*error=EIO;

    }

    return xdata;

}

void free_service_context(struct service_context_s *context)
{
    struct simple_lock_s wlock;

    logoutput("free_service_context: free context");

    if (context->xdata.fd>0) {
	unsigned int fd=context->xdata.fd;

	logoutput("free_service_context: remove xdata fd %i", fd);

	remove_xdata_from_beventloop(&context->xdata);
	close(fd);

    }

    init_wlock_service_context_hash(&wlock);
    lock_service_context_hash(&wlock);
    remove_service_context_hash(context);
    unlock_service_context_hash(&wlock);

    free(context);

}

static void *connect_dummy(uid_t uid, struct context_interface_s *interface, struct context_address_s *address, unsigned int *error)
{
    return NULL;
}

static int start_dummy(struct context_interface_s *interface, void *data)
{
    return -1;
}

static void dummy_call(struct context_interface_s *interface)
{
}

static struct context_interface_s *get_parent_dummy(struct context_interface_s *interface)
{
    return NULL;
}

static unsigned int get_interface_option_dummy(struct context_interface_s *interface, const char *name, struct context_option_s *option)
{
    return 0;
}

static unsigned int get_interface_info_dummy(struct context_interface_s *interface, const char *what, void *data, struct common_buffer_s *buffer)
{
    return 0;
}

void init_service_context(struct service_context_s *context, unsigned char type, char *name)
{
    memset(context, 0, sizeof(struct service_context_s));

    context->flags=0;
    context->type=0;
    context->unique=0;
    memset(context->name, '\0', sizeof(context->name));
    if (name) strncpy(context->name, name, sizeof(context->name));
    context->fscount=0;
    context->serviceid=0;
    context->fs=NULL;
    context->inode=NULL;
    context->workspace=NULL;
    init_xdata(&context->xdata);
    context->error=0;
    context->refcount=0;
    init_list_element(&context->list, NULL);
    context->parent=NULL;

    context->interface.ptr=NULL;
    context->interface.data=NULL;
    context->interface.connect=connect_dummy;
    context->interface.start=start_dummy;
    context->interface.disconnect=dummy_call;
    context->interface.free=dummy_call;
    context->interface.get_parent=get_parent_dummy;
    context->interface.add_context_eventloop=add_context_eventloop;
    context->interface.get_interface_option=get_interface_option_dummy;
    context->interface.get_interface_info=get_interface_info_dummy;
}

struct service_context_s *create_service_context(struct workspace_mount_s *workspace, unsigned char type)
{
    struct service_context_s *context=NULL;

    logoutput("create_service_context");

    context=malloc(sizeof(struct service_context_s));

    if (context) {
	struct simple_lock_s wlock;

	init_service_context(context, type, NULL);
	context->workspace=workspace;
	if (workspace) add_list_element_last(&workspace->contexes, &context->list);

	init_wlock_service_context_hash(&wlock);
	lock_service_context_hash(&wlock);
	add_service_context_hash(context);
	unlock_service_context_hash(&wlock);

    }

    return context;

}

struct service_context_s *get_next_service_context(struct service_context_s *context, const char *name)
{
    struct bevent_xdata_s *xdata=(context) ? &context->xdata : NULL;
    struct beventloop_s *loop=NULL;
    unsigned int error=0;

    logoutput("get_next_service_context");

    if (xdata) loop=xdata->loop;
    xdata=get_next_xdata(loop, xdata);
    if (name==NULL) goto out;

    while (xdata) {

	error=0;
	if (strcmp_bevent(xdata, (char *)name, &error)==0) break;
	xdata=get_next_xdata(loop, xdata);

    }

    out:

    return (xdata) ? (struct service_context_s *) ( ((char *) xdata) - offsetof(struct service_context_s, xdata)) : NULL;

}

void *get_root_ptr_context(struct service_context_s *context)
{
    struct workspace_mount_s *workspace=context->workspace;
    struct service_context_s *root_context=workspace->context;
    return root_context->interface.ptr;
}

struct service_context_s *get_root_context(struct service_context_s *context)
{
    struct workspace_mount_s *workspace=context->workspace;
    return workspace->context;
}

struct fuse_user_s *get_user_context(struct service_context_s *context)
{
    struct workspace_mount_s *workspace=context->workspace;
    return workspace->user;
}

struct beventloop_s *get_beventloop_ctx(void *ctx)
{
    struct service_context_s *context=(struct service_context_s *) ctx;
    struct workspace_mount_s *workspace=context->workspace;
    struct service_context_s *root_context=workspace->context;
    return root_context->xdata.loop;
}

void add_inode_context(struct service_context_s *context, struct inode_s *inode)
{
    struct entry_s *parent=inode->alias->parent;
    struct inode_s *pinode=parent->inode;
    uint64_t ino=(pinode) ? pinode->st.st_ino : 0;

    add_inode_hashtable(inode, increase_inodes_workspace, (void *) context->workspace);
    logoutput("add_inode_context: parent inode %li", ino);
    (* pinode->fs->type.dir.use_fs)(context, inode);
    inode->st.st_dev=context->unique;

}

struct service_context_s *get_container_context(struct list_element_s *list)
{
    return (list) ? (struct service_context_s *) ( ((char *) list) - offsetof(struct service_context_s, list)) : NULL;
}

void translate_context_host_address(struct host_address_s *host, char **target)
{
    if (target) {

	if (strlen(host->hostname)>0) {

	    *target=host->hostname;

	} else {

	    if (host->ip.type==IP_ADDRESS_TYPE_IPv4) {

		*target=host->ip.ip.v4;

	    } else if (host->ip.type==IP_ADDRESS_TYPE_IPv6) {

		*target=host->ip.ip.v6;

	    }

	}

    }
}

void translate_context_network_port(struct service_address_s *service, unsigned int *port)
{
    if (port) *port=service->target.port.port;
}

void translate_context_address_network(struct context_address_s *address, char **target, unsigned int *port)
{
    translate_context_host_address(&address->network.target.host, target);
    translate_context_network_port(&address->service, port);
}
