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
static struct simple_hash_s context_hash;

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
struct service_context_s *get_next_service_context(void **index, const char *name)
{
    unsigned int hashvalue=get_hashvalue_index(*index, &context_hash);
    void *ptr=NULL;

    logoutput("get_next_service_context");

    while (hashvalue<context_hash.len) {

	ptr=get_next_hashed_value(&context_hash, index, hashvalue);

	if (ptr) {

	    if (name) {
		struct service_context_s *c=(struct service_context_s *) ptr;

		if (strcmp(c->name, name)==0) break;

	    }

	} else {

	    break;

	}

	hashvalue++;

    }

    return (struct service_context_s *) ptr;

}

struct service_context_s *get_service_context(struct context_interface_s *interface)
{
    return (struct service_context_s *) ( ((char *) interface) - offsetof(struct service_context_s, interface));
}

static int add_context_eventloop(struct context_interface_s *interface, struct fs_connection_s *conn, unsigned int fd, int (*read_incoming_data)(int fd, void *ptr, uint32_t events), void *ptr, const char *name, unsigned int *error)
{
    struct service_context_s *context=get_service_context(interface);
    struct workspace_mount_s *workspace=context->workspace;

    *error=EIO;

    if (context->type==SERVICE_CTX_TYPE_CONNECTION) {
	struct service_context_s *root_context=workspace->context;
	struct fs_connection_s *root_connection=root_context->service.connection;
	struct beventloop_s *loop=root_connection->io.socket.xdata.loop;

	if (add_to_beventloop(fd, EPOLLIN, read_incoming_data, ptr, &conn->io.socket.xdata, loop)) {

	    if (name) set_bevent_name(&conn->io.socket.xdata, (char *) name, error);
	    *error=0;
	    context->service.connection=conn;
	    return 0;

	}

    } else if (context->type==SERVICE_CTX_TYPE_WORKSPACE) {
	struct beventloop_s *loop=conn->io.fuse.xdata.loop;

	if (add_to_beventloop(fd, EPOLLIN, read_incoming_data, ptr, &conn->io.fuse.xdata, loop)) {

	    if (name) set_bevent_name(&conn->io.fuse.xdata, (char *) name, error);
	    *error=0;
	    context->service.connection=conn;
	    return 0;

	}

    } else {

	logoutput("add_context_eventloop: error, type %i not reckognized", context->type);
	*error=EINVAL;

    }

    return -1;

}

static void remove_context_eventloop(struct context_interface_s *interface)
{
    struct service_context_s *context=get_service_context(interface);

    if (context->type==SERVICE_CTX_TYPE_CONNECTION || context->type==SERVICE_CTX_TYPE_WORKSPACE) {
	struct fs_connection_s *conn=context->service.connection;

	if (conn) {

	    remove_xdata_from_beventloop(&conn->io.socket.xdata);
	    context->service.connection=NULL;

	}

    }

}

void free_service_context(struct service_context_s *context)
{
    struct simple_lock_s wlock;

    logoutput("free_service_context: free context");

    if (context->type==SERVICE_CTX_TYPE_CONNECTION || context->type==SERVICE_CTX_TYPE_WORKSPACE) {
	struct fs_connection_s *conn=context->service.connection;

	if (conn) {

	    if (conn->io.socket.xdata.fd>0) {

		logoutput("free_service_context: close fd %i", conn->io.socket.xdata.fd);
		(* conn->io.socket.sops->close)(conn->io.socket.xdata.fd);
		conn->io.socket.xdata.fd=0;

	    }

	    remove_xdata_from_beventloop(&conn->io.socket.xdata);

	}

    }

    init_wlock_service_context_hash(&wlock);
    lock_service_context_hash(&wlock);
    remove_service_context_hash(context);
    unlock_service_context_hash(&wlock);
    free(context);

}

static int connect_dummy(uid_t uid, struct context_interface_s *interface, struct context_address_s *address, unsigned int *error)
{
    logoutput("connect_dummy");
    return -1;
}
static int start_dummy(struct context_interface_s *interface, int fd, void *data)
{
    logoutput("start_dummy");
    return -1;
}
static void signal_context_dummy(struct context_interface_s *interface, const char *what)
{
    struct service_context_s *context=get_service_context(interface);
    logoutput("signal_context_dummy: context name %s what %s", context->name, what);
}
static void signal_interface_dummy(struct context_interface_s *interface, const char *what)
{
    struct service_context_s *context=get_service_context(interface);
    logoutput("signal_interface_dummy: context name %s what %s", context->name, what);
}
static struct context_interface_s *get_parent_interface(struct context_interface_s *interface)
{
    if (interface) {
	struct service_context_s *context=get_service_context(interface);
	if (context->parent) return &context->parent->interface;
    }
    return NULL;
}

static unsigned int get_context_option_dummy(struct context_interface_s *interface, const char *name, struct context_option_s *option)
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
    context->type=type;
    context->unique=0;
    memset(context->name, '\0', sizeof(context->name));
    if (name) strncpy(context->name, name, sizeof(context->name));
    context->fscount=0;
    context->serviceid=0;
    context->workspace=NULL;
    context->error=0;
    context->refcount=0;
    init_list_element(&context->list, NULL);
    context->parent=NULL;

    if (type==SERVICE_CTX_TYPE_FILESYSTEM) {

	context->service.filesystem.inode=NULL;
	context->service.filesystem.fs=NULL;

    } else if (type==SERVICE_CTX_TYPE_CONNECTION || type==SERVICE_CTX_TYPE_WORKSPACE) {

	context->service.connection=NULL;

    }

    context->interface.ptr=NULL;
    context->interface.data=NULL;
    context->interface.connect=connect_dummy;
    context->interface.start=start_dummy;
    context->interface.signal_context=signal_context_dummy;
    context->interface.signal_interface=signal_interface_dummy;
    context->interface.get_parent=get_parent_interface;
    context->interface.add_context_eventloop=add_context_eventloop;
    context->interface.remove_context_eventloop=remove_context_eventloop;
    context->interface.get_context_option=get_context_option_dummy;
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
    struct workspace_mount_s *w=context->workspace;
    struct service_context_s *r=w->context;
    return r->service.connection->io.socket.xdata.loop;
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
