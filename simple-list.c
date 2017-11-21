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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <pthread.h>
#include "simple-list.h"

void add_list_element_last(struct list_element_s **head, struct list_element_s **tail, struct list_element_s *element)
{
    if ( ! *head) {

	*head=element;
	*tail=element;
	element->next=NULL;
	element->prev=NULL;

    } else {
	struct list_element_s *last=*tail;

	element->next=NULL;
	last->next=element;
	element->prev=last;
	*tail=element;

    }
}

void add_list_element_first(struct list_element_s **head, struct list_element_s **tail, struct list_element_s *element)
{
    if ( ! *head) {

	*head=element;
	*tail=element;
	element->next=NULL;
	element->prev=NULL;

    } else {
	struct list_element_s *first=*head;

	element->prev=NULL;
	first->prev=element;
	element->next=first;
	*head=element;

    }

}

void add_list_element_after(struct list_element_s **head, struct list_element_s **tail, struct list_element_s *prev, struct list_element_s *element)
{

    if (prev==*tail) {

	prev->next=element;
	element->prev=prev;
	*tail=element;
	element->next=NULL;

    } else {

	element->next=prev->next;
	prev->next->prev=element;
	element->prev=prev;
	prev->next=element;

    }

}

void add_list_element_before(struct list_element_s **head, struct list_element_s **tail, struct list_element_s *next, struct list_element_s *element)
{

    if (next==*head) {

	next->prev=element;
	element->next=next;
	*head=element;
	element->prev=NULL;

    } else {

	element->prev=next->prev;
	next->prev->next=element;
	element->next=next;
	next->prev=element;

    }

}

void remove_list_element(struct list_element_s **head, struct list_element_s **tail, struct list_element_s *element)
{

    if (element==*head) {

	if (element==*tail) {

	    *head=NULL;
	    *tail=NULL;

	} else {

	    *head=element->next;
	    element->next->prev=NULL;

	}

    } else {

	if (element==*tail) {

	    *tail=element->prev;
	    element->prev->next=NULL;

	} else {

	    element->prev->next=element->next;
	    element->next->prev=element->prev;

	}

    }

    element->next=NULL;
    element->prev=NULL;

}


struct list_element_s *get_list_head(struct list_element_s **head, struct list_element_s **tail)
{
    struct list_element_s *element=NULL;

    if (*head) {

	element=*head;

	if (*tail==*head) {

	    *head=NULL;
	    *tail=NULL;

	} else {

	    *head=element->next;
	    (*head)->prev=NULL;

	}

    }

    return element;
}

