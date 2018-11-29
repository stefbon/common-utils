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
#ifndef SB_COMMON_UTILS_SIMPLE_LIST_H
#define SB_COMMON_UTILS_SIMPLE_LIST_H

#include					<stdint.h>

#define SIMPLE_LIST_TYPE_EMPTY			1
#define SIMPLE_LIST_TYPE_DEFAULT		2
#define SIMPLE_LIST_TYPE_ONE			3

#define SIMPLE_LIST_FLAG_REMOVE			1

struct list_element_s;
struct list_header_s;

struct element_ops_s {
    void 			(* delete)(struct list_element_s *e);
    void			(* insert_after)(struct list_element_s *a, struct list_element_s *e);
    void			(* insert_before)(struct list_element_s *b, struct list_element_s *e);
};

struct header_ops_s {
    void			(* insert_after)(struct list_element_s *a, struct list_element_s *e);
    void			(* insert_before)(struct list_element_s *b, struct list_element_s *e);
    void 			(* delete)(struct list_element_s *element);
};

struct list_element_s {
    struct list_header_s	*h;
    struct list_element_s 	*n;
    struct list_element_s 	*p;
    struct timespec		added;
    struct element_ops_s	ops;
};

struct list_header_s {
    uint64_t			count;
    struct list_element_s 	*head;
    struct list_element_s 	*tail;
    struct header_ops_s		*ops;
    char			*name;
};

struct header_ops_s empty_header_ops;

#define				INIT_LIST_HEADER		{ 0, NULL, NULL, &empty_header_ops, NULL }

void init_list_element(struct list_element_s *e, struct list_header_s *h);
void init_list_header(struct list_header_s *h, unsigned char type, struct list_element_s *e);

void add_list_element_last(struct list_header_s *h, struct list_element_s *e);
void add_list_element_first(struct list_header_s *h, struct list_element_s *e);
void add_list_element_after(struct list_header_s *h, struct list_element_s *p, struct list_element_s *e);
void add_list_element_before(struct list_header_s *h, struct list_element_s *n, struct list_element_s *e);
void remove_list_element(struct list_element_s *e);

struct list_element_s *get_list_head(struct list_header_s *h, unsigned char flags);

struct list_element_s *search_list_element_forw(struct list_header_s *h, int (* condition)(struct list_element_s *list, void *ptr), void *ptr);
struct list_element_s *search_list_element_back(struct list_header_s *h, int (* condition)(struct list_element_s *list, void *ptr), void *ptr);

signed char list_element_is_first(struct list_element_s *e);
signed char list_element_is_last(struct list_element_s *e);

struct list_element_s *get_next_element(struct list_element_s *e);
struct list_element_s *get_prev_element(struct list_element_s *e);

#endif
