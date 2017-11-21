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
#include <errno.h>
#include <err.h>
#include <syslog.h>
#include <sys/syscall.h>

#ifdef LOGGING

#define logoutput_debug(...) syslog(LOG_DEBUG, __VA_ARGS__)
#define logoutput_info(...) syslog(LOG_INFO, __VA_ARGS__)
#define logoutput_notice(...) syslog(LOG_NOTICE, __VA_ARGS__)
#define logoutput_warning(...) syslog(LOG_WARNING, __VA_ARGS__)
#define logoutput_error(...) syslog(LOG_ERR, __VA_ARGS__)

#define logoutput(...) syslog(LOG_DEBUG, __VA_ARGS__)

static pid_t gettid()
{
    return (pid_t) syscall(SYS_gettid);
}

#else

static inline void dummy_nolog()
{
    /* logs nothing */
}

#define logoutput_debug(...) dummy_nolog()
#define logoutput_info(...) dummy_nolog()
#define logoutput_notice(...) dummy_nolog()
#define logoutput_warning(...) dummy_nolog()
#define logoutput_error(...) dummy_nolog()

#define logoutput(...) dummy_nolog()

static pid_t gettid()
{
    return 0;
}

#endif
