/*
  2010, 2011, 2012 Stef Bon <stefbon@gmail.com>

  Add the fuse channel to the mainloop, init threads and process any fuse event.

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

#include "global-defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <inttypes.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <pthread.h>
#include "logging.h"
#include "utils.h"
#include "simple-list.h"
#include "workerthreads.h"

struct list_head_s {
    struct list_element_s				*head;
    struct list_element_s				*tail;
};

struct threadjob_s {
    void 						(*cb) (void *data);
    void 						*data;
    struct list_element_s 				list;
};

struct workerthread_s {
    pthread_t 						threadid;
    struct workerthreads_queue_s			*queue;
    struct threadjob_s					*job;
    struct list_element_s				list;
};

struct workerthreads_queue_s {
    struct list_head_s 					threads;
    struct list_element_s 				*current;
    struct list_head_s					joblist;
    pthread_mutex_t 					mutex;
    pthread_cond_t 					cond;
    unsigned int 					nrthreads;
    unsigned int 					max_nrthreads;
    unsigned char 					finish;
};

static struct workerthreads_queue_s default_queue;

/* get the workerthread when list is known
    notice: list may not be null
*/
static struct workerthread_s *get_containing_thread(struct list_element_s *list)
{
    return (struct workerthread_s *) ( ((char *) list) - offsetof(struct workerthread_s, list));
}

static struct threadjob_s *get_containing_job(struct list_element_s *list)
{
    return (struct threadjob_s *) ( ((char *) list) - offsetof(struct threadjob_s, list));
}

static struct threadjob_s *get_next_job(struct workerthreads_queue_s *queue)
{
    struct list_head_s *jobs=&queue->joblist;

    /* get job from joblist for this workerthreads queue */

    if (jobs->head) {
	struct threadjob_s *job=get_containing_job(jobs->head);

	jobs->head=job->list.next;

	if (jobs->head) {
	    struct list_element_s *next=jobs->head;

	    next->prev=NULL;

	} else {

	    jobs->tail=NULL;

	}

	job->list.next=NULL;
	return job;

    }

    return NULL;
}

static void process_job(void *threadarg)
{
    struct workerthread_s *thread=NULL;
    struct workerthreads_queue_s *queue=NULL;
    struct threadjob_s *threadjob=NULL;

    thread=(struct workerthread_s *) threadarg;
    if ( ! thread ) return;

    queue=thread->queue;
    thread->threadid=pthread_self();

    logoutput("process_job: started %li/%i", (unsigned long) thread->threadid, gettid());

    /* thread can be cancelled any time */

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while (1) {

	/* wait till this thread has to do some work */

	pthread_mutex_lock(&queue->mutex);

	while (queue->finish==0 && thread->job==NULL) {

	    pthread_cond_wait(&queue->cond, &queue->mutex);

	}

	if (queue->finish==1 && ! thread->job) {

	    /* finish every thread */

	    thread->threadid=0;
	    queue->nrthreads--;
	    remove_list_element(&queue->threads.head, &queue->threads.tail, &thread->list);
	    pthread_cond_broadcast(&queue->cond);
	    pthread_mutex_unlock(&queue->mutex);
	    free(thread);
	    break;

	}

	/* there must be a job for this thread: otherwise it wouldn't be woken up */

	threadjob=thread->job;
	thread->job=NULL;

	pthread_mutex_unlock(&queue->mutex);

	processjob:

	(* threadjob->cb) (threadjob->data);

	free(threadjob);
	thread->job=NULL;

	/* ready: 
	    - test for another job
	    - move to tail of queue */

	pthread_mutex_lock(&queue->mutex);

	threadjob=get_next_job(queue);
	if (threadjob) {

	    pthread_mutex_unlock(&queue->mutex);
	    goto processjob;

	}

	/* move the thread in the list to the last postition
	   - left are the threads with a job
	   - right are the thread without a job
	   - current is pointing to the first waiting thread (maybe NULL)
	    general case:

	    t (job) <-> t <-> t (job) <-> t <-> t <-> t
	                |                 |           |
                   thread ready        current      last ^
			|                                |
			----------------------------------

	    special cases:
	    - ready thread is already latest
	    - current maybe NULL (all threads are busy)

	*/

	if (queue->threads.tail == &thread->list) {

	    queue->current=&thread->list;

	} else {
	    struct list_element_s *prev=NULL;

	    /* not already latest
		move to the latest position */

	    if (queue->threads.head==&thread->list) {
		struct list_element_s *next=thread->list.next;

		queue->threads.head=next;
		next->prev=NULL; /* there is **always** a next since thread is not the latest */

	    } else {
		struct list_element_s *next=thread->list.next;

		prev=thread->list.prev;
		prev->next=next; /* there is **always** a prev since thread is not the first */
		next->prev=prev; /* there is **always** a next since thread is not the latest */

	    }

	    thread->list.next=NULL;
	    thread->list.prev=NULL;

	    /* append after latest */

	    prev=queue->threads.tail;
	    prev->next=&thread->list;
	    thread->list.prev=prev;
	    queue->threads.tail=&thread->list;

	    if (!queue->current) queue->current=&thread->list;

	}

	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);

    }

}

static struct workerthread_s *create_workerthread(struct workerthreads_queue_s *queue, unsigned int *error)
{
    struct workerthread_s *thread=NULL;

    thread=malloc(sizeof(struct workerthread_s));

    if (thread) {
	int result;

	thread->threadid=0;
	thread->queue=queue;
	thread->list.next=NULL;
	thread->list.prev=NULL;
	thread->job=NULL;

	result=pthread_create(&thread->threadid, NULL, (void *) process_job, (void *) thread);

	if (result!=0) {

	    logoutput("create_workerthread: error %i:%s starting thread", result, strerror(result));

	    *error=result;
	    free(thread);
	    thread=NULL;

	}

    } else {

	*error=ENOMEM;

    }

    return thread;

}

void work_workerthread(void *ptr, int timeout, void (*cb) (void *data), void *data, unsigned int *error)
{
    struct workerthreads_queue_s *queue=(struct workerthreads_queue_s *) ptr;
    struct threadjob_s *job=NULL;
    struct list_head_s *joblist=NULL;

    if (queue==NULL) queue=&default_queue;
    joblist=&queue->joblist;

    pthread_mutex_lock(&queue->mutex);

    if (queue->finish==1) {

	pthread_mutex_unlock(&queue->mutex);
	*error=EPERM;
	return;

    }

    pthread_mutex_unlock(&queue->mutex);

    job=malloc(sizeof(struct threadjob_s));

    if (! job) {

	*error=ENOMEM;
	return;

    }

    job->cb=cb;
    job->data=data;
    job->list.next=NULL;
    job->list.prev=NULL;

    pthread_mutex_lock(&queue->mutex);

    /* is a thread available immediatly?
	if yes: give that thread the job and signal
    */

    if (queue->current) {
	struct workerthread_s *thread=NULL;

	thread=get_containing_thread(queue->current);
	thread->job=job;
	queue->current=thread->list.next;

	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);
	return;

    }

    /* create a new thread? */

    if (queue->nrthreads<queue->max_nrthreads) {
	struct workerthread_s *thread=NULL;

	*error=0;

	thread=create_workerthread(queue, error);

	if (!thread) {

	    logoutput("work_workerthread: unable to create new thread");
	    return;

	}

	if (queue->threads.tail) {
	    struct workerthread_s *prev=get_containing_thread(queue->threads.tail);

	    prev->list.next=&thread->list;
	    thread->list.prev=&prev->list;
	    queue->threads.tail=&thread->list;

	} else {

	    queue->threads.head=&thread->list;
	    queue->threads.tail=&thread->list;

	}

	thread->job=job;
	queue->nrthreads++;

	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);
	return;

    }

    /* maximum number of threads: put job on queue */

    if (joblist->tail) {
	struct threadjob_s *prev=get_containing_job(joblist->tail);

	prev->list.next=&job->list;
	job->list.prev=&prev->list;

	joblist->tail=&job->list;

    } else {

	joblist->head=&job->list;
	joblist->tail=&job->list;

    }

    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

}

void init_workerthreads(void *ptr)
{
    struct workerthreads_queue_s *queue=(struct workerthreads_queue_s *) ptr;

    if (queue==NULL) queue=&default_queue;

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);

    queue->threads.head=NULL;
    queue->threads.tail=NULL;
    queue->current=NULL;

    queue->joblist.head=NULL;
    queue->joblist.tail=NULL;

    queue->nrthreads=0;
    queue->max_nrthreads=6;
    queue->finish=0;

}

void stop_workerthreads(void *ptr)
{
    struct workerthreads_queue_s *queue=(struct workerthreads_queue_s *) ptr;
    if (queue==NULL) queue=&default_queue;

    pthread_mutex_lock(&queue->mutex);
    queue->finish=1;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}


static void cancel_workerthreads(struct workerthreads_queue_s *queue)
{

    getthread:

    pthread_mutex_lock(&queue->mutex);

    if (queue->threads.head) {
	struct workerthread_s *thread=NULL;
	struct list_element_s *list;

	list=queue->threads.head;

	if (list) {

	    remove_list_element(&queue->threads.head, &queue->threads.tail, list);
	    thread=get_containing_thread(list);

	    if (thread->threadid>0) {

		pthread_cancel(thread->threadid);
		thread->threadid=0;

	    }

	    free(thread);

	}

	pthread_mutex_unlock(&queue->mutex);
	goto getthread;

    }

    pthread_mutex_unlock(&queue->mutex);


}

void terminate_workerthreads(void *ptr, unsigned int timeout)
{
    struct workerthreads_queue_s *queue=(struct workerthreads_queue_s *) ptr;
    unsigned int nrthreads=0;

    if (queue==NULL) queue=&default_queue;

    pthread_mutex_lock(&queue->mutex);
    nrthreads=queue->nrthreads;
    queue->finish=1;
    pthread_cond_broadcast(&queue->cond);

    if (nrthreads==0) goto finish;
    logoutput("terminate_workerthreads: %i threads", nrthreads);

    if (timeout==0) {

	while(queue->nrthreads>0) {

	    pthread_cond_wait(&queue->cond, &queue->mutex);

	    if (queue->nrthreads<nrthreads) {

		logoutput("terminate_workerthreads: %i threads", queue->nrthreads);
		nrthreads=queue->nrthreads;

	    }

	}

    } else {
	struct timespec expire;
	int result=0;

	get_current_time(&expire);
	expire.tv_sec+=timeout;

	while(queue->nrthreads>0) {

	    result=pthread_cond_timedwait(&queue->cond, &queue->mutex, &expire);

	    if (result==ETIMEDOUT) {

		logoutput("terminate_workerthreads: timeout, %i threads", queue->nrthreads);
		break;

	    } else if (queue->nrthreads<nrthreads) {

		logoutput("terminate_workerthreads: %i threads", queue->nrthreads);
		nrthreads=queue->nrthreads;

	    }

	}

    }

    pthread_mutex_unlock(&queue->mutex);

    cancel_workerthreads(queue);

    finish:

    if (queue->joblist.head) {
	struct threadjob_s *job=get_next_job(queue);

	while(job) {

	    free(job);
	    get_next_job(queue);

	}

    }

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);

}

void set_max_numberthreads(void *ptr, unsigned maxnr)
{
    struct workerthreads_queue_s *queue=(struct workerthreads_queue_s *) ptr;
    if (queue==NULL) queue=&default_queue;
    pthread_mutex_lock(&queue->mutex);
    queue->max_nrthreads=maxnr;
    pthread_mutex_unlock(&queue->mutex);
}

unsigned get_numberthreads(void *ptr)
{
    struct workerthreads_queue_s *queue=(struct workerthreads_queue_s *) ptr;
    if (queue==NULL) queue=&default_queue;
    return queue->nrthreads;
}

unsigned get_max_numberthreads(void *ptr)
{
    struct workerthreads_queue_s *queue=(struct workerthreads_queue_s *) ptr;
    if (queue==NULL) queue=&default_queue;
    return queue->max_nrthreads;
}
