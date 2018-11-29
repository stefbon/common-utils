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
#include <dirent.h>

#include <pwd.h>

#ifndef ENOATTR
#define ENOATTR ENODATA        /* No such attribute */
#endif

#include "utils.h"
#include "pathinfo.h"
#include "entry-management.h"
#include "fuse-fs.h"
#include "workspaces.h"

#undef LOGGING
#include "logging.h"

static struct workspace_base_s *base_list=NULL;


/* function to get the "real" path from a template, which has has the 
   strings $HOME and $USER in it, which have to be replaced by the real value

   this can be programmed in a more generic way, but here only a small number fixed variables
   is to be looked for.. 

   return value: the converted string, which has to be freed later 
*/

char *get_path_from_template(char *template, struct fuse_user_s *user, char *buff, size_t len0)
{
    char *conversion=NULL;
    char *p1, *p2, *p1_keep;
    unsigned int len1, len2, len3, len4, len5;
    char path1[PATH_MAX];
    struct passwd *pwd=NULL;

    logoutput_notice("get_path_from_template: template %s", template);

    pwd=getpwuid(user->uid);

    if (! pwd) return NULL;

    len1=strlen(template);

    len2=strlen("$HOME");
    len3=strlen(pwd->pw_dir);
    len4=strlen("$USER");
    len5=strlen(pwd->pw_name);

    p1=template;
    p2=path1;

    findnext:

    p1_keep=p1;
    p1=strchrnul(p1, '$');

    if ( *p1=='$' ) {

	if (p1 + len2 <= template + len1) {

	    if ( strncmp(p1, "$HOME", len2)==0 ) {

		if (p1>p1_keep) {

		    memcpy(p2, p1_keep, p1-p1_keep);
		    p2+=p1-p1_keep;

		}

		memcpy(p2, pwd->pw_dir, len3);
		p2+=len3;
		p1+=len2;

		goto findnext;

	    }

	}

	if (p1 + len4 <= template + len1) {

	    if ( strncmp(p1, "$USER", len4)==0 ) {

		if (p1>p1_keep) {

		    memcpy(p2, p1_keep, p1-p1_keep);
		    p2+=p1-p1_keep;

		}

		memcpy(p2, pwd->pw_name, len5);
		p2+=len5;
		p1+=len4;

		goto findnext;

	    }

	}

	/* when here: a $ is found, but it's not one of above */

	p1++;

    } else {

	/* $ not found, p1 points to end of string: maybe there is some left over */

	if ( p1>p1_keep ) {

	    memcpy(p2, p1_keep, p1-p1_keep);
	    p2+=p1-p1_keep;

	}

	/* terminator */

	*p2='\0';

    }

    if (p2!=path1) {

	/* size including the \0 terminator */

	len1=p2-path1+1;

	if ( buff ) {

	    /* store in the supplied buffer */

	    if ( len1<=len0 ) {

		conversion=buff;
		memcpy(conversion, path1, len1);

	    }

	} else {

	    /* create a new buffer */

	    conversion=malloc(len1);
	    if (conversion) memcpy(conversion, path1, len1);

	}

    }

    if (conversion) {

	logoutput_notice("get_path_from_template: result %s", conversion);

    }

    return conversion;

}

static struct workspace_base_s *read_workspace_file(char *workspacefile, char *name)
{
    FILE *fp;
    char line[512];
    char *option;
    char *value;
    struct workspace_base_s base;
    struct workspace_base_s *out=NULL;
    char *sep=NULL;

    logoutput("read_workspace_file, open %s, name %s", workspacefile, name);

    memset(line, '\0', 512);

    fp=fopen(workspacefile, "r");

    if ( ! fp) {

        logoutput("read_workspace_file, error %i when trying to open", errno);
	return NULL;

    }

    memset(&base, 0, sizeof(struct workspace_base_s));

    base.flags=0;
    base.name=strdup(name);
    base.ingrouppolicy=WORKSPACE_RULE_POLICY_NONE;
    base.mount_path_template=NULL;
    base.ingroup=(gid_t) -1;

    base.next=NULL;

    /* type */

    base.type=0;

    if (strncmp(name, "dev.", 4)==0) {

	base.type=WORKSPACE_TYPE_DEVICES;

    } else if (strncmp(name, "network.", 3)==0) {

	base.type=WORKSPACE_TYPE_NETWORK;

    } else if (strncmp(name, "file.", 4)==0) {

	base.type=WORKSPACE_TYPE_FILE;

    } else if (strncmp(name, "backup.", 4)==0) {

	base.type=WORKSPACE_TYPE_BACKUP;

    }

    while (!feof(fp)) {

	if ( ! fgets(line, 512, fp)) continue;

	sep=strchr(line, '\n');
	if (sep) *sep='\0';

	sep=strchr(line, '=');
	if ( ! sep ) continue;

	*sep='\0';
	value=sep+1;
	option=line;

	convert_to(option, UTILS_CONVERT_SKIPSPACE | UTILS_CONVERT_TOLOWER);

	if ( strncmp(option, "#", 1)==0 || strlen(option)==0) {

	    /* skip comments and empty lines */
	    continue;

	} else if ( strcmp(option, "mountpoint")==0 ) {

	    /* MountPoint=templatemountpoint */

	    base.mount_path_template=strdup(value);

	} else if ( strcmp(option, "useringroup")==0 ) {
	    struct group *grp;

	    grp=getgrnam(value);

	    if (grp) {

		/* UserInGroup=%somegroup% */

		base.ingroup=grp->gr_gid;

	    }

	} else if ( strcmp(option, "ingrouppolicy")==0 ) {

	    /* InGroupPolicy */

	    convert_to(value, UTILS_CONVERT_SKIPSPACE | UTILS_CONVERT_TOLOWER);

	    if ( strcmp(value, "required")==0 ) {

		base.ingrouppolicy=WORKSPACE_RULE_POLICY_REQUIRED;

	    } else if ( strcmp(value, "sufficient")==0 ) {

		base.ingrouppolicy=WORKSPACE_RULE_POLICY_SUFFICIENT;

	    }

	}

    }

    fclose(fp);

    /* all records read, test it's valid, consistent */

    if (  ! base.mount_path_template ) {

	logoutput("read_workspace_file: error reading workspace file %s: mount path not set", base.name);
	goto incomplete;

    }

    if ( base.ingroup==(gid_t) -1) {

	logoutput("read_workspace_file: error reading workspace file %s: ingroup not set", base.name);
	goto incomplete;

    }

    if ( base.ingrouppolicy==WORKSPACE_RULE_POLICY_NONE ) {

	logoutput("error reading workspace file %s: ingroup policy not set", base.name);
	goto incomplete;

    }

    out=malloc(sizeof(struct workspace_base_s));

    if ( out) {

	memcpy(out, &base, sizeof(struct workspace_base_s));

	out->next=base_list;
	base_list=out;

	return out;

    }

    incomplete:

    return NULL;

}

void read_workspace_files(char *path)
{
    DIR *dp;

    dp=opendir(path);

    if (dp) {
	char *lastpart;
	struct dirent *de;
	unsigned int len0=strlen(path);

	while((de=readdir(dp))) {

	    if ( strcmp(de->d_name, ".")==0 || strcmp(de->d_name, "..")==0 ) continue;

	    logoutput("read_workspace_files: found %s", de->d_name);

	    lastpart=strrchr(de->d_name, '.');

	    if (lastpart) {

		if ( strcmp(lastpart, ".workspace")==0 ) {
		    unsigned int len1=len0 + 2 + strlen(de->d_name);
		    char workspacefile[len1];

		    memset(workspacefile, '\0', len1);
		    snprintf(workspacefile, len1, "%s/%s", path, de->d_name);

		    if (read_workspace_file(workspacefile, de->d_name)) {

			logoutput("read_workspace_files: reading %s success", de->d_name);

		    }

		}

	    }

	}

	closedir(dp);

    } else {

	logoutput("read_workspace_files: cannot open directory %s, error %i", path, errno);

    }

}

struct workspace_base_s *get_next_workspace_base(struct workspace_base_s *base)
{

    if (base) return base->next;
    return base_list;

}

int create_directory(struct pathinfo_s *pathinfo, mode_t mode, unsigned int *error)
{
    char path[pathinfo->len + 1];
    char *slash=NULL;
    unsigned int len=0;

    memcpy(path, pathinfo->path, pathinfo->len + 1);
    unslash(path);
    len=strlen(path);

    /* create the parent path */

    slash=strchrnul(path, '/');

    while (slash) {

	if (*slash=='/') *slash='\0';

	if (strlen(path)==0) goto next;

	if (mkdir(path, mode)==-1) {

	    if (errno != EEXIST) {

		logoutput("create_directory: error %i%s creating %s", errno, strerror(errno), path);
		*error=errno;
		return -1;

	    }

	}

	next:

	if ((int) (slash - path) >= len) {

	    break;

	} else {

	    *slash='/';
	    slash=strchrnul(slash+1, '/');

	}

    }

    return 0;

}

unsigned char ismounted(char *path)
{
    unsigned int len=strlen(path);
    char tmppath[strlen(path)+1];
    char *slash=NULL;
    unsigned char ismounted=0;
    struct stat st;

    memcpy(tmppath, path, len+1);
    slash=strrchr(tmppath, '/');

    if (slash && stat(tmppath, &st)==0) {
	dev_t dev=st.st_dev;

	*slash='\0';

	if (stat(tmppath, &st)==0) {

	    if (dev!=st.st_dev) ismounted=1;

	}

	*slash='/';

    }

    return ismounted;

}

unsigned char user_is_groupmember(char *username, struct group *grp)
{
    unsigned char found=0;
    char **member;

    member=grp->gr_mem;

    while(*member) {

	if (strcmp(username, *member)==0) {

	    found=1;
	    break;

	}

	member++;

    }

    return found;

}

void free_workspaces_base()
{
    struct workspace_base_s *base=base_list;

    while(base) {

	base_list=base->next;
	if (base->name) free(base->name);
	if (base->mount_path_template) free(base->mount_path_template);
	free(base);
	base=base_list;

    }

}
