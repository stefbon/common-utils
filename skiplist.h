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

#ifndef SB_COMMON_UTILS_SKIPLIST_H
#define SB_COMMON_UTILS_SKIPLIST_H

#define _SKIPLIST_PROB					4

/* lock set on an entire dirnode when exact match is found by delete */
#define _DIRNODE_RMLOCK					1

/* lock set in a lane/region to inserte/remove an entry */
#define _DIRNODE_WRITELOCK				4

/* lock set in a lane/region to read */
#define _DIRNODE_READLOCK				8

#define _SKIPLIST_READLOCK				1
#define _SKIPLIST_PREEXCLLOCK				2
#define _SKIPLIST_EXCLLOCK				3
#define _SKIPLIST_MUTEX					4

#define _DIRNODE_TYPE_INIT				0
#define _DIRNODE_TYPE_BETWEEN				1
#define _DIRNODE_TYPE_START				2
#define _DIRNODE_TYPE_FREE				4


struct lock_struct {
    pthread_mutex_t			mutex;
    pthread_cond_t			cond;
    unsigned char 			lock;
};

struct dirnode_junction_struct {
    struct dirnode_struct 		*next;
    struct dirnode_struct 		*prev;
    unsigned  				count;
    unsigned char			lock;
};

struct dirnode_struct {
    void 				*data;
    unsigned char			type;
    unsigned char			lock;
    unsigned short			level;
    struct dirnode_junction_struct	*junction;
};

struct skiplist_struct;

struct slops_struct {
    void 					*(* next) (void *data);
    void 					*(* prev) (void *data);
    int 					(* compare)(void *a, void *b);
    void 					(* insert_before) (void *data, void *before, struct skiplist_struct *sl);
    void 					(* insert_after) (void *data, void *after, struct skiplist_struct *sl);
    void 					(* delete) (void *data, struct skiplist_struct *sl);
    void					*(* create_rlock)(struct skiplist_struct *sl);
    void					*(* create_wlock)(struct skiplist_struct *sl);
    int						(* lock) (struct skiplist_struct *sl, void *ptr);
    int						(* unlock) (struct skiplist_struct *sl, void *ptr);
    int						(* upgradelock) (struct skiplist_struct *sl, void *ptr);
    int						(* prelock) (struct skiplist_struct *sl, void *ptr);
    unsigned int 				(* count) (struct skiplist_struct *sl);
    void 					*(* first) (struct skiplist_struct *sl);
    void 					*(* last) (struct skiplist_struct *sl);
};

struct skiplist_struct {
    struct dirnode_struct		*dirnode;
    struct slops_struct			ops;
    unsigned 				prob;
    pthread_mutex_t			mutex;
    pthread_cond_t			cond;
};

struct vector_lane_struct {
    struct dirnode_struct *dirnode;
    unsigned char lockset;
    unsigned int step;
};

struct vector_dirnode_struct {
    unsigned short maxlevel;
    unsigned short minlevel;
    unsigned char lockset;
    struct vector_lane_struct *lane;
};

/* prototypes */

void init_vector(struct vector_dirnode_struct *vector);
int  add_vector_lanes(struct vector_dirnode_struct *vector, unsigned short level, unsigned int *error);
void destroy_vector_lanes(struct vector_dirnode_struct *vector);

void unlock_dirnode_vector(struct vector_dirnode_struct *vector);

int init_skiplist(struct skiplist_struct *sl, unsigned char prob,
		    void *(* next)(void *data), void *(*prev)(void *data),
		    int (* compare) (void *a, void *b),
		    void (* insert_before) (void *data, void *before, struct skiplist_struct *sl),
		    void (* insert_after) (void *data, void *after, struct skiplist_struct *sl),
		    void (* delete) (void *data, struct skiplist_struct *sl),
		    void *(* create_rlock)(struct skiplist_struct *sl),
		    void *(* create_wlock)(struct skiplist_struct *sl),
		    int (* lock) (struct skiplist_struct *sl, void *ptr),
		    int (* unlock) (struct skiplist_struct *sl, void *ptr),
		    int (* upgradelock) (struct skiplist_struct *sl, void *ptr),
		    int (* prelock) (struct skiplist_struct *sl, void *ptr),
		    unsigned int (* count) (struct skiplist_struct *sl),
		    void *(* first) (struct skiplist_struct *sl),
		    void *(* last) (struct skiplist_struct *sl),
		    unsigned int *error);

struct skiplist_struct *create_skiplist(unsigned int *error);

void clear_skiplist(struct skiplist_struct *sl);
void destroy_lock_skiplist(struct skiplist_struct *sl);
void destroy_skiplist(struct skiplist_struct *sl);

struct dirnode_struct *create_dirnode(unsigned short level);
void destroy_dirnode(struct dirnode_struct *dirnode);

struct dirnode_struct *create_head_dirnode(unsigned short level);
unsigned short resize_head_dirnode(struct dirnode_struct *dirnode, unsigned short level);

#endif
