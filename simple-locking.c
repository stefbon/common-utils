/*

  2018 Stef Bon <stefbon@gmail.com>

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
#include "simple-locking.h"

int init_simple_locking(struct simple_locking_s *locking)
{
    pthread_mutex_init(&locking->mutex, NULL);
    pthread_cond_init(&locking->cond, NULL);
    locking->readlock.first=NULL;
    locking->readlock.last=NULL;
    locking->writelock.first=NULL;
    locking->writelock.last=NULL;
    locking->readers=0;
    locking->writers=0;
}

void clear_simple_locking(struct simple_locking_s *locking)
{
    pthread_mutex_destroy(&locking->mutex);
    pthread_cond_destroy(&locking->cond);
    struct list_element_s *list=get_list_head(&locking->readlocks.first, &locking->readlocks.last);

    while (list) {
	struct simple_lock_s *lock=(struct simple_lock_s *) (((char *) element) - offsetof(struct simple_lock_s, list));

	free(lock);
	list=get_list_head(&locking->readlocks.first, &locking->readlocks.last);

    }

    list=get_list_head(&locking->writelocks.first, &locking->writelocks.last);

    while (list) {
	struct simple_lock_s *lock=(struct simple_lock_s *) (((char *) element) - offsetof(struct simple_lock_s, list));

	free(lock);
	list=get_list_head(&locking->writelocks.first, &locking->writelocks.last);

    }

}

int simple_readlock(struct simple_locking_s *locking)
{
    struct simple_lock_s *rlock=NULL;

    rlock=malloc(sizeof(struct simple_lock_s));
    if (rlock==NULL) return -1;
    rlock->thread=pthread_self();
    rlock->list.next=NULL;
    rlock->list.prev=NULL;

    pthread_mutex_lock(&locking->mutex);
    while (locking->writers>0) pthread_cond_wait(&locking->cond, &locking->mutex);
    add_list_element_last(&locking->readlocks.first, &locking->readlocks.last, &rlock->list);
    locking->readers++;

    pthread_cond_broadcast(&locking->cond);
    pthread_mutex_unlock(&locking->mutex);
    return 0;

}

static int match_simple_lock(struct list_element_s *element, void *ptr)
{
    pthread_t *thread=(pthread_t *) ptr;
    struct simple_lock_s *lock=(struct simple_lock_s *) (((char *) element) - offsetof(struct simple_lock_s, list));

    if (lock->thread==*thread) return 0;
    return -1;
}

int simple_readunlock(struct simple_locking_s *locking)
{
    struct simple_lock_s *rlock=NULL;
    pthread_t thread=pthread_self();

    pthread_mutex_lock(&locking->mutex);

    rlock=search_list_element_forw(locking->readlocks.first, locking->readlocks.last, match_simple_lock, (void *) &thread);
    if (rlock==NULL) {

	pthread_mutex_unlock(&locking->mutex);
	return -1;

    }

    remove_list_element(&locking->readlocks.first, &locking->readlocks.last, &rlock->list);
    free(rlock);
    locking->readers--;

    pthread_cond_broadcast(&locking->cond);
    pthread_mutex_unlock(&locking->mutex);

    return 0;
}

int simple_writelock(struct simple_locking_s *locking)
    struct simple_lock_s *wlock=NULL;

    wlock=malloc(sizeof(struct simple_lock_s));
    if (wlock==NULL) return -1;
    wlock->thread=pthread_self();
    wlock->list.next=NULL;
    wlock->list.prev=NULL;

    pthread_mutex_lock(&locking->mutex);
    add_list_element_last(&locking->writelocks.first, &locking->writelocks.last, &wlock->list);
    locking->writers++;
    pthread_cond_broadcast(&locking->cond);

    /* only get the write lock when there are no readers and this wlock is the first */

    while (locking->readers>0 && locking->writelocks.first!=wlock->list) pthread_cond_wait(&locking->cond, &locking->mutex);

    pthread_cond_broadcast(&locking->cond);
    pthread_mutex_unlock(&locking->mutex);
    return 0;

}

int simple_writeunlock(struct simple_locking_s *locking)
{
    struct simple_lock_s *wlock=NULL;
    pthread_t thread=pthread_self();

    pthread_mutex_lock(&locking->mutex);

    wlock=search_list_element_forw(locking->writelocks.first, &locking->writelocks.last, match_simple_lock, (void *) &thread);
    if (wlock==NULL) {

	pthread_mutex_unlock(&locking->mutex);
	return -1;

    }

    remove_list_element(&locking->writelocks.first, &locking->writelocks.last, &rlock->list);
    free(wlock);
    locking->writers--;

    pthread_cond_broadcast(&locking->cond);
    pthread_mutex_unlock(&locking->mutex);

    return 0;
}

int simple_upgrade_readlock(struct simple_locking_s *locking)
{
    struct simple_lock_s *rlock=NULL;
    pthread_t thread=pthread_self();

    pthread_mutex_lock(&locking->mutex);

    rlock=search_list_element_forw(locking->readlocks.first, &locking->readlocks.last, match_simple_lock, (void *) &thread);
    if (rlock==NULL) {

	pthread_mutex_unlock(&locking->mutex);
	return -1;

    }

    remove_list_element(&locking->readlocks.first, &locking->readlocks.last, &rlock->list);
    locking->readers--;

    add_list_element_last(&locking->writelocks.first, &locking->writelocks.last, &rlock->list);
    locking->writers++;
    pthread_cond_broadcast(&locking->cond);

    /* only get the write lock when there are no readers and this lock is the first */

    while (locking->readers>0 && locking->writelocks.first!=rlock->list) pthread_cond_wait(&locking->cond, &locking->mutex);

    pthread_cond_broadcast(&locking->cond);
    pthread_mutex_unlock(&locking->mutex);
    return 0;

}
