/*

  2017 Stef Bon <stefbon@nomail.com>

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

#include <pthread.h>

#include <sys/stat.h>
#include <sys/param.h>
#include <sys/types.h>
#include <time.h>

#include "linked-library.h"

static pthread_mutex_t		linked_libraries_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct linked_library_s	linked_libraries=NULL;

void add_linked_library(void *ptr, char *name, void (* close)(void *ptr))
{
    struct linked_library_s *library=NULL;

    pthread_mutex_lock(&linked_libraries_mutex);

    library=linked_libraries;

    while (library) {

	if (strcmp(library->name, name)==0) {

	    pthread_mutex_unlock(&linked_libraries_mutex);
	    return;

	}

	library=library->next;

    }

    library=malloc(sizeof(struct linked_library_s));

    if (library) {

	memset(library, 0, sizeof(struct linked_library_s));

	library->name=name;
	library->ptr=ptr;
	library->close=close;

	library->next=linked_libraries;
	linked_libraries=library;

    }

    pthread_mutex_unlock(&linked_libraries_mutex);

}

void close_linked_libraries()
{
    struct linked_library_s *library=NULL;

    pthread_mutex_lock(&linked_libraries_mutex);

    library=linked_libraries;

    while (library) {

	linked_libraries=library->next;

	(* library->close)(library->ptr);
	free(library);
	library=linked_libraries;

    }

    pthread_mutex_unlock(&linked_libraries_mutex);

}
