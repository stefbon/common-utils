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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <syslog.h>
#include <sys/syscall.h>
#include "logging.h"

static unsigned char defaultlevel=0;
void (* logoutput)(const char *fmt, ...);

unsigned int gettid()
{
    return (unsigned int) syscall(SYS_gettid);
}

/* no logging */

void logoutput_debug_nolog(const char *fmt, ...)
{
}
void logoutput_info_nolog(const char *fmt, ...)
{
}
void logoutput_notice_nolog(const char *fmt, ...)
{
}
void logoutput_warning_nolog(const char *fmt, ...)
{
}
void logoutput_error_nolog(const char *fmt, ...)
{
}

/* log to stdout/stderr
    note:
    debug, notice and info -> stdout
    warning, error (=crit) -> stderr
    */

void logoutput_debug_std(const char *fmt, ...)
{
    va_list args;
    unsigned int len=strlen(fmt);
    char fmtextra[len+1];

    memcpy(fmtextra, fmt, len);
    fmtextra[len]='\n';

    va_start(args, fmt);
    vfprintf(stdout, fmtextra, args);
    va_end(args);
}
void logoutput_info_std(const char *fmt, ...)
{
    va_list args;
    unsigned int len=strlen(fmt);
    char fmtextra[len+1];

    memcpy(fmtextra, fmt, len);
    fmtextra[len]='\n';

    va_start(args, fmt);
    vfprintf(stdout, fmtextra, args);
    va_end(args);
}
void logoutput_notice_std(const char *fmt, ...)
{
    va_list args;
    unsigned int len=strlen(fmt);
    char fmtextra[len+1];

    memcpy(fmtextra, fmt, len);
    fmtextra[len]='\n';

    va_start(args, fmt);
    vfprintf(stdout, fmtextra, args);
    va_end(args);
}
void logoutput_warning_std(const char *fmt, ...)
{
    va_list args;
    unsigned int len=strlen(fmt);
    char fmtextra[len+1];

    memcpy(fmtextra, fmt, len);
    fmtextra[len]='\n';

    va_start(args, fmt);
    vfprintf(stderr, fmtextra, args);
    va_end(args);
}
void logoutput_error_std(const char *fmt, ...)
{
    va_list args;
    unsigned int len=strlen(fmt);
    char fmtextra[len+1];

    memcpy(fmtextra, fmt, len);
    fmtextra[len]='\n';

    va_start(args, fmt);
    vfprintf(stderr, fmtextra, args);
    va_end(args);
}

/* log to syslog */

void logoutput_debug_syslog(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsyslog(LOG_DEBUG, fmt, args);
    va_end(args);
}
void logoutput_info_syslog(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsyslog(LOG_INFO, fmt, args);
    va_end(args);
}
void logoutput_notice_syslog(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsyslog(LOG_NOTICE, fmt, args);
    va_end(args);
}
void logoutput_warning_syslog(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsyslog(LOG_WARNING, fmt, args);
    va_end(args);
}
void logoutput_error_syslog(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsyslog(LOG_CRIT, fmt, args);
    va_end(args);
}

struct logoutput_s logging = {

    .debug			= logoutput_debug_std,
    .info			= logoutput_info_std,
    .notice			= logoutput_notice_std,
    .warning			= logoutput_warning_std,
    .error			= logoutput_error_std,
};

void switch_logging_backend(const char *what)
{

    if (strcmp(what, "std")==0) {

	logging.debug=logoutput_debug_std;
	logging.info=logoutput_info_std;
	logging.notice=logoutput_notice_std;
	logging.warning=logoutput_warning_std;
	logging.error=logoutput_error_std;

    } else if (strcmp(what, "syslog")==0) {

	logging.debug=logoutput_debug_syslog;
	logging.info=logoutput_info_syslog;
	logging.notice=logoutput_notice_syslog;
	logging.warning=logoutput_warning_syslog;
	logging.error=logoutput_error_syslog;

    } else if (strcmp(what, "nolog")==0) {

	logging.debug=logoutput_debug_nolog;
	logging.info=logoutput_info_nolog;
	logging.notice=logoutput_notice_nolog;
	logging.warning=logoutput_warning_nolog;
	logging.error=logoutput_error_nolog;

    }

}

void switch_default_loglevel(const char *what)
{

    if (strcmp(what, "debug")==0) {

	defaultlevel=0;
	logoutput=logging.debug;

    } else if (strcmp(what, "notice")==0) {

	defaultlevel=2;
	logoutput=logging.notice;

    } else if (strcmp(what, "info")==0) {

	defaultlevel=1;
	logoutput=logging.info;

    } else if (strcmp(what, "warning")==0) {

	defaultlevel=3;
	logoutput=logging.warning;

    } else if (strcmp(what, "error")==0) {

	defaultlevel=4;
	logoutput=logging.error;

    }

}


