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

#define IP_ADDRESS_FAMILY_IPv4			1
#define IP_ADDRESS_FAMILY_IPv6			2

struct ip_adddress_s {
    unsigned char				family;
    union {
	char					v4[INET_ADDRSTRLEN + 1];
	char					v6[INET6_ADDRSTRLEN + 1];
    } ip;
};

#define HOST_ADDRESS_FLAG_HOSTNAME		1
#define HOST_ADDRESS_FLAG_IP			2

struct host_address_s {
    unsigned int				flags;
    char					hostname[NI_MAXHOST + 1];
    struct ip_adddress_s			ip;
};

/* prototypes */

unsigned char check_family_ip_address(char *address, const char *what);

unsigned int get_msg_controllen(struct msghdr *message, const char *what);
void init_fd_msg(struct msghdr *message, char *buffer, unsigned int size, int fd);

int read_fd_msg(struct msghdr *message);

int compare_host_address(struct host_address_s *a, struct host_address_s *b);
int set_host_address(struct host_address_s *a, char *hostname, char *ipv4, char *ipv6);
void translate_context_host_address(struct host_address_s *host, char **target, unsigned int *family);
void get_host_address(struct host_address_s *a, char **hostname, char **ipv4, char **ipv6);
void init_host_address(struct host_address_s *a);

#endif
