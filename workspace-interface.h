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

#ifndef SB_COMMON_UTILS_WORKSPACE_INTERFACE_H
#define SB_COMMON_UTILS_WORKSPACE_INTERFACE_H

#include "pathinfo.h"
#include "network-utils.h"

#define _INTERFACE_ADDRESS_NONE				0
#define _INTERFACE_ADDRESS_NETWORK			1
#define _INTERFACE_ADDRESS_SMB_SERVER			2

#define _INTERFACE_SERVICE_NONE				0
#define _INTERFACE_SERVICE_FUSE				1
#define _INTERFACE_SERVICE_PORT				2
#define _INTERFACE_SERVICE_SMB_SHARE			3
#define _INTERFACE_SERVICE_NFS_EXPORT			4
#define _INTERFACE_SERVICE_SFTP				5

#define _INTERFACE_SFTP_FLAG_NEWREADDIR			1

#define _INTERFACE_PORT_TCP				1
#define _INTERFACE_PORT_UDP				2

#define _INTERFACE_OPTION_INT				1
#define _INTERFACE_OPTION_PCHAR				2
#define _INTERFACE_OPTION_PVOID				3

struct context_option_s {
    unsigned char					type;
    union {
	unsigned int					number;
	char						*ptr;
	void						*data;
    } value;
};

struct network_port_s {
    unsigned int					port;
    unsigned char					type;
};

struct service_address_s {
    unsigned char					type;
    union {
	struct network_port_s				port;
	struct fuse_mount_s {
	    char					*source;
	    char					*mountpoint;
	    char					*name;
	} fuse;
	struct smbshare_s {
	    char					share[128];
	    unsigned int				port;
	} smbshare;
	struct sftp_server_s {
	    char					name[256];
	} sftp;
	struct nfs_export_s {
	    char					*dir;
	    unsigned int				port;
	} nfs;
    } target;
};

struct context_address_s {
    struct network_address_s {
	unsigned char					type;
	union {
	    struct host_address_s			host;
	    char					smbserver[128];
	} target;
    } network;
    struct service_address_s				service;
};

#define CONTEXT_INTERFACE_BACKEND_SFTP_PREFIX_HOME					1
#define CONTEXT_INTERFACE_BACKEND_SFTP_PREFIX_ROOT					2
#define CONTEXT_INTERFACE_BACKEND_SFTP_PREFIX_CUSTOM					3

struct context_interface_s {
    void				*ptr;
    void				*data;
    void 				*(* connect)(uid_t uid, struct context_interface_s *interface, struct context_address_s *address, unsigned int *error);
    int					(* start)(struct context_interface_s *interface, void *data);
    void				(* disconnect)(struct context_interface_s *interface);
    void				(* free)(struct context_interface_s *interface);
    struct context_interface_s		*(*get_parent)(struct context_interface_s *interface);
    struct bevent_xdata_s 		*(*add_context_eventloop)(struct context_interface_s *interface, unsigned int fd, int (*read_incoming_data)(int fd, void *ptr, uint32_t events), void *ptr, const char *name, unsigned int *error);
    unsigned int			(* get_interface_option)(struct context_interface_s *interface, const char *name, struct context_option_s *option);
    unsigned int			(* get_interface_info)(struct context_interface_s *interface, const char *what, void *data, struct common_buffer_s *buffer);
    union {
	struct sftp_ops_s {
	    int					(* complete_path)(struct context_interface_s *interface, char *buffer, struct pathinfo_s *pathinfo);
	    unsigned int			(* get_complete_pathlen)(struct context_interface_s *interface, unsigned int len);
	    struct prefix_s {
		unsigned char			type;
		char				*path;
		unsigned int			len;
	    } prefix;
	    unsigned int			flags;
	} sftp;
	void					*data;
    } backend;
};

static inline unsigned int get_interface_option_integer(struct context_interface_s *interface, const char *name, int *reply)
{
    struct context_option_s option;
    unsigned int result=0;

    memset(&option, 0, sizeof(struct context_option_s));

    result=(* interface->get_interface_option)(interface, name, &option);

    if (option.type==_INTERFACE_OPTION_INT) {

	memcpy(reply, (void *) &option.value.number, sizeof(int));
	return result;

    }

    return 0;

}

#endif
