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

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <sys/time.h>
#include <ctype.h>
#include <inttypes.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#define LOGGING
#include "logging.h"

#include "network-utils.h"

unsigned char check_family_ip_address(char *address, const char *what)
{
    if (strcmp(what, "ipv4")==0) {
	struct in_addr tmp;

	return inet_pton(AF_INET, address, (void *)&tmp);

    } else if (strcmp(what, "ipv6")==0) {
	struct in6_addr tmp;

	return inet_pton(AF_INET6, address, (void *)&tmp);

    }

    return 0;

}

/* get ipv4 as char * of connection
    - what = 0 -> local
    - what = 1 -> remote */

char *get_connection_ipv4(unsigned int fd, unsigned char what, unsigned int *error)
{
    struct sockaddr_in addr;
    socklen_t len=sizeof(struct sockaddr_in);
    char *result=NULL;
    char *tmp=NULL;

    if (what==0) {

	if (getsockname(fd, &addr, &len)==-1) {

	    *error=errno;
	    return NULL;

	}

    } else {

	if (getpeername(fd, &addr, &len)==-1) {

	    *error=errno;
	    return NULL;

	}

    }

    tmp=inet_ntoa(addr.sin_addr);

    if (tmp) {

	result=strdup(tmp);
	if (result==NULL) *error=ENOMEM;

    }

    return result;

}

/* get hostname of connection
    - what = 0 -> local
    - what = 1 -> remote
*/

char *get_connection_hostname(unsigned int fd, unsigned char what, unsigned int *error)
{
    struct sockaddr addr;
    socklen_t len = sizeof(struct sockaddr);
    int result = 0;
    char tmp[NI_MAXHOST];
    unsigned int count = 0;

    if (what==0) {

	logoutput("get_connection_hostname: fd=%i local name", fd);

	if (getsockname(fd, &addr, &len)==-1) {

	    *error=errno;
	    return NULL;

	}

    } else {

	logoutput("get_connection_hostname: fd=%i remote name", fd);

	if (getpeername(fd, &addr, &len)==-1) {

	    *error=errno;
	    return NULL;

	}

    }

    gethostname:

    memset(tmp, '\0', NI_MAXHOST);
    result=getnameinfo(&addr, len, tmp, NI_MAXHOST, NULL, 0, NI_NAMEREQD);
    count++;

    if (result==0) {
	char *hostname=NULL;

	hostname=strdup(tmp);
	if (hostname==NULL) *error=ENOMEM;
	return hostname;

    } else {

	logoutput("get_connection_hostname: error %i:%s", result, gai_strerror(result));

	if (result==EAI_MEMORY) {

	    *error=ENOMEM;

	} else if (result==EAI_NONAME) {

	    *error=ENOENT;

	} else if (result==EAI_SYSTEM) {

	    *error=errno;

	} else if (result==EAI_OVERFLOW) {

	    *error=ENAMETOOLONG;

	} else if (result==EAI_AGAIN) {

	    if (count<10) goto gethostname;
	    *error=EIO;

	} else {

	    *error=EIO;

	}

    }

    return NULL;

}

/* initialize a message to send an fd over a unix socket 
    copied from question on stackoverflow:
    https://stackoverflow.com/questions/37885831/ubuntu-linux-send-file-descriptor-with-unix-domain-socket#37885976
*/

unsigned int get_msg_controllen(struct msghdr *message, const char *what)
{

    if (strcmp(what, "int")==0) {

	return CMSG_SPACE(sizeof(int));

    }

    return 0;
}

void init_fd_msg(struct msghdr *message, char *buffer, unsigned int size, int fd)
{
    struct cmsghdr *ctrl_message = NULL;

    /* init space ancillary data */

    memset(buffer, 0, size);
    message->msg_control = buffer;
    message->msg_controllen = size;

    /* assign fd to a single ancillary data element */

    ctrl_message = CMSG_FIRSTHDR(message);
    ctrl_message->cmsg_level = SOL_SOCKET;
    ctrl_message->cmsg_type = SCM_RIGHTS;
    ctrl_message->cmsg_len = CMSG_LEN(sizeof(int));

    *((int *) CMSG_DATA(ctrl_message)) = fd;

}

/* read fd from a message
    copied from question on stackoverflow:
    https://stackoverflow.com/questions/37885831/ubuntu-linux-send-file-descriptor-with-unix-domain-socket#37885976
*/

int read_fd_msg(struct msghdr *message)
{
    struct cmsghdr *ctrl_message = NULL;
    int fd=-1;

    logoutput("read_fd_msg");

    /* iterate ancillary elements */

    for (ctrl_message = CMSG_FIRSTHDR(message); ctrl_message != NULL; ctrl_message = CMSG_NXTHDR(message, ctrl_message)) {

	if ((ctrl_message->cmsg_level == SOL_SOCKET) && (ctrl_message->cmsg_type == SCM_RIGHTS)) {

	    logoutput("read_fd_msg: found ctrl message");

	    fd = *((int *) CMSG_DATA(ctrl_message));
	    break;
	}

    }

    return fd;

}

int set_host_address(struct host_address_s *a, char *hostname, char *ipv4, char *ipv6)
{

    if (hostname && strlen(hostname)>0) {
	unsigned int len=strlen(hostname);

	memset(a->hostname, '\0', NI_MAXHOST + 1);

	if (len>NI_MAXHOST) len=NI_MAXHOST;
	memcpy(a->hostname, hostname, len);
	return 0;

    }

    if (ipv4 && strlen(ipv4)>0) {

	memset(a->ip.ip.v4, '\0', INET_ADDRSTRLEN + 1);

	if (strlen(ipv4) <=INET_ADDRSTRLEN) {

	    strcpy(a->ip.ip.v4, ipv4);
	    a->ip.type=IP_ADDRESS_TYPE_IPv4;
	    return 0;

	}

    }

    if (ipv6) {

	memset(a->ip.ip.v6, '\0', INET6_ADDRSTRLEN + 1);

	if (strlen(ipv6)<=INET6_ADDRSTRLEN) {

	    strcpy(a->ip.ip.v6, ipv6);
	    a->ip.type=IP_ADDRESS_TYPE_IPv6;
	    return 0;

	}

    }

    return -1;
}

int compare_network_address(struct host_address_s *a, struct host_address_s *b)
{

    if (strlen(a->hostname)>0 && strlen(b->hostname)>0) {

	if (strcmp(a->hostname, b->hostname)==0) return 0;

    }

    if (a->ip.type==IP_ADDRESS_TYPE_IPv4 && b->ip.type==IP_ADDRESS_TYPE_IPv4) {

	if (strcmp(a->ip.ip.v4, b->ip.ip.v4)==0) return 0;

    }

    if (a->ip.type==IP_ADDRESS_TYPE_IPv6 && b->ip.type==IP_ADDRESS_TYPE_IPv6) {

	if (strcmp(a->ip.ip.v6, b->ip.ip.v6)==0) return 0;

    }

    return -1;

}
