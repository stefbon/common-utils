/*
  2010, 2011, 2012, 2013, 2014, 2015, 2016 Stef Bon <stefbon@gmail.com>

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
#include <sys/mount.h>

#include <pthread.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "pathinfo.h"
#include "utils.h"
#include "beventloop.h"
#include "workerthreads.h"
#include "entry-management.h"
#include "directory-management.h"
#include "entry-utils.h"
#include "fuse-interface.h"
#include "fuse-fs.h"
#include "workspaces.h"
#include "path-caching.h"

#undef LOGGING
#include "logging.h"

extern const char *dotdotname;
extern const char *dotname;

void adjust_pathmax(struct workspace_mount_s *w, unsigned int len)
{
    pthread_mutex_lock(&w->mutex);
    if (len>w->pathmax) w->pathmax=len;
    pthread_mutex_unlock(&w->mutex);
}

unsigned int get_pathmax(struct workspace_mount_s *w)
{
    return w->pathmax;
}

void clear_workspace_mount(struct workspace_mount_s *workspace)
{
    struct directory_s *directory=get_directory(&workspace->rootinode);
    unsigned int error=0;

    logoutput("clear_workspace_mount: mountpoint %s", workspace->mountpoint.path);

    directory=(* directory->dops->remove_directory)(&workspace->rootinode, &error);

    if (directory) {
	struct service_context_s *context=workspace->context;

	clear_directory(&context->interface, directory, NULL, NULL);
	destroy_directory(directory);

    }

}

void free_workspace_mount(struct workspace_mount_s *workspace)
{
    free_path_pathinfo(&workspace->mountpoint);
    pthread_mutex_destroy(&workspace->mutex);
    free(workspace);
}

void increase_inodes_workspace(void *data)
{
    struct workspace_mount_s *workspace=(struct workspace_mount_s *) data;
    pthread_mutex_lock(&workspace->mutex);
    workspace->nrinodes++;
    pthread_mutex_unlock(&workspace->mutex);
}

void decrease_inodes_workspace(void *data)
{
    struct workspace_mount_s *workspace=(struct workspace_mount_s *) data;
    pthread_mutex_lock(&workspace->mutex);
    workspace->nrinodes--;
    pthread_mutex_unlock(&workspace->mutex);
}

int init_workspace_mount(struct workspace_mount_s *workspace, unsigned int *error)
{
    struct entry_s *rootentry=NULL;
    struct name_s xname={NULL, 0, 0};
    struct inode_s *rootinode=&workspace->rootinode;
    struct stat *st=&rootinode->st;

    memset(workspace, 0, sizeof(struct workspace_mount_s));

    workspace->base=NULL;
    workspace->context=NULL;
    workspace->user=NULL;

    workspace->nrinodes=1;
    workspace->mountpoint.path=NULL;
    workspace->mountpoint.len=0;
    workspace->mountpoint.flags=0;
    workspace->mountpoint.refcount=0;

    workspace->syncdate.tv_sec=0;
    workspace->syncdate.tv_nsec=0;
    workspace->status=0;
    workspace->fscount=0;

    workspace->free=free_workspace_mount;
    init_list_header(&workspace->contexes, SIMPLE_LIST_TYPE_EMPTY, NULL);
    init_list_element(&workspace->list, NULL);

    init_inode(rootinode);
    st->st_mode=S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    st->st_nlink=2;
    st->st_uid=0;
    st->st_gid=0;
    st->st_size=_INODE_DIRECTORY_SIZE;
    st->st_blksize=1024;
    st->st_blocks=(unsigned int) (st->st_size / st->st_blksize) + ((st->st_size % st->st_blksize)==0) ? 1 : 0;

    get_current_time(&st->st_mtim);
    st->st_ctim.tv_sec=st->st_mtim.tv_sec;
    st->st_ctim.tv_nsec=st->st_mtim.tv_nsec;
    st->st_ino=FUSE_ROOT_ID;

    rootinode->nlookup=1;
    rootinode->fs=NULL;

    pthread_mutex_init(&workspace->mutex, NULL);
    workspace->pathmax=512;

    xname.name=(char *) dotname;
    xname.len=strlen(xname.name);
    calculate_nameindex(&xname);

    rootentry=create_entry(NULL, &xname);

    if (rootentry) {

	rootinode->alias=rootentry;
	rootentry->inode=rootinode;

    } else {

	*error=ENOMEM;
	return -1;

    }

    return 0;

}

int mount_workspace_mount(struct service_context_s *context, char *source, char *name, unsigned int *error)
{
    struct workspace_mount_s *workspace=context->workspace;

    if (init_fuse_interface(&context->interface)==0) {
	struct context_address_s address;

	memset(&address, 0, sizeof(struct context_address_s));

	address.network.type=_INTERFACE_ADDRESS_NONE;
	address.service.type=_INTERFACE_SERVICE_FUSE;
	address.service.target.fuse.source=source;
	address.service.target.fuse.mountpoint=workspace->mountpoint.path;
	address.service.target.fuse.name=name;

	if (! (* context->interface.connect)(workspace->user->uid, &context->interface, &address, error)) {

	    logoutput("mount_workspace_mount: error %i:%s mounting %s", *error, strerror(*error), workspace->mountpoint.path);
	    return -1;;

	}

    } else {

	logoutput("mount_workspace_mount: error initializing  fuse interface");
	return -1;

    }

    return 0;

}

void umount_workspace_mount(struct service_context_s *context)
{
    (* context->interface.free)(&context->interface);
}

/*
    get the path to the root (mountpoint) of this fuse fs
*/

int get_path_root(struct inode_s *inode, struct fuse_path_s *fpath)
{
    struct entry_s *entry=inode->alias;
    unsigned int pathlen=(unsigned int) (fpath->path + fpath->len - fpath->pathstart);
    struct name_s *xname=NULL;

    appendname:

    xname=&entry->name;

    fpath->pathstart-=xname->len;
    memcpy(fpath->pathstart, xname->name, xname->len);
    fpath->pathstart--;
    *(fpath->pathstart)='/';
    pathlen+=xname->len+1;

    /* go one entry higher */

    entry=entry->parent;
    inode=entry->inode;
    if (inode->st.st_ino>FUSE_ROOT_ID) goto appendname;

    return pathlen;

}

struct workspace_mount_s *get_container_workspace(struct list_element_s *list)
{
    return (list) ? (struct workspace_mount_s *) ( ((char *) list) - offsetof(struct workspace_mount_s, list)) : NULL;
}

void create_personal_workspace_mount(struct workspace_mount_s *w)
{
    struct fuse_user_s *user=w->user;
    struct passwd *pw=getpwuid(user->uid);

    if (pw) {
	unsigned int len = 2 + strlen(pw->pw_dir) + strlen("Network");
	char path[len];

	if (snprintf(path, len, "%s/Network", pw->pw_dir)>0) {

	    if (mkdir(path, S_IFDIR | S_IRUSR | S_IXUSR | S_IWUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)==-1) {

		if (errno==EEXIST) umount(path);

	    }

	    mount(w->mountpoint.path, path, NULL, MS_BIND, NULL);

	}

    }

}