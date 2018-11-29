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
#undef LOGGING
#include "logging.h"
#include "utils.h"
#include "simple-list.h"
#include "workerthreads.h"

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
    struct list_header_s 				threads;
    struct list_element_s 				*current;
    struct list_header_s				joblist;
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
    struct list_header_s *jobs=&queue->joblist;
    struct list_element_s *list=NULL;

    /* get job from joblist for this workerthreads queue */

    list=get_list_head(jobs, SIMPLE_LIST_FLAG_REMOVE);
    return (list) ? get_containing_job(list) : NULL;
}

static void process_job(void *threadarg)
{
    struct workerthread_s *thread=NULL;
    struct workerthreads_queue_s *queue=NULL;
    struct threadjob_s *threadjob=NULL;

    // logoutput("process_job: started %i", gettid());

    thread=(struct workerthread_s *) threadarg;
    if ( ! thread ) return;

    queue=thread->queue;
    thread->threadid=pthread_self();

    /* thread can be cancelled any time */

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while (1) {

	/* wait till this thread has to do some work */

	pthread_mutex_lock(&queue->mutex);

	while (queue->finish==0 && thread->job==NULL) {

	    pthread_cond_wait(&queue->cond, &queue->mutex);

	}

	// logoutput("process_job: thread %i waking up", gettid());

	if (queue->finish==1 && thread->job==NULL) {

	    /* finish every thread */

	    thread->threadid=0;
	    queue->nrthreads--;
	    remove_list_element(&thread->list);
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

	// logoutput("process_job: A");

	(* threadjob->cb) (threadjob->data);

	free(threadjob);
	thread->job=NULL;

	/* ready: 
	    - test for another job
	    - move to tail of queue */

	pthread_mutex_lock(&queue->mutex);

	// logoutput("process_job: B");

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

	// logoutput("process_job: C");

	if (list_element_is_last(&thread->list)==0) {

	    // logoutput("process_job: D1");
	    queue->current=&thread->list;

	} else {

	    /* not already latest
		move to the latest position */

	    // logoutput("process_job: D2");

	    remove_list_element(&thread->list);
	    add_list_element_last(&queue->threads, &thread->list);
	    if (queue->current==NULL) queue->current=&thread->list;

	}

	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);

    }

}

static struct workerthread_s *create_workerthread(struct workerthreads_queue_s *queue, unsigned int *error)
{
    struct workerthread_s *thread=NULL;

    logoutput("create_workerthread");

    thread=malloc(sizeof(struct workerthread_s));

    if (thread) {
	int result;

	thread->threadid=0;
	thread->queue=queue;
	init_list_element(&thread->list, NULL);
	thread->job=NULL;

	result=pthread_create(&thread->threadid, NULL, (void *) process_job, (void *) thread);

	if (result!=0) {

	    logoutput("create_workerthread: error %i:%s starting thread", result, strerror(result));

	    *error=result;
	    free(thread);
	    thread=NULL;

	}

    } else {

	logoutput("create_workerthread: error %i:%s starting thread", ENOMEM, strerror(ENOMEM));
	*error=ENOMEM;

    }

    return thread;

}

void work_workerthread(void *ptr, int timeout, void (*cb) (void *data), void *data, unsigned int *error)
{
    struct workerthreads_queue_s *queue=NULL;
    struct threadjob_s *job=NULL;
    struct list_header_s *joblist=NULL;

    // logoutput("work_workerthread");

    queue=(ptr) ? (struct workerthreads_queue_s *) ptr : &default_queue;
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
    init_list_element(&job->list, NULL);

    pthread_mutex_lock(&queue->mutex);

    /* is a thread available immediatly?
	if yes: give that thread the job and signal
    */

    if (queue->current) {
	struct workerthread_s *thread=NULL;

	// logoutput("work_workerthread: get thread from stack");

	thread=get_containing_thread(queue->current);
	thread->job=job;
	queue->current=get_next_element(&thread->list);

	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);

	// logoutput("work_workerthread: gtfs ready");

	return;

    }

    /* create a new thread? */

    if (queue->nrthreads<queue->max_nrthreads) {
	struct workerthread_s *thread=NULL;

	// logoutput("work_workerthread: create thread");

	*error=0;
	thread=create_workerthread(queue, error);

	if (thread==NULL) {

	    // logoutput("work_workerthread: unable to create new thread");
	    return;

	}

	// logoutput("work_workerthread: A");

	add_list_element_last(&queue->threads, &thread->list);

	// logoutput("work_workerthread: B");

	thread->job=job;
	queue->nrthreads++;

	// logoutput("work_workerthread: C");

	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->mutex);

	// logoutput("work_workerthread: D");

	return;

    }

    /* maximum number of threads: put job on queue */

    // logoutput("work_workerthread: put job on list");

    add_list_element_last(joblist, &job->list);

    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

}

void init_workerthreads(void *ptr)
{
    struct workerthreads_queue_s *queue=(ptr) ? (struct workerthreads_queue_s *) ptr : &default_queue;

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);

    init_list_header(&queue->threads, SIMPLE_LIST_TYPE_EMPTY, NULL);
    queue->current=NULL;

    init_list_header(&queue->joblist, SIMPLE_LIST_TYPE_EMPTY, NULL);

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
    struct list_element_s *list;

    getthread:

    pthread_mutex_lock(&queue->mutex);

    list=get_list_head(&queue->threads, SIMPLE_LIST_FLAG_REMOVE);

    if (list) {
	struct workerthread_s *thread=NULL;

	thread=get_containing_thread(list);

	if (thread->threadid>0) {

	    pthread_cancel(thread->threadid);
	    thread->threadid=0;

	}

	free(thread);

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
    struct workerthreads_queue_s *queue=(ptr) ? (struct workerthreads_queue_s *) ptr : &default_queue;

    pthread_mutex_lock(&queue->mutex);
    queue->max_nrthreads=maxnr;
    pthread_mutex_unlock(&queue->mutex);

}

unsigned get_numberthreads(void *ptr)
{
    struct workerthreads_queue_s *queue=(ptr) ? (struct workerthreads_queue_s *) ptr : &default_queue;
    return queue->nrthreads;
}

unsigned get_max_numberthreads(void *ptr)
{
    struct workerthreads_queue_s *queue=(ptr) ? (struct workerthreads_queue_s *) ptr : &default_queue;
    return queue->max_nrthreads;
}

static void start_workerthread_log(void *ptr)
{
}

void start_default_workerthreads(void *ptr)
{
    struct workerthreads_queue_s *queue=(ptr) ? (struct workerthreads_queue_s *) ptr : &default_queue;
    unsigned int error=0;

    for (unsigned int i=0; i<queue->max_nrthreads; i++) {

	work_workerthread(ptr, 0, start_workerthread_log, NULL, &error);

    }

}
