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
#ifndef GENERAL_SIMPLE_LIST_H
#define GENERAL_SIMPLE_LIST_H

struct list_element_s {
    struct list_element_s *next;
    struct list_element_s *prev;
};

struct list_header_s {
    struct list_element_s *head;
    struct list_element_s *tail;
};

void add_list_element_last(struct list_element_s **head, struct list_element_s **tail, struct list_element_s *element);
void add_list_element_first(struct list_element_s **head, struct list_element_s **tail, struct list_element_s *element);
void add_list_element_after(struct list_element_s **head, struct list_element_s **tail, struct list_element_s *prev, struct list_element_s *element);
void add_list_element_before(struct list_element_s **head, struct list_element_s **tail, struct list_element_s *next, struct list_element_s *element);
void remove_list_element(struct list_element_s **head, struct list_element_s **tail, struct list_element_s *element);

struct list_element_s *get_list_head(struct list_element_s **head, struct list_element_s **tail);

#endif
