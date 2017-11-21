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

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "logging.h"
#include "pathinfo.h"

void init_pathinfo(struct pathinfo_s *pathinfo)
{
    pathinfo->path=NULL;
    pathinfo->len=0;
    pathinfo->flags=0;
    pathinfo->refcount=0;
}

void free_path_pathinfo(struct pathinfo_s *pathinfo)
{

    pathinfo->refcount--;

    if (pathinfo->refcount<=0) {

	if (pathinfo->path && (pathinfo->flags & PATHINFO_FLAGS_ALLOCATED)) {

	    free(pathinfo->path);
	    pathinfo->path=NULL;

	    pathinfo->flags-=PATHINFO_FLAGS_ALLOCATED;


	}

    }

}
