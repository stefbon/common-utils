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
#include <pthread.h>
#include <time.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "logging.h"

#include "pathinfo.h"
#include "entry-management.h"
#include "directory-management.h"
#include "entry-utils.h"
#include "fuse-fs.h"
#include "workspaces.h"
#include "path-caching.h"

/*
    struct to store the pathcache
    - pathinfo			cached path
    - base_inode		root inode of this fs (like a server)
    - type			temporary or permanent
*/

struct pathcache_s {
    struct service_context_s	*context;
    unsigned char		type;
    unsigned int		len;
    char 			path[];
};



/*

    get the path relative to a "root" inode of a service

    the root of a service (SHH, NFS, WebDav and SMB) is connected at an inode in this fs
    for communication with the backend server most services use the path relative to this root
    this function determines the path relative to this "root"

    it does this by looking at the inode->fs-calls->get_type() value
    this is different for every set of fs-calls

*/

int get_service_path_default(struct inode_s *inode, struct fuse_path_s *fpath)
{
    struct entry_s *entry=inode->alias;
    struct fuse_fs_s *fs=inode->fs;
    unsigned int pathlen=0;
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
    fs=inode->fs;

    if (inode->inode_link==NULL || inode->inode_link->type!=INODE_LINK_TYPE_CONTEXT) goto appendname;

    /* inode is the "root" of the service: data is holding the context */

    fpath->context=(struct service_context_s *) inode->inode_link->link.data;
    return pathlen;

}

unsigned int add_name_path(struct fuse_path_s *fpath, struct name_s *xname)
{

    fpath->pathstart-=xname->len;
    memcpy(fpath->pathstart, xname->name, xname->len);
    fpath->pathstart--;
    *fpath->pathstart='/';

    return xname->len+1;
}

void init_fuse_path(struct fuse_path_s *fpath, char *path, unsigned int len)
{
    fpath->context=NULL;
    fpath->path=path;
    fpath->len=len;
    fpath->pathstart=path+len;
    *(fpath->pathstart)='\0';
}

/* determine the path for this directory the default way */

static int get_service_path(struct directory_s *directory, void *ptr)
{
    struct fuse_path_s *fpath=(struct fuse_path_s *) ptr;
    return get_service_path_default(directory->inode, fpath);
}

/* get the cached path for directory */

static int get_service_path_cached(struct directory_s *directory, void *ptr)
{
    struct pathcache_s *pathcache=(struct pathcache_s *) directory->pathcalls.cache;
    struct fuse_path_s *fpath=(struct fuse_path_s *) ptr;

    fpath->context=pathcache->context;
    fpath->pathstart-=pathcache->len;
    memcpy(fpath->pathstart, pathcache->path, pathcache->len);

    return pathcache->len;

}

/* get the path when the directory is the root of the service */

static int get_service_path_root(struct directory_s *directory, void *ptr)
{
    struct fuse_path_s *fpath=(struct fuse_path_s *) ptr;
    union datalink_u *link=NULL;

    link=&directory->link;
    fpath->context=(struct service_context_s *) link->data;
    return 0;

}

void free_pathcache_dummy(struct pathcalls_s *pathcalls)
{
}

void free_pathcache(struct pathcalls_s *p)
{
    struct pathcache_s *pathcache=(struct pathcache_s *) p->cache;

    if (pathcache && (pathcache->type & PATHCACHE_TYPE_TEMP)) {

	free(pathcache);

	p->cache=NULL;
	p->get_path=get_service_path;
	p->free=free_pathcache_dummy;

    }

}


/* create a pathcache using the path stored in pathinfo */

void create_pathcache(struct pathcalls_s *p, struct fuse_path_s *fpath, unsigned char type)
{
    struct pathcache_s *pathcache=(struct pathcache_s *) p->cache;
    unsigned int len=0; 

    if (pathcache) return;

    len=(unsigned int) (fpath->path + fpath->len - fpath->pathstart); /* len of path in buffer from pathstart */
    pathcache=malloc(sizeof(struct pathcache_s) + len);

    if (pathcache) {

	pathcache->context=fpath->context;
	pathcache->type=type;
	pathcache->len=len;
	memcpy(pathcache->path, fpath->pathstart, len);

	p->get_path=get_service_path_cached;
	p->cache=(void *) pathcache;
	p->free=free_pathcache;

    } else {

	p->get_path=get_service_path;

    }

}


void init_pathcalls(struct pathcalls_s *p)
{

    memset(p, 0, sizeof(struct pathcalls_s));

    p->cache=NULL;
    p->get_path=get_service_path;
    p->free=free_pathcache_dummy;
    pthread_mutex_init(&p->mutex, NULL);

}

void init_pathcalls_root(struct pathcalls_s *p)
{
    memset(p, 0, sizeof(struct pathcalls_s));

    p->cache=NULL;
    p->get_path=get_service_path_root;
    p->free=free_pathcache_dummy;
    pthread_mutex_init(&p->mutex, NULL);

}
