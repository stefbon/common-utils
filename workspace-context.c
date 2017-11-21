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
#include "entry-management.h"
#include "directory-management.h"
#include "entry-utils.h"
#include "utils.h"

#include "beventloop.h"
#include "beventloop-xdata.h"
#include "fuse-interface.h"
#include "fuse-fs.h"
#include "workspaces.h"

#include "logging.h"

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

	set_bevent_name(xdata, (char *) name, error);

    } else {

	*error=EIO;

    }

    return xdata;

}

void free_service_context(struct service_context_s *context)
{

    if (context->xdata.fd>0) {
	unsigned int fd=context->xdata.fd;

	logoutput("free_service_context: remove xdata fd %i", fd);

	remove_xdata_from_beventloop(&context->xdata);
	close(fd);

    }

    // if (context->inode) {
	//struct inode_s *inode=context->inode;
	//union datalink_u *link=get_datalink(inode);

	//link->data=NULL;
	//use_virtual_fs(NULL, inode);

    //}

    logoutput("free_service_context: free context");
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

static unsigned int get_interface_info_dummy(struct context_interface_s *interface, const char *what, void *data, unsigned char *buffer, unsigned int len, unsigned int *error)
{
    return 0;
}

struct service_context_s *create_service_context(struct workspace_mount_s *workspace, unsigned char type)
{
    struct service_context_s *context=NULL;

    context=malloc(sizeof(struct service_context_s));

    if (context) {

	memset(context, 0, sizeof(struct service_context_s));

	context->flags=0;
	context->type=type;
	memset(context->name, '\0', sizeof(context->name));
	context->fscount=0;
	context->serviceid=0;
	context->fs=NULL;
	context->inode=NULL;
	context->workspace=workspace;
	init_xdata(&context->xdata);
	context->error=0;
	context->refcount=0;
	context->list.next=NULL;
	context->list.prev=NULL;
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

	if (workspace) add_list_element_last(&workspace->contexes.head, &workspace->contexes.tail, &context->list);

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

unsigned char get_context_fs_count(struct context_interface_s *interface)
{
    struct service_context_s *context=get_service_context(interface);
    return get_workspace_fs_count(context->workspace);
}

void add_inode_context(struct service_context_s *context, struct inode_s *inode)
{
    struct entry_s *parent=inode->alias->parent;
    struct inode_s *parent_inode=parent->inode;

    add_inode_hashtable(inode, increase_inodes_workspace, (void *) context->workspace);
    (* parent_inode->fs->type.dir.use_fs)(context, inode);

    if (S_ISDIR(inode->mode)) {

	inode->link.data=(void *) get_dummy_directory();

    }

}

struct service_context_s *get_container_context(struct list_element_s *list)
{
    return (list) ? (struct service_context_s *) ( ((char *) list) - offsetof(struct service_context_s, list)) : NULL;
}
