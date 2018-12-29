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

enum GenericErrorSubsystem {
    GENERIC_ERROR_SUBSYSTEM_DUMMY = 0,
    GENERIC_ERROR_SUBSYSTEM_APPLICATION,
    GENERIC_ERROR_SUBSYSTEM_LINUX,
    GENERIC_ERROR_SUBSYSTEM_LIBRARY,
};

union errorcode_u {
    uint64_t				value;
    void				*ptr;
    char				buffer[8];			/* size big enough to hold any error struct by libraries */
};

struct generic_error_s {
    enum GenericErrorSubsystem		type;
    union errorcode_u			error;
    char				*get_decription(struct generic_error_s *error);
    struct generic_error_s		*get_application_error(struct generic_error_s *error);
};


/* prototypes */

char *get_decription_error(struct generic_error_s *error);
struct generic_error_s *get_application_error(struct generic_error_s *error);

void set_generic_error_linux(struct generic_error_s *error, unsigned int errno);
