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

char *get_decription_error(struct generic_error_s *error)
{
    return (* error->get_description)(error);
}

struct generic_error_s *get_application_error(struct generic_error_s *error)
{
    return (* error->get_application_error)(error);
}

static char *get_description_linux(struct generic_error_s *error)
{
    return strerror(error->error.value);
}

static struct generic_error_s *get_app_error(struct generic_error_s *error)
{
    /* here the application errors are not known */
    return NULL;
}

void set_generic_error_linux(struct generic_error_s *error, unsigned int errno)
{
    error->type=GENERIC_ERROR_SUBSYSTEM_LINUX;
    error->error.value=errno;
    error->get_description=get_description_linux;
    error->get_application_error=get_app_error;
}
