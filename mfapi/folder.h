/*
 * Copyright (C) 2013 Bryan Christ <bryan.christ@mediafire.com>
 *               2014 Johannes Schauer <j.schauer@email.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2, as published by 
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with 
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef __MFAPI_FOLDER_H__
#define __MFAPI_FOLDER_H__

typedef struct _folder_s    mffolder;

struct _folder_s;

mffolder*   folder_alloc(void);

void        folder_free(mffolder *folder);

int         folder_set_key(mffolder *folder,const char *folderkey);

const char* folder_get_key(mffolder *folder);

int         folder_set_parent(mffolder *folder,const char *folderkey);

const char* folder_get_parent(mffolder *folder);

int         folder_set_name(mffolder *folder,const char *name);

const char* folder_get_name(mffolder *folder);

#endif