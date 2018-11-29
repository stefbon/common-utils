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

#include "logging.h"

static unsigned char defaultlevel=0;

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

static struct logging_s logging_nolog = {

    .debug=logoutput_debug_nolog,
    .info=logoutput_info_nolog,
    .notice=logoutput_notice_nolog,
    .warning=logoutput_warning_nolog,
    .error=logoutput_error_nolog,

};

struct logging_s *logging=&logging_nolog;

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

static struct logging_s logging_std = {

    .debug=logoutput_debug_std,
    .info=logoutput_info_std,
    .notice=logoutput_notice_std,
    .warning=logoutput_warning_std,
    .error=logoutput_error_std,

};


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

static struct logging_s logging_syslog = {

    .debug			= logoutput_debug_syslog,
    .info			= logoutput_info_syslog,
    .notice			= logoutput_notice_syslog,
    .warning			= logoutput_warning_syslog,
    .error			= logoutput_error_syslog,
};

void switch_logging_backend(const char *what)
{

    logging=&logging_nolog;

    if (strcmp(what, "std")==0) {

	logging=&logging_std;

    } else if (strcmp(what, "syslog")==0) {

	logging=&logging_syslog;

    } else {

	syslog(LOG_WARNING, "log backend %s not reckognized", what);

    }

}
