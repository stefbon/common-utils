/*
  2010, 2011, 2012, 2013, 2014, 2015 Stef Bon <stefbon@gmail.com>

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

#define INOTIFY_EVENT_SIZE (sizeof(struct inotify_event))
#define INOTIFY_BUFF_LEN (1024 * (INOTIFY_EVENT_SIZE + 16))

static struct bevent_xdata_s xdata_inotify;
static struct simple_hash_s group_watches_inotify;

struct inotify_watch_s {
    unsigned int wd;
    struct notifywatch_s *watch;
};

/* functions to lookup a inotify watch using the inotify watch destriptor (wd) */

static unsigned int calculate_wd_hash(unsigned int wd)
{
    return wd % group_watches_inotify.len;
}

static unsigned int wd_hashfunction(void *data)
{
    struct inotify_watch_s *i_watch=(struct inotify_watch_s *) data;
    return calculate_wd_hash(i_watch->wd);
}

static struct inotify_watch_s *lookup_inotify_watch_wd(unsigned int wd)
{
    unsigned int hashvalue=calculate_wd_hash(wd);
    void *index=NULL;
    struct inotify_watch_s *inotify_watch=NULL;

    inotify_watch=(struct inotify_watch_s *) get_next_hashed_value(&group_watches_inotify, &index, hashvalue);

    while(inotify_watch) {

	if (inotify_watch->wd==wd) break;
	inotify_watch=(struct inotify_watch_s *) get_next_hashed_value(&group_watches_inotify, &index, hashvalue);

    }

    return inotify_watch;

}

void add_watch_inotifytable(struct inotify_watch_s *inotify_watch)
{
    add_data_to_hash(&group_watches_inotify, (void *) inotify_watch);
}

void remove_watch_inotifytable(struct inotify_watch_s *inotify_watch)
{
    remove_data_from_hash(&group_watches_inotify, (void *) inotify_watch);
}

void free_i_watch(void *data)
{
    struct inotify_watch_s *i_watch=(struct inotify_watch_s *) data;
    free(i_watch);
}

static struct inotify_watch_s *get_next_inotify_watch(void **index)
{
    unsigned int hashvalue=0;
    struct inotify_watch_s *i_watch=NULL;

    if (*index) hashvalue=get_hashvalue_index(*index, &group_watches_inotify);

    getnext:

    i_watch=(struct inotify_watch_s *) get_next_hashed_value(&group_watches_inotify, index, hashvalue);

    if (! i_watch) {

	/* no element found for this hashvalue, try one bigger */

	hashvalue++;

	if (hashvalue<group_watches_inotify.len) {

	    *index=NULL;
	    goto getnext;

	}

    }

    return i_watch;

}

uint32_t translate_mask_fsnotify_to_inotify(unsigned int *mask)
{
    uint32_t inotify_mask=0;

    if (*mask & NOTIFYWATCH_MASK_ATTRIB) {

	inotify_mask|=IN_ATTRIB;
	*mask-=NOTIFYWATCH_MASK_ATTRIB;

    }

    if (*mask & NOTIFYWATCH_MASK_MODIFY) {

	inotify_mask|=IN_MODIFY;
	*mask-=NOTIFYWATCH_MASK_MODIFY;

    }

    if (*mask & NOTIFYWATCH_MASK_OPEN) {

	inotify_mask|=IN_OPEN;
	*mask-=NOTIFYWATCH_MASK_OPEN;

    }

    if (*mask & NOTIFYWATCH_MASK_CLOSE_NOWRITE) {

	inotify_mask|=IN_CLOSE_NOWRITE;
	*mask-=NOTIFYWATCH_MASK_CLOSE_NOWRITE;

    }

    if (*mask & NOTIFYWATCH_MASK_CLOSE_WRITE) {

	inotify_mask|=IN_CLOSE_WRITE;
	*mask-=NOTIFYWATCH_MASK_CLOSE_WRITE;

    }

    if (*mask & NOTIFYWATCH_MASK_CREATE) {

	inotify_mask|=IN_CREATE | IN_MOVED_TO;
	*mask-=NOTIFYWATCH_MASK_CREATE;

    }

    if (*mask & NOTIFYWATCH_MASK_DELETE) {

	inotify_mask|=IN_DELETE | IN_MOVED_FROM;
	*mask-=NOTIFYWATCH_MASK_DELETE;

    }

    if (*mask & NOTIFYWATCH_MASK_MOVE_SELF) {

	inotify_mask|=IN_MOVE_SELF;
	*mask-=NOTIFYWATCH_MASK_MOVE_SELF;

    }

    return inotify_mask;

}

static signed char inotify_is_move(struct fsevent_s *fsevent, unsigned int *cookie, unsigned int *error)
{

    if (fsevent->backend.linuxinotify.mask & IN_MOVE) {

	*cookie=fsevent->backend.linuxinotify.cookie;
	return 1;

    }

    return 0;
}

static signed char inotify_is_dir(struct fsevent_s *fsevent, unsigned int *error)
{
    return (fsevent->backend.linuxinotify.mask & IN_ISDIR) ? 1 : 0;
}

static unsigned int inotify_get_pid(struct fsevent_s *fsevent, unsigned int *error)
{
    *error=EINVAL;
    return 0;
}


/*
    function which set a os specific watch on the backend on path with mask mask
*/

int set_watch_backend_inotify(struct notifywatch_s *watch, uint32_t mask)
{
    int wd=0;
    unsigned int error=0;

    logoutput_info("set_watch_backend_inotify");

    /*
	test the the underlying fs works with inotify
	for example /proc and /sys do not support inotify
	change this test 
    */

    if (issubdirectory(watch->pathinfo.path, "/proc", 1)==1 || issubdirectory(watch->pathinfo.path, "/sys", 1)==1) {

	error=ENOSYS;
	goto out;

    }

    if (mask>0) {

	logoutput_info("set_watch_backend_inotify: call inotify_add_watch on path %s and mask %i", watch->pathinfo.path, mask);

	/*
	    add some sane flags and all events:
	*/

	mask |= IN_DONT_FOLLOW;

#ifdef IN_EXCL_UNLINK

	mask |= IN_EXCL_UNLINK;

#endif

	wd=inotify_add_watch(xdata_inotify.fd, watch->pathinfo.path, mask);

	if ( wd==-1 ) {

	    error=errno;

    	    logoutput_error("set_watch_backend_inotify: setting inotify watch on %s (fd=%i)gives error: %i (%s)", watch->pathinfo.path, xdata_inotify.fd, error, strerror(error));

	} else {
	    struct inotify_watch_s *inotify_watch=NULL;

	    inotify_watch=lookup_inotify_watch_wd(wd);

	    if (inotify_watch) {

		if (!(inotify_watch->watch==watch)) {

		    logoutput_error("set_watch_backend_inotify: internal error, inotify watch (wd=%i)(path=%s)", wd, watch->pathinfo.path);
		    inotify_watch->watch=watch;

		}

	    } else {

		inotify_watch=malloc(sizeof(struct inotify_watch_s));

		if (inotify_watch) {

		    inotify_watch->wd=wd;
		    inotify_watch->watch=watch;

		    add_watch_inotifytable(inotify_watch);

		}

	    }

	}

    }

    out:

    return (error>0) ? -error : wd;

}

void remove_watch_backend_inotify(struct notifywatch_s *watch)
{
    unsigned int mask=watch->mask;
    uint32_t inotify_mask=translate_mask_fsnotify_to_inotify(&mask);

    if (inotify_mask>0) {
	void *index=NULL;
	struct inotify_watch_s *i_watch=get_next_inotify_watch(&index);

	/* walk every i_watch */

	while (i_watch) {

	    if (i_watch->watch==watch) break;
	    i_watch=get_next_inotify_watch(&index);

	}

	if (i_watch) {
	    int res;

	    remove_watch_inotifytable(i_watch);
	    res=inotify_rm_watch(xdata_inotify.fd, i_watch->wd);
	    free(i_watch);

	}

    }

}

static int inotify_complete_fsevent(struct fsevent_s *fsevent, unsigned int *mask, struct name_s *xname)
{

    *mask=0;

    *mask|=(fsevent->backend.linuxinotify.mask & IN_ATTRIB) ? NOTIFYWATCH_MASK_ATTRIB : 0;
    *mask|=(fsevent->backend.linuxinotify.mask & IN_MODIFY) ? NOTIFYWATCH_MASK_MODIFY : 0;

    *mask|=(fsevent->backend.linuxinotify.mask & IN_OPEN) ? NOTIFYWATCH_MASK_OPEN : 0;
    *mask|=(fsevent->backend.linuxinotify.mask & IN_CLOSE_NOWRITE) ? NOTIFYWATCH_MASK_CLOSE_NOWRITE : 0;
    *mask|=(fsevent->backend.linuxinotify.mask & IN_CLOSE_WRITE) ? NOTIFYWATCH_MASK_CLOSE_WRITE : 0;

    *mask|=(fsevent->backend.linuxinotify.mask & IN_CREATE) ? NOTIFYWATCH_MASK_CREATE : 0;
    *mask|=(fsevent->backend.linuxinotify.mask & IN_MOVED_TO) ? NOTIFYWATCH_MASK_CREATE : 0;

    *mask|=(fsevent->backend.linuxinotify.mask & IN_DELETE) ? NOTIFYWATCH_MASK_DELETE : 0;
    *mask|=(fsevent->backend.linuxinotify.mask & IN_MOVED_FROM) ? NOTIFYWATCH_MASK_DELETE : 0;
    *mask|=(fsevent->backend.linuxinotify.mask & IN_MOVE_SELF) ? NOTIFYWATCH_MASK_MOVE_SELF : 0;

    if (*mask>0) {

	xname->name=fsevent->backend.linuxinotify.name;

	if (xname->name) {

	    xname->len=strlen(xname->name);
	    calculate_nameindex(xname);

	} else {

	    xname->len=0;

	}

	return 0;

    }

    return -1;

}

static void inotify_free_fsevent(struct fsevent_s *fsevent)
{

    if (fsevent->backend.linuxinotify.name) {

	free(fsevent->backend.linuxinotify.name);
	fsevent->backend.linuxinotify.name=NULL;

    }

}

static void disable_inotify_file(struct notifywatch_s *watch, struct name_s *xname)
{
    /* inotify does not support disabling of fsevents yet */
}

static struct fseventbackend_s inotify_fseventbackend = {
    .type				= NOTIFYWATCH_BACKEND_LINUX_INOTIFY,
    .complete				= inotify_complete_fsevent,
    .is_move				= inotify_is_move,
    .is_dir				= inotify_is_dir,
    .get_pid				= inotify_get_pid,
    .free				= inotify_free_fsevent,
};


static int handle_inotify_fd(int fd, void *data, uint32_t events)
{
    int lenread=0;
    char buff[INOTIFY_BUFF_LEN];

    lenread=read(fd, buff, INOTIFY_BUFF_LEN);

    if ( lenread<0 ) {

        logoutput("handle_inotify_fd: error (%i) reading inotify events (fd: %i)", errno, fd);

    } else {
        int i=0, res;
        struct inotify_event *i_event=NULL;

        while(i<lenread) {

            i_event = (struct inotify_event *) &buff[i];

            if ( (i_event->mask & IN_Q_OVERFLOW) || i_event->wd==-1 ) {

                /* what to do here: read again?? go back ??*/

                goto next;

            } else {
		struct inotify_watch_s *inotify_watch=NULL;

		inotify_watch=lookup_inotify_watch_wd(i_event->wd);

		if (inotify_watch) {
		    struct notifywatch_s *watch=inotify_watch->watch;
		    struct fsevent_s *fsevent=NULL;
		    char *name=NULL;

		    // logoutput("handle_inotify_fd: path %s/%s", watch->pathinfo.path, (i_event->name) ? i_event->name : "");

		    if (i_event->name && test_watch_ignore(watch, i_event->name)==1) goto next;

		    fsevent=malloc(sizeof(struct fsevent_s));
		    name=(i_event->name) ? strdup(i_event->name) : NULL;

		    if (fsevent && ((i_event->name && name) || ! i_event->name)) {

			fsevent->watch=watch;
			fsevent->next=NULL;
			fsevent->prev=NULL;
			fsevent->functions=&inotify_fseventbackend;

			fsevent->backend.linuxinotify.cookie=i_event->cookie;
			fsevent->backend.linuxinotify.mask=i_event->mask;
			fsevent->backend.linuxinotify.name=name;

			queue_fsevent(fsevent);

		    } else {

			if (name) {

			    free(name);
			    name=NULL;

			}

			if (fsevent) {

			    free(fsevent);
			    fsevent=NULL;

			}

		    }

		}

	    }

	    next:

            i += INOTIFY_EVENT_SIZE + i_event->len;

    	}

    }

    return 0;

}

void initialize_inotify(struct beventloop_s *loop, unsigned int *error)
{
    struct bevent_xdata_s *xdata=NULL;
    int result=0, fd=0;

    /* create the inotify instance */

    fd=inotify_init();

    if (fd==-1) {

	*error=errno;
        logoutput("initialize_inotify: error creating inotify fd: %i.", errno);
        goto error;

    }

    /*
	add inotify to the main eventloop
    */

    xdata=add_to_beventloop(fd, EPOLLIN | EPOLLPRI, &handle_inotify_fd, loop, &xdata_inotify, NULL);

    if ( ! xdata ) {

        logoutput("initialize_inotify: error adding inotify fd to eventloop.");
        goto error;

    } else {

        logoutput("initialize_inotify: inotify fd %i added to eventloop", fd);

    }

    result=initialize_group(&group_watches_inotify, wd_hashfunction, 256, error);

    if (result<0) {

    	logoutput("initialize_inotify: error (%i:%s) creating inotify hash table", *error, strerror(*error));
	goto error;

    }

    return;

    error:

    if (fd>0) {

	close(fd);
	fd=0;

    }

    if (xdata) {

	remove_xdata_from_beventloop(xdata);
	xdata=NULL;

    }

    free_group(&group_watches_inotify, free_i_watch);

}

void close_inotify()
{

    if ( xdata_inotify.fd>0 ) {

	close(xdata_inotify.fd);
	xdata_inotify.fd=0;

    }

    remove_xdata_from_beventloop(&xdata_inotify);
    free_group(&group_watches_inotify, free_i_watch);

}

