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

#include <fcntl.h>

#define FANOTIFY_BUFF_LEN (512 * (FAN_EVENT_METADATA_LEN + 16))

static struct bevent_xdata_s xdata_fanotify;
static char fd_path[PATH_MAX];
static char target_path[PATH_MAX];
static unsigned int mainpid=0;

static struct simple_group_s group_fanotify_fsevent;

#define FA_EVENT_FLAG_MODIFIED				1
#define FA_EVENT_FLAG_DISABLED				2

struct fanotify_fsevent_s {
    struct notifywatch_s 	*watch;
    unsigned char		flags;
    int				(* complete)(struct fanotify_fsevent_s *fa, struct fsevent_s *fsevent, struct name_s *xname);
    unsigned int 		pathlen;
    char 			path[];
};

static pthread_mutex_t fa_hashtable_mutex=PTHREAD_MUTEX_INITIALIZER;
static int (*check_version_cb)(unsigned char version);

/*
    get the hash value for a path:
    take the length of the path of the directory
    and the value of the first character of the filename:

    (len << 7) + (*firstchar - 32)

    (and modulo hashtable len)

    - note the path **has** a / for sure, so the memrchr is always successfull

    */

static unsigned int calculate_fa_hash(char *path, unsigned int len)
{
    char *sep=memrchr(path, '/', len);
    return (((unsigned int)(sep-path) << 7) + (*(sep+1)-32)) % group_fanotify_fsevent.len;

}

static unsigned int fa_hashfunction(void *data)
{
    struct fanotify_fsevent_s *fa=(struct fanotify_fsevent_s *) data;
    return calculate_fa_hash(fa->path, fa->pathlen);
}

static struct fanotify_fsevent_s *_lookup_fa_event(char *path, unsigned int len)
{
    unsigned int hashvalue=calculate_fa_hash(path, len);
    void *index=NULL;
    struct fanotify_fsevent_s *fa=NULL;

    pthread_mutex_lock(&fa_hashtable_mutex);

    fa=(struct fanotify_fsevent_s *) get_next_hashed_value(&group_fanotify_fsevent, &index, hashvalue);

    while(fa) {

	if (fa->pathlen==len && memcmp(path, fa->path, len)==0) break;
	fa=(struct fanotify_fsevent_s *) get_next_hashed_value(&group_fanotify_fsevent, &index, hashvalue);

    }

    pthread_mutex_unlock(&fa_hashtable_mutex);
    return fa;

}

static struct fanotify_fsevent_s *lookup_fa_event(struct fsevent_s *fsevent)
{
    return _lookup_fa_event(fsevent->backend.linuxfanotify.path, fsevent->backend.linuxfanotify.pathlen);
}

static void add_faevent_hashtable(struct fanotify_fsevent_s *fa)
{
    pthread_mutex_lock(&fa_hashtable_mutex);
    add_element_to_group(&group_fanotify_fsevent, (void *) fa);
    pthread_mutex_unlock(&fa_hashtable_mutex);
}

static void remove_faevent_hashtable(struct fanotify_fsevent_s *fa)
{
    pthread_mutex_lock(&fa_hashtable_mutex);
    remove_element_from_group(&group_fanotify_fsevent, (void *) fa);
    pthread_mutex_unlock(&fa_hashtable_mutex);
}

void free_fa_event(void *data)
{
    struct fanotify_fsevent_s *fa=(struct fanotify_fsevent_s *) data;
    free(fa);
}

static int complete_fa_unknown(struct fanotify_fsevent_s *fa, struct fsevent_s *fsevent, struct name_s *xname)
{

    free(fsevent->backend.linuxfanotify.path);
    fsevent->backend.linuxfanotify.path=NULL;
    fsevent->backend.linuxfanotify.pathlen=0;

    xname->name=NULL;
    xname->len=0;
    xname->index=0;

    if (fsevent->backend.linuxfanotify.mask & FAN_CLOSE) {

	remove_faevent_hashtable(fa);
	free(fa);

    }

    return -1;

}

static int complete_fa_unknown_name(struct fanotify_fsevent_s *fa, struct fsevent_s *fsevent, struct name_s *xname)
{
    struct notifywatch_s *watch=fa->watch;

    if (watch && fsevent->backend.linuxfanotify.pathlen > watch->pathinfo.len) {

	memmove(fsevent->backend.linuxfanotify.path, (fsevent->backend.linuxfanotify.path + watch->pathinfo.len + 1), fsevent->backend.linuxfanotify.pathlen - watch->pathinfo.len);
	fsevent->backend.linuxfanotify.path=realloc(fsevent->backend.linuxfanotify.path, fsevent->backend.linuxfanotify.pathlen - watch->pathinfo.len);
	fsevent->backend.linuxfanotify.pathlen=fsevent->backend.linuxfanotify.pathlen - watch->pathinfo.len - 1;

	xname->name=fsevent->backend.linuxfanotify.path;
	xname->len=fsevent->backend.linuxfanotify.pathlen;
	xname->index=0;

	fsevent->watch=watch;

	if (fsevent->backend.linuxfanotify.mask & FAN_CLOSE) {

	    remove_faevent_hashtable(fa);
	    free(fa);

	    return (fa->flags & FA_EVENT_FLAG_MODIFIED) ? 0 : -1;

	} else if (fsevent->backend.linuxfanotify.mask & FAN_MODIFY) {

	    fa->flags |= FA_EVENT_FLAG_MODIFIED;
	    return -1;

	} else if (test_watch_ignore(watch, xname->name)==1) {

	    return -1;

	}

	return 0;

    }

    ignore:

    return complete_fa_unknown(fa, fsevent, xname);

}

static void create_fa_cache_entry(struct fsevent_s *fsevent, struct notifywatch_s *watch, int (*complete)(struct fanotify_fsevent_s *fa, struct fsevent_s *fsevent, struct name_s *xname))
{
    struct fanotify_fsevent_s *fa=malloc(sizeof(struct fanotify_fsevent_s) + fsevent->backend.linuxfanotify.pathlen - 1);

    if (fa) {

	fa->complete=complete;
	fa->watch=watch;
	fa->flags=0;
	fa->pathlen=fsevent->backend.linuxfanotify.pathlen;
	memcpy(fa->path, fsevent->backend.linuxfanotify.path, fa->pathlen);

	add_faevent_hashtable(fa);

    }

}

/*
    translate a fsnotify mask to fanotify mask

    the values used by inotify are the same as fsnotify
    so this is simple

*/

static uint32_t translate_mask_fsnotify_to_fanotify(unsigned int *mask)
{
    uint32_t fanotify_mask=0;

    /*
	tanslate watch mask into mask for fanotify
	not every event mask is used for fanotify
    */

    if (*mask & NOTIFYWATCH_MASK_MODIFY) {

	fanotify_mask|=FAN_MODIFY;
	*mask-=NOTIFYWATCH_MASK_MODIFY;

    }

    if (*mask & NOTIFYWATCH_MASK_OPEN) {

	fanotify_mask|=FAN_OPEN;
	*mask-=NOTIFYWATCH_MASK_OPEN;

    }

    if (*mask & NOTIFYWATCH_MASK_CLOSE_NOWRITE) {

	fanotify_mask|=FAN_CLOSE_NOWRITE;
	*mask-=NOTIFYWATCH_MASK_CLOSE_NOWRITE;

    }

    if (*mask & NOTIFYWATCH_MASK_CLOSE_WRITE) {

	fanotify_mask|=FAN_CLOSE_WRITE;
	*mask-=NOTIFYWATCH_MASK_CLOSE_WRITE;

    }

    return fanotify_mask;

}

/*
    function which set a os specific watch on the backend on path with mask mask
*/

int set_watch_backend_fanotify(struct notifywatch_s *watch, uint32_t mask)
{
    unsigned int error=0;

    /*
	test the the underlying fs works with fanotify
	for example /proc and /sys do not support fanotify
	change this test 
    */

    if (issubdirectory(watch->pathinfo.path, "/proc", 1)==1 || issubdirectory(watch->pathinfo.path, "/sys", 1)==1) {

	return -ENOSYS;

    }

    if (mask>0) {

	/*
	    add some sane flags
	*/

	mask |= FAN_EVENT_ON_CHILD;
	mask |= FAN_CLOSE;

	if (fanotify_mark(xdata_fanotify.fd, FAN_MARK_ADD | FAN_MARK_DONT_FOLLOW | FAN_MARK_ONLYDIR, mask, 0, watch->pathinfo.path)==-1) {

	    error=errno;
    	    logoutput_error("set_watch_backend_fanotify: setting fanotify watch on %s with mask %i gives error: %i (%s)", watch->pathinfo.path, mask, error, strerror(error));

	}

    } else {

	error=EINVAL;

    }

    out:

    return (error>0) ? -error : 0;

}

void remove_watch_backend_fanotify(struct notifywatch_s *watch)
{
    unsigned int mask=watch->mask;
    uint32_t fanotify_mask;

    fanotify_mask=translate_mask_fsnotify_to_fanotify(&mask);

    if (fanotify_mask>0) {

	fanotify_mask |= FAN_EVENT_ON_CHILD;
	fanotify_mask |= FAN_CLOSE;

	if (fanotify_mark(xdata_fanotify.fd, FAN_MARK_REMOVE | FAN_MARK_DONT_FOLLOW | FAN_MARK_ONLYDIR, fanotify_mask, 0, watch->pathinfo.path)==-1) {
	    unsigned int error=0;

	    error=errno;
    	    logoutput_error("remove_watch_backend_fanotify: removing fanotify watch from %s with mask %i gives error: %i (%s)", watch->pathinfo.path, fanotify_mask, error, strerror(error));

	}

    }

}

static void fanotify_free_fsevent(struct fsevent_s *fsevent)
{

    if (fsevent->backend.linuxfanotify.path) {

	free(fsevent->backend.linuxfanotify.path);
	fsevent->backend.linuxfanotify.path=NULL;

    }

    fsevent->backend.linuxfanotify.pathlen=0;
}

static int get_backup_event(struct entry_s *parent, struct fsevent_s *fsevent, struct name_s *xname)
{
    struct entry_s *entry=NULL;
    char *slash=NULL;
    unsigned int error=0;

    entry=NULL;

    xname->name=fsevent->backend.linuxfanotify.path;

    while(1) {

        /*  walk through path from begin to end and 
            check every part */

        slash=strchr(xname->name, '/');

        if (slash==xname->name) {

            xname->name++;
            if (*xname->name=='\0') break;
            continue;

        }

        if (slash) *slash='\0';

	xname->len=strlen(xname->name);
	calculate_nameindex(xname);

	error=0;
        entry=find_entry(parent, xname, &error);

	if (slash) {

	    *slash='/';

	    if (! entry) break;

	    parent=entry;
	    xname->name=slash+1;

	} else {

	    if (! entry) {
		struct inode_s *inode=parent->inode;

		/* top entry not known: check the parent */

		if (inode->link_type==_INODE_LINK_TYPE_BACKUP) {
		    struct backupjob_struct *backup=(struct backupjob_struct *) get_link_data(inode);

		    if (backup) {

			fsevent->watch=backup->watch;

			create_fa_cache_entry(fsevent, backup->watch, complete_fa_unknown_name);

			if (fsevent->backend.linuxfanotify.mask & FAN_OPEN) goto free;

			memmove(fsevent->backend.linuxfanotify.path, xname->name, xname->len+1);
			fsevent->backend.linuxfanotify.path=realloc(fsevent->backend.linuxfanotify.path, xname->len+1);
			fsevent->backend.linuxfanotify.pathlen=xname->len;
			xname->name=fsevent->backend.linuxfanotify.path;

			return 0;

		    }

		}

	    } else {
		struct inode_s *inode=entry->inode;

		if (inode->link_type==_INODE_LINK_TYPE_BACKUP) {
		    struct backupjob_struct *backup=(struct backupjob_struct *) get_link_data(inode);

		    if (backup) {

			fsevent->watch=backup->watch;

			free(fsevent->backend.linuxfanotify.path);
			fsevent->backend.linuxfanotify.path=NULL;
			fsevent->backend.linuxfanotify.pathlen=0;

			xname->name=NULL;
			xname->len=0;
			xname->index=0;

			return 0;

		    }

		} else {

		    inode=parent->inode;

		    if (inode->link_type==_INODE_LINK_TYPE_BACKUP) {
			struct backupjob_struct *backup=(struct backupjob_struct *) get_link_data(inode);

			if (backup) {

			    fsevent->watch=backup->watch;
			    create_fa_cache_entry(fsevent, backup->watch, complete_fa_unknown_name);

			    if (fsevent->backend.linuxfanotify.mask & FAN_OPEN) goto free;

			    memmove(fsevent->backend.linuxfanotify.path, xname->name, xname->len+1);
			    fsevent->backend.linuxfanotify.path=realloc(fsevent->backend.linuxfanotify.path, xname->len+1);
			    fsevent->backend.linuxfanotify.pathlen=xname->len;
			    xname->name=fsevent->backend.linuxfanotify.path;

			    return 0;

			}

		    }

		}

	    }

	    break;

	}

    }

    free:

    free(fsevent->backend.linuxfanotify.path);
    fsevent->backend.linuxfanotify.path=NULL;
    fsevent->backend.linuxfanotify.pathlen=0;

    xname->name=NULL;
    xname->len=0;
    xname->index=0;

    return -1;

}


static int fanotify_complete_fsevent(struct fsevent_s *fsevent, unsigned int *mask, struct name_s *xname)
{
    struct fanotify_fsevent_s *fa=NULL;

    *mask=0;

    *mask|=(fsevent->backend.linuxfanotify.mask & FAN_MODIFY) ? NOTIFYWATCH_MASK_MODIFY : 0;
    *mask|=(fsevent->backend.linuxfanotify.mask & FAN_OPEN) ? NOTIFYWATCH_MASK_OPEN : 0;
    *mask|=(fsevent->backend.linuxfanotify.mask & FAN_CLOSE_NOWRITE) ? NOTIFYWATCH_MASK_CLOSE_NOWRITE : 0;
    *mask|=(fsevent->backend.linuxfanotify.mask & FAN_CLOSE_WRITE) ? NOTIFYWATCH_MASK_CLOSE_WRITE : 0;

    /* look for entry in cache */

    fa=lookup_fa_event(fsevent);

    if (fa) {

	return (* fa->complete)(fa, fsevent, xname);

    } else {

	return get_backup_event(backup_workspace.rootinode.alias, fsevent, xname);

    }

    return 0;

}

static signed char fanotify_is_move(struct fsevent_s *fsevent, unsigned int *cookie, unsigned int *error)
{
    *error=EINVAL;
    return -1; /* not supported with fanotify */
}

static signed char fanotify_is_dir(struct fsevent_s *fsevent, unsigned int *error)
{
    struct stat st;

    if (stat(fsevent->backend.linuxfanotify.path, &st)==0) {

	return S_ISDIR(st.st_mode);

    }

    *error=errno;
    return -1;

}

static unsigned int fanotify_get_pid(struct fsevent_s *fsevent, unsigned int *error)
{
    return fsevent->backend.linuxfanotify.pid;
}

static void disable_fanotify_file(struct notifywatch_s *watch, struct name_s *xname)
{
    unsigned int len = watch->pathinfo.len + 1 + xname->len;
    char path[len+1];
    struct fanotify_fsevent_s *fa=NULL;

    memcpy(&path[0], watch->pathinfo.path, watch->pathinfo.len);
    path[watch->pathinfo.len]='/';
    memcpy(&path[watch->pathinfo.len+1], xname->name, xname->len+1);

    fa=_lookup_fa_event(path, len);

    if (fa) {

	/* some protection here ?*/

	fa->complete=complete_fa_unknown;
	fa->flags |= FA_EVENT_FLAG_DISABLED;

    }

}

static struct fseventbackend_struct fanotify_fseventbackend = {
    .type				= NOTIFYWATCH_BACKEND_LINUX_FANOTIFY,
    .complete				= fanotify_complete_fsevent,
    .is_move				= fanotify_is_move,
    .is_dir				= fanotify_is_dir,
    .get_pid				= fanotify_get_pid,
    .free				= fanotify_free_fsevent,
};

static int check_version_dummy(unsigned char version)
{
    return 0;
}

static int check_version_init(unsigned char version)
{

    if (version != FANOTIFY_METADATA_VERSION) {

	logoutput_error("check_version_init: version fanotify mismatch: kernel reports version %i, userspace %i", version, FANOTIFY_METADATA_VERSION);
	logoutput_error("check_version_init: this is a serious error, futher use of fanotify is disabled");

	return -1;

    }

    check_version_cb=check_version_dummy;
    return 0;

}

static int handle_fanotify_fd(int fd, void *data, uint32_t events)
{
    int lenread=0;
    char buffer[FANOTIFY_BUFF_LEN];

    lenread=read(fd, (void *) &buffer, FANOTIFY_BUFF_LEN);

    if ( lenread<0 ) {

        logoutput_error("handle_fanotify_fd: error (%i) reading inotify events (fd: %i)", errno, fd);

    } else {
        const struct fanotify_event_metadata *fanotify_metadata=NULL;
        int len_target=0;

	fanotify_metadata = (struct fanotify_event_metadata *) buffer;

        while (FAN_EVENT_OK(fanotify_metadata, lenread)) {

	    if (check_version_cb(fanotify_metadata->vers)==-1) {

		close_fanotify();
		break;

	    }

	    if (fanotify_metadata->fd >= 0) {

		if (fanotify_metadata->pid == mainpid) {

		    close(fanotify_metadata->fd);
		    goto next;

		}

		snprintf(fd_path, PATH_MAX, "/proc/self/fd/%d", fanotify_metadata->fd);

		len_target=readlink(fd_path, target_path, PATH_MAX - 1);

		if (len_target>0) {
		    struct fsevent_struct *fsevent=NULL;
		    char *path=NULL;

		    target_path[len_target]='\0';

		    path=malloc(len_target+1);
		    fsevent=malloc(sizeof(struct fsevent_struct));

		    if (path && fsevent) {

			memcpy(path, target_path, len_target);
			*(path+len_target)='\0';

			// logoutput("handle_fanotify_fd: path %s", path);

			fsevent->watch=NULL;
			fsevent->next=NULL;
			fsevent->prev=NULL;
			fsevent->functions=&fanotify_fseventbackend;
			fsevent->backend.linuxfanotify.path=path;
			fsevent->backend.linuxfanotify.pathlen=len_target;
			fsevent->backend.linuxfanotify.mask=fanotify_metadata->mask;
			fsevent->backend.linuxfanotify.pid=fanotify_metadata->pid;

			// logoutput_info("handle_fanotify_fd: queue event %i on %s", fanotify_metadata->mask, path);

			queue_fsevent(fsevent);

		    } else {

			if (path) {

			    free(path);
			    path=NULL;

			}

			if (fsevent) {

			    free(fsevent);
			    fsevent=NULL;

			}

		    }

		}

		close(fanotify_metadata->fd);

	    }

	    next:

	    fanotify_metadata=FAN_EVENT_NEXT(fanotify_metadata, lenread);

    	}

    }

    return 0;

}


void initialize_fanotify(struct beventloop_s *loop, unsigned int *error)
{
    struct bevent_xdata_s *xdata=NULL;
    int result=0, fd=0;

    fd=fanotify_init(FAN_CLOEXEC | FAN_NONBLOCK, O_RDONLY | O_LARGEFILE);

    if (fd==-1) {

	*error=errno;
        logoutput("initialize_fanotify: error creating fanotify: %i:%s.", errno, strerror(errno));
        goto error;

    }

    xdata=add_to_beventloop(fd, EPOLLIN | EPOLLPRI, &handle_fanotify_fd, loop, &xdata_fanotify, NULL);

    if ( ! xdata ) {

        logoutput("initialize_fanotify: error adding fanotify fd to eventloop.");
        goto error;

    } else {

        logoutput_info("initialize_fanotify: fanotify fd %i added to eventloop", fd);

    }

    result=initialize_group(&group_fanotify_fsevent, fa_hashfunction, 256, error);

    if (result<0) {

	*error=abs(result);
    	logoutput("initialize_fanotify: error %i creating fa event hashtable", *error);
	goto error;

    }

    check_version_cb=check_version_init;
    mainpid=getpid();

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

    free_group(&group_fanotify_fsevent, free_fa_event);

}

void close_fanotify()
{

    if ( xdata_fanotify.fd>0 ) {

	close(xdata_fanotify.fd);
	xdata_fanotify.fd=0;

    }

    remove_xdata_from_beventloop(&xdata_fanotify);
    free_group(&group_fanotify_fsevent, free_fa_event);

}

