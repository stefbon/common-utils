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
