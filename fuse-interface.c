/*
  2010, 2011, 2012, 2013, 2014 Stef Bon <stefbon@gmail.com>

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
#include <sys/stat.h>
#include <sys/param.h>
#include <pthread.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <dirent.h>
#include <pwd.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "logging.h"
#include "pathinfo.h"
#include "utils.h"
#include "beventloop.h"
#include "beventloop-xdata.h"
#include "workerthreads.h"

#include "fuse-dentry.h"
#include "workspace-interface.h"
#include "fuse-interface.h"

#define FUSEPARAM_STATUS_CONNECTING				1
#define FUSEPARAM_STATUS_CONNECTED				2
#define FUSEPARAM_STATUS_DISCONNECTING				4
#define FUSEPARAM_STATUS_DISCONNECTED				8

#define FUSEPARAM_STATUS_DISCONNECT				( FUSEPARAM_STATUS_DISCONNECTING | FUSEPARAM_STATUS_DISCONNECTED )

#define FUSEPARAM_QUEUE_HASHSIZE				128

typedef void (* fuse_cb_t)(struct fuse_request_s *request);

/* index for queue */

struct double_index_s {
    struct fuse_request_s			*request;
    struct double_index_s			*next;
    struct double_index_s			*prev;
};

/* queue of incoming requests */

struct fusequeue_s {
    struct double_index_s			*first;
    struct double_index_s			*last;
    pthread_mutex_t				mutex;
};

/* actual connection to the VFS/kernel */

struct fuseparam_s {
    size_t 					size;
    size_t					read;
    unsigned char				status;
    struct timespec				attr_timeout;
    struct timespec				entry_timeout;
    struct timespec				negative_timeout;
    struct context_interface_s			*interface;
    struct fs_connection_s			connection;
    unsigned int				size_cb;
    fuse_cb_t					fuse_cb[48]; /* depends on version protocol; at this moment max opcode is 47 */
    mode_t					(* get_masked_perm)(mode_t perm, mode_t mask);
    pthread_mutex_t				mutex;
    pthread_cond_t				cond;
    struct fusequeue_s				queue;
    char					buffer[];
};

static struct double_index_s			*datahash[FUSEPARAM_QUEUE_HASHSIZE];
static unsigned char				hashinit=0;
static pthread_mutex_t				datahash_mutex=PTHREAD_MUTEX_INITIALIZER;

static unsigned int				size_in_header=sizeof(struct fuse_in_header);
static unsigned int				size_out_header=sizeof(struct fuse_out_header);

void notify_VFS_delete(void *ptr, uint64_t pino, uint64_t ino, char *name, unsigned int len)
{

#if FUSE_VERSION >= 29
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    struct fs_connection_s *conn=&fuseparam->connection;
    struct fuse_ops_s *fops=conn->io.fuse.fops;
    ssize_t alreadywritten=0;
    struct iovec iov[3];
    struct fuse_out_header oh;
    struct fuse_notify_delete_out out;

    oh.len=size_out_header + sizeof(struct fuse_notify_delete_out) + len;
    oh.error=FUSE_NOTIFY_DELETE;
    oh.unique=0;

    out.parent=pino;
    out.child=ino;
    out.namelen=len;
    out.padding=0;

    iov[0].iov_base=(void *) &oh;
    iov[0].iov_len=size_out_header;

    iov[1].iov_base=(void *) &out;
    iov[1].iov_len=sizeof(struct fuse_notify_delete_out);

    iov[2].iov_base=(void *) name;
    iov[2].iov_len=len;

    replyVFS:

    alreadywritten+=(* fops->writev)(&conn->io.fuse, iov, 3);

#endif


}

void notify_VFS_create(void *ptr, uint64_t pino, char *name)
{

#if FUSE_VERSION >= 29


#endif

}

void notify_kernel_change(void *ptr, uint64_t ino, uint32_t mask)
{

    /* TODO: */

}

size_t add_direntry_buffer(void *ptr, char *buffer, size_t size, off_t offset, struct name_s *xname, struct stat *st, unsigned int *error)
{
    size_t dirent_size=offsetof(struct fuse_dirent, name) + xname->len;
    size_t dirent_size_alligned=(((dirent_size) + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) - 1));

    *error=0;

    if ( dirent_size_alligned < size) {
	struct fuse_dirent *dirent=(struct fuse_dirent *) buffer;

	memset(buffer, 0, dirent_size_alligned); /* to be sure, the buffer should be zerod already */

	dirent->ino=st->st_ino;
	dirent->off=offset;
	dirent->namelen=xname->len;
	dirent->type=(st->st_mode>0) ? (st->st_mode & S_IFMT) >> 12 : DT_UNKNOWN;
	memcpy(dirent->name, xname->name, xname->len);

    } else {

	*error=ENOBUFS;

    }

    return dirent_size_alligned;

}

size_t add_direntry_plus_buffer(void *ptr, char *buffer, size_t size, off_t offset, struct name_s *xname, struct stat *st, unsigned int *error)
{
    size_t dirent_size=offsetof(struct fuse_direntplus, dirent.name) + xname->len;
    size_t dirent_size_alligned=(((dirent_size) + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) - 1));

    *error=0;

    if ( dirent_size_alligned < size) {
	struct fuse_direntplus *direntplus=(struct fuse_direntplus *) buffer;
	struct timespec *attr_timeout=get_fuse_interface_attr_timeout(ptr);
	struct timespec *entry_timeout=get_fuse_interface_entry_timeout(ptr);

	memset(buffer, 0, dirent_size_alligned); /* to be sure, the buffer should be zerod already */

	direntplus->dirent.ino=st->st_ino;
	direntplus->dirent.off=offset;
	direntplus->dirent.namelen=xname->len;
	direntplus->dirent.type=(st->st_mode>0) ? (st->st_mode & S_IFMT) >> 12 : DT_UNKNOWN;
	memcpy(direntplus->dirent.name, xname->name, xname->len);

	direntplus->entry_out.nodeid=st->st_ino;
	direntplus->entry_out.generation=0; /* ???? */

	direntplus->entry_out.entry_valid=entry_timeout->tv_sec;
	direntplus->entry_out.entry_valid_nsec=entry_timeout->tv_nsec;
	direntplus->entry_out.attr_valid=attr_timeout->tv_sec;
	direntplus->entry_out.attr_valid_nsec=attr_timeout->tv_nsec;

	direntplus->entry_out.attr.ino=st->st_ino;
	direntplus->entry_out.attr.size=st->st_size;
	direntplus->entry_out.attr.blksize=_DEFAULT_BLOCKSIZE;
	direntplus->entry_out.attr.blocks=st->st_size / _DEFAULT_BLOCKSIZE + (st->st_size % _DEFAULT_BLOCKSIZE == 0) ? 0 : 1;

	direntplus->entry_out.attr.atime=(uint64_t) st->st_atim.tv_sec;
	direntplus->entry_out.attr.atimensec=(uint64_t) st->st_atim.tv_nsec;
	direntplus->entry_out.attr.mtime=(uint64_t) st->st_mtim.tv_sec;
	direntplus->entry_out.attr.mtimensec=(uint64_t) st->st_mtim.tv_nsec;
	direntplus->entry_out.attr.ctime=(uint64_t) st->st_ctim.tv_sec;
	direntplus->entry_out.attr.ctimensec=(uint64_t) st->st_ctim.tv_nsec;

	direntplus->entry_out.attr.mode=st->st_mode;
	direntplus->entry_out.attr.nlink=st->st_nlink;
	direntplus->entry_out.attr.uid=st->st_uid;
	direntplus->entry_out.attr.gid=st->st_gid;
	direntplus->entry_out.attr.rdev=0;

	direntplus->entry_out.attr.padding=0;

    } else {

	*error=ENOBUFS;

    }

    return dirent_size_alligned;

}

#ifdef FUSE_DO_FSNOTIFY
void notify_VFS_fsnotify(void *ptr, uint64_t ino, uint32_t mask)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    struct fs_connection_s *conn=&fuseparam->connection;
    struct fuse_ops_s *fops=conn->io.fuse.fops;
    ssize_t alreadywritten=0;
    struct iovec iov[2];
    struct fuse_out_header oh;
    struct fuse_notify_fsnotify_out fsnotify_out;

    logoutput_notice("notify_VFS_fsnotify");

    oh.len=size_out_header + sizeof(struct fuse_notify_fsnotify_out);
    oh.error=FUSE_NOTIFY_FSNOTIFY;
    oh.unique=0;

    fsnotify_out.parent=ino;
    fsnotify_out.mask=mask;
    fsnotify_out.namelen=0;
    fsnotify_out.padding=0;

    iov[0].iov_base=&oh;
    iov[0].iov_len=size_out_header;

    iov[1].iov_base=&fsnotify_out;
    iov[1].iov_len=sizeof(struct fuse_notify_fsnotify_out);

    replyVFS:

    alreadywritten+=(* fops->writev)(&conn->io.fuse, iov, 2);

}

void notify_VFS_fsnotify_child(void *ptr, uint64_t ino, uint32_t mask, struct name_s *xname)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    struct fs_connection_s *conn=&fuseparam->connection;
    struct fuse_ops_s *fops=conn->io.fuse.fops;
    ssize_t alreadywritten=0;
    struct iovec iov[3];
    struct fuse_out_header oh;
    struct fuse_notify_fsnotify_out fsnotify_out;

    logoutput_notice("notify_VFS_fsnotify_child");

    oh.len=size_out_header + sizeof(struct fuse_notify_fsnotify_out) + xname->len;
    oh.error=FUSE_NOTIFY_FSNOTIFY;
    oh.unique=0;

    fsnotify_out.parent=ino;
    fsnotify_out.mask=mask;
    fsnotify_out.namelen=xname->len;
    fsnotify_out.padding=0;

    iov[0].iov_base=&oh;
    iov[0].iov_len=size_out_header;

    iov[1].iov_base=&fsnotify_out;
    iov[1].iov_len=sizeof(struct fuse_notify_fsnotify_out);

    iov[2].iov_base=xname->name;
    iov[2].iov_len=xname->len;

    replyVFS:

    alreadywritten+=(* fops->writev)(&conn->io.fuse, iov, 3);

}

#else

void notify_VFS_fsnotify(void *ptr, uint64_t ino, uint32_t mask)
{
}
void notify_VFS_fsnotify_child(void *ptr, uint64_t ino, uint32_t mask, struct name_s *xname)
{
}

#endif

void reply_VFS_data(struct fuse_request_s *request, char *buffer, size_t size)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) request->interface->ptr;
    struct fs_connection_s *conn=&fuseparam->connection;
    struct fuse_ops_s *fops=conn->io.fuse.fops;
    ssize_t alreadywritten=0;
    struct iovec iov[2];
    struct fuse_out_header oh;

    oh.len=size_out_header + size;
    oh.error=0;
    oh.unique=request->unique;

    iov[0].iov_base=&oh;
    iov[0].iov_len=size_out_header;

    iov[1].iov_base=buffer;
    iov[1].iov_len=size;

    replyVFS:

    alreadywritten+=(* fops->writev)(&conn->io.fuse, iov, 2);
}

void reply_VFS_error(struct fuse_request_s *request, unsigned int error)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) request->interface->ptr;
    struct fs_connection_s *conn=&fuseparam->connection;
    struct fuse_ops_s *fops=conn->io.fuse.fops;
    ssize_t alreadywritten=0;
    struct fuse_out_header oh;
    struct iovec iov[1];

    oh.len=size_out_header;
    oh.error=-error;
    oh.unique=request->unique;

    iov[0].iov_base=&oh;
    iov[0].iov_len=oh.len;

    replyVFS:

    alreadywritten+=(* fops->writev)(&conn->io.fuse, iov, 1);
}

void reply_VFS_nosys(struct fuse_request_s *request)
{
    reply_VFS_error(request, ENOSYS);
}

void reply_VFS_xattr(struct fuse_request_s *request, size_t size)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) request->interface->ptr;
    struct fuse_getxattr_out getxattr_out;

    getxattr_out.size=size;
    getxattr_out.padding=0;

    reply_VFS_data(request, (char *) &getxattr_out, sizeof(getxattr_out));

}

static void do_reply_nosys(struct fuse_request_s *request)
{
    reply_VFS_nosys(request);
}

static void do_noreply(struct fuse_request_s *request)
{
}

static void do_init(struct fuse_request_s *request)
{
    struct fuse_init_in *init_in=(struct fuse_init_in *) request->buffer;

    if (init_in->major<7) {

	logoutput("do_init: unsupported kernel protocol version");
	reply_VFS_error(request, EPROTO);
	return;

    } else {
	struct fuse_init_out init_out;

	memset(&init_out, 0, sizeof(struct fuse_init_out));

	init_out.major=FUSE_KERNEL_VERSION;
	init_out.minor=FUSE_KERNEL_MINOR_VERSION;
	init_out.flags=0;

	if (init_in->major>7) {

	    reply_VFS_data(request, (char *) &init_out, sizeof(init_out));
	    return;

	} else {

	    init_out.max_readahead = init_in->max_readahead;
	    init_out.max_write = 4096; /* 4K */
	    init_out.max_background=(1 << 16) - 1;
	    init_out.congestion_threshold=(3 * init_out.max_background) / 4;
	    reply_VFS_data(request, (char *) &init_out, sizeof(init_out));

	}

    }

}

void register_fuse_function(void *ptr, uint32_t opcode, void (* func) (struct fuse_request_s *r))
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;

    if (opcode>0 && opcode<fuseparam->size_cb) {

	fuseparam->fuse_cb[opcode]=func;

    } else {

	logoutput("register_fuse_function: error opcode %i out of range", opcode);

    }

}

static void _add_datahash(struct double_index_s *index, uint64_t unique)
{
    unsigned int hash=unique % FUSEPARAM_QUEUE_HASHSIZE;

    pthread_mutex_lock(&datahash_mutex);

    index->next=NULL;
    index->prev=NULL;

    if (datahash[hash]) {

	index->next=datahash[hash];
	datahash[hash]->prev=index;

    }

    datahash[hash]=index;

    pthread_mutex_unlock(&datahash_mutex);

}

static unsigned char signal_request_common(void *ptr, uint64_t unique, unsigned int flag, unsigned int error)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    unsigned int hash=unique % FUSEPARAM_QUEUE_HASHSIZE;
    struct double_index_s *index=NULL;
    struct fuse_request_s *request=NULL;
    unsigned char signal=0;

    pthread_mutex_lock(&datahash_mutex);
    index=datahash[hash];

    while(index) {

	request=index->request;

	if (request->interface==fuseparam->interface && request->unique==unique) {

	    pthread_mutex_lock(&fuseparam->mutex);
	    request->flags|=flag;
	    request->error=error;
	    pthread_cond_broadcast(&fuseparam->cond);
	    pthread_mutex_unlock(&fuseparam->mutex);
	    signal=1;
	    break;

	}

	index=index->next;

    }

    pthread_mutex_unlock(&datahash_mutex);
    return signal;

}
/* signal any thread a request is interrupted
    called by fuse when receiving an interrupt message*/
unsigned char set_request_interrupted(void *ptr, uint64_t unique)
{
    return signal_request_common(ptr, unique, FUSEDATA_FLAG_INTERRUPTED, EINTR);
}

/* signal any thread a response is received for a request
    called when receiving a reply from the backend*/
unsigned char signal_request_response(void *ptr, uint64_t unique)
{
    return signal_request_common(ptr, unique, FUSEDATA_FLAG_RESPONSE, 0);
}

/* signal any thread an error occurred when processing a request */
unsigned char signal_request_error(void *ptr, uint64_t unique, unsigned int error)
{
    return signal_request_common(ptr, unique, FUSEDATA_FLAG_ERROR, error);
}
static void signal_fuse_interface_common(struct fuseparam_s *fuseparam, unsigned char status)
{
    pthread_mutex_lock(&fuseparam->mutex);
    fuseparam->status |= status;
    pthread_cond_broadcast(&fuseparam->cond);
    pthread_mutex_unlock(&fuseparam->mutex);
}

unsigned char wait_service_response(void *ptr, struct fuse_request_s *request, struct timespec *timeout)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    struct timespec expire;
    int result=0;

    get_current_time(&expire);

    expire.tv_sec+=timeout->tv_sec;
    expire.tv_nsec+=timeout->tv_nsec;

    if (expire.tv_nsec > 1000000000) {

	expire.tv_nsec -= 1000000000;
	expire.tv_sec++;

    }

    pthread_mutex_lock(&fuseparam->mutex);

    while (request->flags==0) {

	result=pthread_cond_timedwait(&fuseparam->cond, &fuseparam->mutex, &expire);

	if (request->flags>0) {

	    break;

	} else if (result==ETIMEDOUT) {

	    request->flags|=FUSEDATA_FLAG_ERROR;
	    request->error=ETIMEDOUT;
	    break;

	}

    }

    pthread_mutex_unlock(&fuseparam->mutex);

    return (request->flags & FUSEDATA_FLAG_RESPONSE) ? 1 : 0;
}

static void _remove_datahash(struct double_index_s *index, uint64_t unique)
{
    unsigned int hash=unique % FUSEPARAM_QUEUE_HASHSIZE;

    pthread_mutex_lock(&datahash_mutex);

    if (datahash[hash]==index) {

	datahash[hash]=index->next;
	if (datahash[hash]) datahash[hash]->prev=NULL;

    } else {

	if (index->next) index->next->prev=index->prev;
	index->prev->next=index->next;

    }

    index->next=NULL;
    index->prev=NULL;

    pthread_mutex_unlock(&datahash_mutex);

}

/*
    function to be done in a seperate thread
    here the data which is read from the VFS is 
    processed by the fuse fs call (fuse_session_process_buf)
*/

static void process_fusequeue(void *data)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) data;
    struct double_index_s *index=NULL;

    readqueue:

    pthread_mutex_lock(&fuseparam->queue.mutex);

    index=fuseparam->queue.first;

    if (index) {

	fuseparam->queue.first=index->next;
	if (! index->next) fuseparam->queue.last=NULL;

    }

    pthread_mutex_unlock(&fuseparam->queue.mutex);

    /* the first bytes are the header, containing the opcode */

    if (index) {
	struct fuse_request_s *request=index->request;

	if (request->opcode<=fuseparam->size_cb) {

	    _add_datahash(index, request->unique);
	    (* fuseparam->fuse_cb[request->opcode])(request);
	    _remove_datahash(index, request->unique);

	} else {

	    reply_VFS_nosys(request);
	    logoutput_error("process_fusebuffer: unknown opcode %i", request->opcode);

	}

	free(request);
	free(index);
	index=NULL;

	goto readqueue;

    }

}

static unsigned char fuse_request_interrupted_default(struct fuse_request_s *request)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) request->interface->ptr;
    return ((request->flags & FUSEDATA_FLAG_INTERRUPTED) || (fuseparam->status & FUSEPARAM_STATUS_DISCONNECT));
}

static unsigned char fuse_request_interrupted_nonfuse(struct fuse_request_s *request)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) request->interface->ptr;
    return (fuseparam->status & FUSEPARAM_STATUS_DISCONNECT);
}

static int read_fuse_event(int fd, void *ptr, uint32_t events)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    struct fs_connection_s *conn=&fuseparam->connection;
    struct fuse_ops_s *fops=conn->io.fuse.fops;
    int lenread=0;
    unsigned int error=0;

    logoutput("read_fuse_event");

    if ((events & (EPOLLERR | EPOLLHUP)) || (events & EPOLLIN)==0) {

	/* the remote side (==kernel/VFS) disconnected */

        logoutput( "read_fuse_event: event %i causes disconnect", events);
	goto disconnect;

    }

    /* read the data coming from VFS */

    readbuffer:

    errno=0;
    lenread=(* fops->read)(&conn->io.fuse, fuseparam->buffer, fuseparam->size);
    error=errno;

    /* number bytes read should be at least the size of the incoming header */

    if (lenread < (int) size_in_header) {

	logoutput("read_fuse_event: len read %i error %i buffer size %i", lenread, error, fuseparam->size);

	if (lenread==0) {

	    /* umount/disconnect */
	    goto disconnect;

	} else if (error==EAGAIN || error==EWOULDBLOCK) {

	    goto readbuffer;

	} else if (error==EINTR) {

	    logoutput("read_fuse_event: read interrupted");

	} else {

	    logoutput("read_fuse_event: error %i %s", error, strerror(error));

	}

    } else {
	struct fuse_request_s *request=NULL;
	struct fuse_in_header *in = (struct fuse_in_header *) fuseparam->buffer;
	struct double_index_s *index=NULL;

	if (in->len != lenread) {

	    logoutput("read_fuse_event: opcode %i error len %i differs bytes %i read", in->opcode, in->len, lenread);
	    error=EIO;
	    goto error;

	}

	/* put data read on simple queue at tail */

	lenread-=size_in_header;
	request=malloc(sizeof(struct fuse_request_s) + lenread);
	index=malloc(sizeof(struct double_index_s));

	if (request && index) {

	    request->interface=fuseparam->interface;
	    request->opcode=in->opcode;
	    request->flags=0;
	    request->is_interrupted=fuse_request_interrupted_default;
	    request->unique=in->unique;
	    request->ino=in->nodeid;
	    request->uid=in->uid;
	    request->gid=in->gid;
	    request->pid=in->pid;
	    request->size=lenread;

	    logoutput("read_fuse_event: opcode %i size %i", in->opcode, request->size);

	    memcpy(request->buffer, fuseparam->buffer + size_in_header, lenread);

	    index->request=request;
	    index->next=NULL;
	    index->prev=NULL;

	    pthread_mutex_lock(&fuseparam->queue.mutex);

	    if (! fuseparam->queue.last) {

		fuseparam->queue.last=index;
		fuseparam->queue.first=index;

	    } else {

		fuseparam->queue.last->next=index;
		fuseparam->queue.last=index;

	    }

	    pthread_mutex_unlock(&fuseparam->queue.mutex);

	    error=0;
	    work_workerthread(NULL, 0, process_fusequeue, (void *) fuseparam, &error);

	    return 0;

	} else {

	    if (request) {

		free(request);
		request=NULL;

	    }

	    if (index) {

		free(index);
		index=NULL;

	    }

	    error=ENOMEM;

	}

	error:

	if (error>0) {
	    struct fuse_in_header *in = (struct fuse_in_header *) fuseparam->buffer;
	    ssize_t alreadywritten=0;
	    struct fuse_out_header oh;
	    struct iovec iov[1];

	    oh.len=size_out_header;
	    oh.error=-error;
	    oh.unique=in->unique;

	    iov[0].iov_base=&oh;
	    iov[0].iov_len=oh.len;

	    replyVFS:

	    alreadywritten+=(* fops->writev)(&conn->io.fuse, iov, 1);

	}

    }

    if (error==0) error=EIO;
    logoutput("read_fuse_event: error (%i:%s)", error, strerror(error));
    return -1;

    disconnect:

    (* fuseparam->interface->signal_context)(fuseparam->interface, "disconnect");
    return -1;

}

static size_t get_fuse_buffer_size()
{
    return getpagesize() + 0x1000;
}

static mode_t get_masked_perm_default(mode_t perm, mode_t mask)
{
    return ((perm & (S_IRWXU | S_IRWXG | S_IRWXO)) & ~mask);
}

static mode_t get_masked_perm_ignore(mode_t perm, mode_t mask)
{
    return (perm & (S_IRWXU | S_IRWXG | S_IRWXO));
}

static struct fuseparam_s *create_fuse_interface()
{
    struct fuseparam_s *fuseparam=NULL;
    size_t size=get_fuse_buffer_size();

    fuseparam=malloc(sizeof(struct fuseparam_s) + size);

    if (fuseparam) {

	memset(fuseparam, 0, sizeof(struct fuseparam_s) + size);

	fuseparam->size=size;
	fuseparam->read=0;
	fuseparam->status=0;
	fuseparam->interface=NULL;
	init_connection(&fuseparam->connection, FS_CONNECTION_TYPE_FUSE, FS_CONNECTION_ROLE_CLIENT);

	/* change this .... make this longer for a network fs like 4/5/6 seconds */

	fuseparam->attr_timeout.tv_sec=1;
	fuseparam->attr_timeout.tv_nsec=0;

	fuseparam->entry_timeout.tv_sec=1;
	fuseparam->entry_timeout.tv_nsec=0;

	fuseparam->negative_timeout.tv_sec=1;
	fuseparam->negative_timeout.tv_nsec=0;

	fuseparam->size_cb=(sizeof(fuseparam->fuse_cb) / sizeof(fuseparam->fuse_cb[0]));

	/* default various ops */

	for (unsigned int i=0; i < fuseparam->size_cb; i++) fuseparam->fuse_cb[i]=do_reply_nosys;
	fuseparam->fuse_cb[FUSE_INIT]=do_init;
	fuseparam->fuse_cb[FUSE_DESTROY]=do_noreply;
	fuseparam->fuse_cb[FUSE_FORGET]=do_noreply;
	fuseparam->fuse_cb[FUSE_BATCH_FORGET]=do_noreply;

	fuseparam->get_masked_perm=get_masked_perm_default;

	pthread_mutex_init(&fuseparam->mutex, NULL);
	pthread_cond_init(&fuseparam->cond, NULL);

	fuseparam->queue.first=NULL;
	fuseparam->queue.last=NULL;

	pthread_mutex_init(&fuseparam->queue.mutex, NULL);

	if (hashinit==0) {

	    for (unsigned int i=0; i<FUSEPARAM_QUEUE_HASHSIZE; i++) datahash[i]=NULL;
	    hashinit=1;

	}

    } else {

	logoutput_warning("create_fuse_interface: unable to allocate fuseparam (buffer size: %i)", (int) size);

    }

    return fuseparam;

}

void disable_masking_userspace(void *ptr)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    if (fuseparam) fuseparam->get_masked_perm=get_masked_perm_ignore;
}

mode_t get_masked_permissions(void *ptr, mode_t perm, mode_t mask)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    return (* fuseparam->get_masked_perm)(perm, mask);
}

pthread_mutex_t *get_fuse_pthread_mutex(struct context_interface_s *interface)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) interface->ptr;
    return &fuseparam->mutex;
}

pthread_cond_t *get_fuse_pthread_cond(struct context_interface_s *interface)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) interface->ptr;
    return &fuseparam->cond;
}

struct timespec *get_fuse_interface_attr_timeout(void *ptr)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    return &fuseparam->attr_timeout;
}

struct timespec *get_fuse_interface_entry_timeout(void *ptr)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    return &fuseparam->entry_timeout;
}

struct timespec *get_fuse_interface_negative_timeout(void *ptr)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) ptr;
    return &fuseparam->negative_timeout;
}

static void close_fuse_interface(struct fuseparam_s *fuseparam)
{
    if (fuseparam->connection.io.fuse.xdata.fd>0) {
	close(fuseparam->connection.io.fuse.xdata.fd);
	fuseparam->connection.io.fuse.xdata.fd=0;
    }
}

void signal_fuse_interface(struct context_interface_s *interface, const char *what)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) interface->ptr;

    if (fuseparam==NULL) return;

    if (strcmp(what, "disconnecting")==0) {

	signal_fuse_interface_common(fuseparam, FUSEPARAM_STATUS_DISCONNECTING);

    } else if (strcmp(what, "disconnected")==0) {

	signal_fuse_interface_common(fuseparam, FUSEPARAM_STATUS_DISCONNECTED);

    } else if (strcmp(what, "close")==0) {

	close_fuse_interface(fuseparam);

    } else if (strcmp(what, "free")==0) {

	close_fuse_interface(fuseparam);
	pthread_mutex_destroy(&fuseparam->mutex);
	pthread_cond_destroy(&fuseparam->cond);
	free(fuseparam);
	interface->ptr=NULL;

    }

}

mode_t get_default_rootmode_fuse_mountpoint()
{
    return (S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
}

mode_t get_rootmode_fuse_mountpoint(struct context_interface_s *interface)
{
    struct context_option_s option;
    mode_t rootmode=0;

    memset(&option, 0, sizeof(struct context_option_s));
    option.type=_INTERFACE_OPTION_INT;

    if ((* interface->get_context_option)(interface, "fuse:rootmode", &option)>0) rootmode=(mode_t) option.value.number;

    return (rootmode>0) ? rootmode : get_default_rootmode_fuse_mountpoint();
}

unsigned int get_default_maxread_fuse_mountpoint()
{
    return 8192;
}

unsigned int get_maxread_fuse_mountpoint(struct context_interface_s *interface)
{
    struct context_option_s option;
    unsigned int maxread=0;

    memset(&option, 0, sizeof(struct context_option_s));
    option.type=_INTERFACE_OPTION_INT;

    if ((* interface->get_context_option)(interface, "fuse:maxread", &option)>0) maxread=(unsigned int) option.value.number;

    return (maxread>0) ? maxread : get_default_maxread_fuse_mountpoint();
}

static int get_format_mountoptions(char **format)
{
    *format=strdup("fd=%i,rootmode=%o,user_id=%i,group_id=%i,default_permissions,max_read=%i");
    return (*format) ? strlen(*format) : -1;
}

static int print_format_mountoptions(struct context_interface_s *interface, char *buffer, unsigned int size, int fd, uid_t uid, gid_t gid)
{
    char *format=NULL;
    int result=-1;

    if (get_format_mountoptions(&format)>0) {
	mode_t rootmode=get_rootmode_fuse_mountpoint(interface);
	unsigned int maxread=get_maxread_fuse_mountpoint(interface);

	result=snprintf(buffer, size, format, fd, rootmode, uid, gid, maxread);
	free(format);

    }

    return result;
}

int compare_format_mountoptions(struct context_interface_s *interface, char *mountoptions, uid_t uid, gid_t gid)
{
    int result=-1;
    char *format=NULL;

    if (get_format_mountoptions(&format)>0) {
	int s_fd=0;
	mode_t s_rootmode=0;
	int s_uid=0;
	int s_gid=0;
	int s_max=0;

	/* get the parameters from the mountoptions given the format
	    important are the uid and the gid */

	if (sscanf(mountoptions, format, &s_fd, &s_rootmode, &s_uid, &s_gid, &s_max)==5) {

	    if (s_uid==uid && s_gid==gid && 
	    ((interface && s_rootmode==get_rootmode_fuse_mountpoint(interface)) || interface==NULL)) result=0;

	}

    }

    return result;

}

/* connect the fuse interface with the target: the VFS/kernel */

static int connect_fuse_interface(uid_t uid, struct context_interface_s *interface, struct context_address_s *address, unsigned int *error)
{
    struct fuseparam_s *fuseparam=NULL;
    char fusedevice[32];
    char mountoptions[256];
    unsigned int mountflags=0;
    int fd=-1;
    struct passwd *pwd=NULL;

    if (!(address->network.type==_INTERFACE_ADDRESS_NONE) || !(address->service.type==_INTERFACE_SERVICE_FUSE)) {

	*error=EINVAL;
	logoutput("connect_fuse_interface: error, only fuse address");
	goto error;

    } else if (address->service.target.fuse.source==NULL || address->service.target.fuse.mountpoint==NULL || address->service.target.fuse.name==NULL) {

	*error=EINVAL;
	logoutput("connect_fuse_interface: error, incomplete fuse target");
	goto error;

    }

    fuseparam=(struct fuseparam_s *) interface->ptr;
    if (fuseparam==NULL) {

	*error=EINVAL;
	goto error;

    }

    fuseparam->status = FUSEPARAM_STATUS_CONNECTING;
    *error=0;
    snprintf(fusedevice, 32, "/dev/fuse");
    fd=open(fusedevice, O_RDWR | O_NONBLOCK);

    if (fd <= 0) {

	/* unable to open the device */

	logoutput("connect_fuse_interface: unable to open %s, error %i:%s", fusedevice, errno, strerror(errno));
	*error=errno;
	goto error;

    } else {

	logoutput("connect_fuse_interface: fuse device %s open with %i", fusedevice, fd);

    }

    pwd=getpwuid(uid);

    if (print_format_mountoptions(interface, mountoptions, 256, fd, uid, ((pwd) ? pwd->pw_gid : 0))<=0) {

	*error=errno;
	goto error;

    }

    errno=0;
    mountflags=MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_NOATIME;

    if (mount(address->service.target.fuse.source, address->service.target.fuse.mountpoint, "fuse", mountflags, (const void *) mountoptions)==0) {

	logoutput("connect_fuse_interface: (fd=%i) mounted %s, type fuse with options %s", fd, address->service.target.fuse.mountpoint, mountoptions);

    } else {

	logoutput("connect_fuse_interface: error %i:%s mounting %s with options %s", errno, strerror(errno), address->service.target.fuse.mountpoint, mountoptions);
	*error=errno;
	goto error;

    }

    out:
    return fd;

    error:

    if (fd>0) close(fd);
    (* interface->signal_interface)(interface, "disconnect");
    return -1;

}

static int start_fuse_interface(struct context_interface_s *interface, int fd, void *data)
{
    struct fuseparam_s *fuseparam=(struct fuseparam_s *) interface->ptr;
    unsigned int error=0;

    logoutput("start_fuse_interface");

    if ((*interface->add_context_eventloop)(interface, &fuseparam->connection, fd, read_fuse_event, (void *) fuseparam, "FUSE", &error)==0) {

	logoutput("start_fuse_interface: %i added to eventloop", fd);
	fuseparam->status |= FUSEPARAM_STATUS_CONNECTED;
	return 0;

    }

    logoutput("start_fuse_interface: error %i adding %i to eventloop", error, fd, strerror(error));
    return -1;
}

int init_fuse_interface(struct context_interface_s *interface)
{
    struct fuseparam_s *fuseparam=NULL;

    /* initialize the buffer to read the data from the VFS -> userspace
	for this workspace (=mountpoint) */

    fuseparam=create_fuse_interface();

    if (fuseparam) {

	fuseparam->interface=interface;
	interface->connect=connect_fuse_interface;
	interface->start=start_fuse_interface;
	interface->signal_interface=signal_fuse_interface;
	interface->ptr=(void *) fuseparam;
	return 0;

    }

    return -1;

}
