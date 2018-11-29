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

#ifndef SB_COMMON_UTILS_NETWORK_UTILS_H
#define SB_COMMON_UTILS_NETWORK_UTILS_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#define IP_ADDRESS_TYPE_IPv4			1
#define IP_ADDRESS_TYPE_IPv6			2

struct ip_adddress_s {
    unsigned char				type;
    union {
	char					v4[INET_ADDRSTRLEN + 1];
	char					v6[INET6_ADDRSTRLEN + 1];
    } ip;
};

struct host_address_s {
    char					hostname[NI_MAXHOST + 1];
    struct ip_adddress_s			ip;
};

/* prototypes */

unsigned char check_family_ip_address(char *address, const char *what);
char *get_connection_ipv4(unsigned int fd, unsigned char what, unsigned int *error);
char *get_connection_hostname(unsigned int fd, unsigned char what, unsigned int *error);

unsigned int get_msg_controllen(struct msghdr *message, const char *what);
void init_fd_msg(struct msghdr *message, char *buffer, unsigned int size, int fd);

int read_fd_msg(struct msghdr *message);

int compare_network_address(struct host_address_s *a, struct host_address_s *b);
int set_host_address(struct host_address_s *a, char *hostname, char *ipv4, char *ipv6);

#endif
