/*
  2010, 2011, 2012, 2013, 2014, 2015, 2016 Stef Bon <stefbon@gmail.com>

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

#ifndef SB_COMMON_UTILS_WORKSPACE_CONTEXT_H
#define SB_COMMON_UTILS_WORKSPACE_CONTEXT_H

#include "workspaces.h"

/* prototypes */

struct service_context_s *get_service_context(struct context_interface_s *interface);
struct bevent_xdata_s *add_context_eventloop(struct context_interface_s *interface, unsigned int fd, int (*read_incoming_data)(int fd, void *ptr, uint32_t events), void *ptr, const char *name, unsigned int *error);

struct service_context_s *create_service_context(struct workspace_mount_s *workspace, unsigned char type);
void free_service_context(struct service_context_s *context);

struct service_context_s *get_next_service_context(struct service_context_s *context, const char *name);

void *get_root_ptr_context(struct service_context_s *context);
struct service_context_s *get_root_context(struct service_context_s *context);
struct fuse_user_s *get_user_context(struct service_context_s *context);

struct beventloop_s *get_beventloop_ctx(void *ctx);
void add_inode_context(struct service_context_s *context, struct inode_s *inode);

struct service_context_s *get_container_context(struct list_element_s *list);

void translate_context_host_address(struct host_address_s *host, char **target);
void translate_context_network_port(struct service_address_s *service, unsigned int *port);
void translate_context_address_network(struct context_address_s *address, char **target, unsigned int *port);

void set_inode_link(struct inode_s *inode, unsigned int type, void *ptr);
struct inode_link_s *get_inode_link(struct inode_s *inode, struct inode_link_s *link);

#endif
