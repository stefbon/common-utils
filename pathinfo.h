/*
  2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017 Stef Bon <stefbon@gmail.com>

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

#ifndef _FUSE_PATHINFO_H
#define _FUSE_PATHINFO_H

#define PATHINFO_FLAGS_NONE					0
#define PATHINFO_FLAGS_ALLOCATED				1
#define PATHINFO_FLAGS_INUSE					2

#define PATHINFO_INIT						{NULL, 0, 0, 0}

struct pathinfo_s {
    char 				*path;
    unsigned int 			len;
    unsigned char 			flags;
    int					refcount;
};

void init_pathinfo(struct pathinfo_s *pathinfo);
void free_path_pathinfo(struct pathinfo_s *pathinfo);

#endif
