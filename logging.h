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

#ifndef SB_COMMON_UTILS_LOGGING
#define SB_COMMON_UTILS_LOGGING

#define _GNU_SOURCE

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <err.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <syslog.h>

struct logging_s {
    void 			(* debug)(const char *fmt, ...);
    void 			(* info)(const char *fmt, ...);
    void 			(* notice)(const char *fmt, ...);
    void 			(* warning)(const char *fmt, ...);
    void 			(* error)(const char *fmt, ...);
};

// #ifdef LOGGING

struct logging_s *logging;

/* without extension defaults to debug */
#define logoutput(...) (* logging->info)(__VA_ARGS__)

#define logoutput_debug(...) (* logging->debug)(__VA_ARGS__)
#define logoutput_info(...) (* logging->info)(__VA_ARGS__)
#define logoutput_notice(...) (* logging->notice)(__VA_ARGS__)
#define logoutput_warning(...) (* logging->warning)(__VA_ARGS__)
#define logoutput_error(...) (* logging->error)(__VA_ARGS__)


unsigned int gettid();
void switch_logging_backend(const char *what);

#endif