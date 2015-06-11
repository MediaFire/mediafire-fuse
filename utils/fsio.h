/*
 * Copyright (C) 2014 Johannes Schauer <j.schauer@email.de>
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

#ifndef _FSIO_H_
#define _FSIO_H_

//#include <unistd.h>
#include <stdbool.h>

#include <sys/types.h>

typedef struct _fsio_s          fsio_t;

typedef struct _fsio_data_s
{
    void        *data;
    ssize_t     data_sz;

    void        *anything;
}
fsio_data_t;

typedef int (*FSIO_Callback)    (fsio_t *fsio,int event,fsio_data_t *fsio_data);

enum
{
    FSIO_EVENT_BLOCK_READ       =   0x00,
    FSIO_EVENT_FILE_READ,
    FSIO_EVENT_BLOCK_WRITTEN,
    FSIO_EVENT_FILE_WRITTEN,
    FSIO_EVENT_ENUM_MAX,                // end of enum counter
};


fsio_t*     fsio_create(void);

int         fsio_set_source(fsio_t *fsio,int source_fd);

int         fsio_set_target(fsio_t *fsio,int target_fd);

int         fsio_file_read(fsio_t *fsio,ssize_t *bytes);

int         fsio_file_copy(fsio_t *fsio,ssize_t *bytes);

int         fsio_get_source_size(fsio_t *fsio,off_t *size);

void        fsio_set_hook(fsio_t *fsio,int event,FSIO_Callback hook);

void        fsio_set_hook_data(fsio_t *fsio,int event,void *anything);

void*       fsio_get_hook_data(fsio_t *fsio,int event);

void        fsio_destroy(fsio_t *fsio,bool close_fds);


#endif
