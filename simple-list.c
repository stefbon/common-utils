/*
  2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018 Stef Bon <stefbon@gmail.com>

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

#undef LOGGING
#include "logging.h"
#include "utils.h"

void set_element_ops_default_tail(struct element_ops_s *ops);
void set_element_ops_default(struct element_ops_s *ops);
void set_element_ops_default_head(struct element_ops_s *ops);
void set_element_ops_one(struct element_ops_s *ops);

static void delete_dummy(struct list_element_s *e)
{
}
static void insert_common_dummy(struct list_element_s *a, struct list_element_s *b)
{
}
void init_list_element(struct list_element_s *e, struct list_header_s *h)
{
    e->h=h;
    e->n=NULL;
    e->p=NULL;
    e->added.tv_sec=0;
    e->added.tv_nsec=0;
    e->ops.delete=delete_dummy;
    e->ops.insert_after=insert_common_dummy;
    e->ops.insert_before=insert_common_dummy;
}

/* OPS for an element DEFAULT */

/* DEFAULT - INSERT BETWEEN */

static void insert_element_after_default(struct list_element_s *p, struct list_element_s *e)
{
    struct list_header_s *h=(p->h) ? p->h : e->h;

    init_list_element(e, h);

    p->n->p=e;
    p->n=e;
    e->p=p;
    e->n=p->n;

    set_element_ops_default(&e->ops);
    h->count++;
}

/* DEFAULT - INSERT after TAIL */

static void insert_element_after_tail_default(struct list_element_s *p, struct list_element_s *e)
{
    struct list_header_s *h=(p->h) ? p->h : e->h;

    init_list_element(e, h);

    p->n=e;
    e->p=p;

    set_element_ops_default(&p->ops); /* p is not the tail anymore */
    set_element_ops_default_tail(&e->ops); /* e is the tail */

    h->tail=e;
    h->count++;
}

static void insert_element_before_default(struct list_element_s *n, struct list_element_s *e)
{
    struct list_header_s *h=(n->h) ? n->h : e->h;

    init_list_element(e, h);

    n->p->n=e;
    n->p=e;
    e->n=n;
    e->p=n->p;

    set_element_ops_default(&e->ops);

    h->count++;
}

/* DEFAULT - INSERT before HEAD */

static void insert_element_before_head_default(struct list_element_s *n, struct list_element_s *e)
{
    struct list_header_s *h=(n->h) ? n->h : e->h;

    init_list_element(e, h);

    n->p=e;
    e->n=n;

    set_element_ops_default(&n->ops); /* n is not the head anymore */
    set_element_ops_default_head(&e->ops); /* e is the head */

    h->head=e;
    h->count++;
}

/* DEFAULT - DELETE between */

static void delete_element_default(struct list_element_s *e)
{
    struct list_header_s *h=e->h;

    e->n->p=e->p;
    e->p->n=e->n;

    init_list_element(e, NULL);
    h->count--;
    init_list_element(e, NULL);

}

static void delete_element_head(struct list_element_s *e)
{
    struct list_header_s *h=e->h;
    struct list_element_s *n=e->n; /* next */

    /* head shifts to right */

    h->head=n;
    n->p=NULL;

    /* new head has now different ops */

    h->count--;

    if (h->count==1) {

	init_list_header(h, SIMPLE_LIST_TYPE_ONE, n);
	set_element_ops_one(&n->ops);

    } else {

	set_element_ops_default_head(&n->ops);

    }

    init_list_element(e, NULL);
}

static void delete_element_tail(struct list_element_s *e)
{
    struct list_header_s *h=e->h;
    struct list_element_s *p=e->p; /* prev */

    /* head shifts to left */

    h->tail=p;
    p->n=NULL;

    /* new tail has now different ops */

    h->count--;
    if (h->count==1) {

	init_list_header(h, SIMPLE_LIST_TYPE_ONE, p);
	set_element_ops_one(&p->ops);

    } else {

	set_element_ops_default_tail(&p->ops);

    }

    init_list_element(e, NULL);
}

void set_element_ops_default(struct element_ops_s *ops)
{
    ops->delete=delete_element_default;
    ops->insert_before=insert_element_before_default;
    ops->insert_after=insert_element_after_default;
}

void set_element_ops_default_head(struct element_ops_s *ops)
{
    ops->delete=delete_element_head;
    ops->insert_before=insert_element_before_head_default;
    ops->insert_after=insert_element_after_default;
}

void set_element_ops_default_tail(struct element_ops_s *ops)
{
    ops->delete=delete_element_tail;
    ops->insert_before=insert_element_before_default;
    ops->insert_after=insert_element_after_tail_default;
}

/* OPS for an element ONE */

/* ONE */

static void delete_element_one(struct list_element_s *e)
{
    struct list_header_s *h=e->h;

    h->head=NULL;
    h->tail=NULL;

    init_list_element(e, NULL);
    h->count--;
    init_list_header(h, SIMPLE_LIST_TYPE_EMPTY, NULL);
}

static void insert_element_before_head_one(struct list_element_s *n, struct list_element_s *e)
{
    struct list_header_s *h=(n->h) ? n->h : e->h;

    init_list_element(e, h);

    n->p=e;
    e->n=n;

    set_element_ops_default_head(&e->ops);
    set_element_ops_default_tail(&n->ops);

    h->head=e;
    h->count++;
    init_list_header(h, SIMPLE_LIST_TYPE_DEFAULT, NULL);
}

static void insert_element_after_tail_one(struct list_element_s *p, struct list_element_s *e)
{
    struct list_header_s *h=(p->h) ? p->h : e->h;

    init_list_element(e, h);

    p->n=e;
    e->p=p;

    set_element_ops_default_head(&p->ops);
    set_element_ops_default_tail(&e->ops);

    h->tail=e;
    h->count++;
    init_list_header(h, SIMPLE_LIST_TYPE_DEFAULT, NULL);
}

void set_element_ops_one(struct element_ops_s *ops)
{
    ops->delete=delete_element_one;
    ops->insert_before=insert_element_before_head_one;
    ops->insert_after=insert_element_after_tail_one;
}

/* header OPS for an EMPTY list */

static void insert_element_common_empty(struct list_element_s *a, struct list_element_s *e)
{
    /* insert after in an empty list: a must be empty
	not a is not defined but is a default parameter */

    struct list_header_s *h=e->h;

    // logoutput("insert_element_common_empty");

    init_list_element(e, h);

    /* start with the ONE ops */
    // logoutput("insert_element_common_empty: A");
    init_list_header(h, SIMPLE_LIST_TYPE_ONE, e);/* well it's not empty anymore */
    // logoutput("insert_element_common_empty: B");
};
static void delete_empty(struct list_element_s *e)
{
    // logoutput("delete_empty");
    /* delete in an empty list: not possible*/
};

void set_element_ops_empty(struct element_ops_s *ops)
{
    ops->delete=delete_empty;
    ops->insert_before=insert_element_common_empty;
    ops->insert_after=insert_element_common_empty;
}

struct header_ops_s empty_header_ops = {
    .delete				= delete_empty,
    .insert_after			= insert_element_common_empty,
    .insert_before			= insert_element_common_empty,
};

/* OPS for a NON EMPTY list */

static void insert_after_default(struct list_element_s *a, struct list_element_s *e)
{
    // logoutput("insert_after_default");
    (* a->ops.insert_after)(a, e);
};
static void insert_before_default(struct list_element_s *b, struct list_element_s *e)
{
    // logoutput("insert_before_default");
    (* b->ops.insert_before)(b, e);
};
static void delete_default(struct list_element_s *e)
{
    // logoutput("delete_default");
    (* e->ops.delete)(e);
};

struct header_ops_s default_header_ops = {
    .delete				= delete_default,
    .insert_after			= insert_after_default,
    .insert_before			= insert_before_default,
};

void add_list_element_last(struct list_header_s *h, struct list_element_s *e)
{

    init_list_element(e, h);

    // logoutput("add_list_element_last (added: %li:%li)", e->added.tv_sec, e->added.tv_nsec);

    // logoutput("add_list_element_last: h defined %s", (h) ? "yes" : "no");

    // logoutput("add_list_element_last: count %i", (h) ? (int) h->count : 0);
    // if (h) {

	// logoutput("add_list_element_last: name %s ops defined %s", (h->name) ? h->name : "UNKNOWN", (h->ops) ? "yes" : "no");
	// if (h->ops) logoutput("add_list_element_last: ops->insert_after defined %s", (h->ops->insert_after) ? "yes" : "no");

    // }

    get_current_time(&e->added);
    (* h->ops->insert_after)(h->tail, e);
}
void add_list_element_first(struct list_header_s *h, struct list_element_s *e)
{
    init_list_element(e, h);
    get_current_time(&e->added);
    (* h->ops->insert_before)(h->head, e);
}
void add_list_element_after(struct list_header_s *h, struct list_element_s *p, struct list_element_s *e)
{
    init_list_element(e, h);
    (* h->ops->insert_after)(p, e);
}
void add_list_element_before(struct list_header_s *h, struct list_element_s *n, struct list_element_s *e)
{
    init_list_element(e, h);
    get_current_time(&e->added);
    (* h->ops->insert_before)(n, e);
}
void remove_list_element(struct list_element_s *e)
{
    struct list_header_s *h=NULL;

    // logoutput("remove_list_element (added: %li:%li)", e->added.tv_sec, e->added.tv_nsec);

    h=(e) ? e->h : NULL;
    // logoutput("remove_list_element: h defined %s", (h) ? "yes" : "no");

    // logoutput("remove_list_element: count %i", (h) ? (int) h->count : 0);
    // if (h) logoutput("remove_list_element: ops defined %s", (h->ops) ? "yes" : "no");

    if (h && h->ops) (* h->ops->delete)(e);
}
struct list_element_s *get_list_head(struct list_header_s *h, unsigned char flags)
{
    struct list_element_s *e=h->head;
    if (e && (flags & SIMPLE_LIST_FLAG_REMOVE)) (* h->ops->delete)(e);
    return e;
}

struct list_element_s *search_list_element_forw(struct list_header_s *h, int (* condition)(struct list_element_s *list, void *ptr), void *ptr)
{
    struct list_element_s *e=h->head;

    while (e) {

	if (condition(e, ptr)==0) break;
	e=e->n;

    }

    return e;
}

struct list_element_s *search_list_element_back(struct list_header_s *h, int (* condition)(struct list_element_s *list, void *ptr), void *ptr)
{
    struct list_element_s *e=h->tail;

    while(e) {

	if (condition(e, ptr)==0) break;
	e=e->p;

    }

    return e;
}

void init_list_header(struct list_header_s *h, unsigned char type, struct list_element_s *e)
{
    if (h==NULL) {

	logoutput_warning("init_list_header: header empty");
	return;

    }

    // logoutput("init_list_header: type %i", type);
    h->name=NULL;

    if (type==SIMPLE_LIST_TYPE_EMPTY) {

	// logoutput("init_list_header: EMPTY");

	h->count=0;
	h->head=NULL;
	h->tail=NULL;
	h->ops=&empty_header_ops;

    } else if (type==SIMPLE_LIST_TYPE_ONE) {

	// logoutput("init_list_header: ONE");

	h->count=1;
	h->head=e;
	h->tail=e;
	h->ops=&default_header_ops;

	set_element_ops_one(&e->ops);

    } else if (type==SIMPLE_LIST_TYPE_DEFAULT) {

	// logoutput("init_list_header: DEFAULT");

	if (e) {

	    if (e==h->head) {

		set_element_ops_default_head(&e->ops);

	    } else if (e==h->tail) {

		set_element_ops_default_tail(&e->ops);

	    } else {

		set_element_ops_default(&e->ops);

	    }

	}

	h->ops=&default_header_ops;

    } else {

	logoutput("init_list_header: type %i not reckognized", type);

    }

}

signed char list_element_is_first(struct list_element_s *e)
{
    struct list_header_s *h=e->h;
    return (h->head==e) ? 0 : -1;
}

signed char list_element_is_last(struct list_element_s *e)
{
    struct list_header_s *h=e->h;
    return (h->tail==e) ? 0 : -1;
}

struct list_element_s *get_next_element(struct list_element_s *e)
{
    return (e) ? e->n : NULL;
}

struct list_element_s *get_prev_element(struct list_element_s *e)
{
    return (e) ? e->p : NULL;
}
