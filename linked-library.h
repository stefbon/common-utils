/*
  2017 Stef Bon <stefbon@gmail.com>

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

/* TODO: add function to translate library specific error to decription and to application specific error */

#ifndef SB_COMMON_UTILS_LINKED_LIBRARY_H
#define SB_COMMON_UTILS_LINKED_LIBRARY_H

#include "error-handling.h"

struct linked_library_s {
    char				*name;
    void				*ptr;
    void				(* close)(void *ptr);
    struct linked_library_s		*next;
};

/* prototypes */

void add_linked_library(void *ptr, char *name, void (* close)(void *ptr));
void close_linked_libraries();

#endif
