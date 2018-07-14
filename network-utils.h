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

#ifndef _COMMON_NETWORK_UTILS_H
#define _COMMON_NETWORK_UTILS_H

/* prototypes */

unsigned char check_family_ip_address(char *address, const char *what);
char *get_connection_ipv4(unsigned int fd, unsigned char what, unsigned int *error);
char *get_connection_hostname(unsigned int fd, unsigned char what, unsigned int *error);

void init_fd_msg(struct msghdr *message, int fd);
int read_fd_msg(struct msghdr *message);

#endif
