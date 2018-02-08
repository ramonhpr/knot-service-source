/*
 * This file is part of the KNOT Project
 *
 * Copyright (c) 2018, CESAR. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __FILE_WATCH_H__
#define __FILE_WATCH_H__

typedef void (*on_file_modified)(void);

void *g_file_watch_add(const char *path, on_file_modified on_file_modified_cb);
void g_file_watch_remove(void *watch);
void *l_file_watch_add(const char *path, on_file_modified on_file_modified_cb);
void l_file_watch_remove(void *watch);

#endif /* __FILE_WATCH_H__ */
