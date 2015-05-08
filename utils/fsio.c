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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/stat.h>

#include "fsio.h"

#define BLOCK_SIZE_KB(blk_sz)       (blk_sz * 1024)

struct _fsio_s
{
    int             source_fd;
    int             target_fd;

    unsigned long   source_blksz;
    unsigned long   target_blksz;

    char            *buffer;
    size_t          buffer_sz;
    size_t          data_sz;

    FSIO_Callback   hook[FSIO_EVENT_ENUM_MAX - 1];
    void            *anything[FSIO_EVENT_ENUM_MAX - 1];

    size_t          blocks_read;
    size_t          bytes_read;
    size_t          bytes_written;

    int             error;
};


// internal functions
static ssize_t  _fsio_read_block(fsio_t *fsio);

static ssize_t  _fsio_write_blocks(fsio_t *fsio);

static int      _fsio_recommend(fsio_t *fsio,unsigned long min_blksz);

static void     _fsio_reset_counters(fsio_t *fsio);

fsio_t*
fsio_create(void)
{
    fsio_t  *fsio;

    fsio = (fsio_t*)calloc(1,sizeof(fsio_t));

    return fsio;
}


int
fsio_set_source(fsio_t *fsio,int source_fd)
{
    if(fsio == NULL) return -1;

    if(source_fd <= 0) return -1;

    fsio->source_fd = source_fd;

    return 0;
}

int
fsio_set_target(fsio_t *fsio,int target_fd)
{
    if(fsio == NULL) return -1;

    if(target_fd <= 0) return -1;

    fsio->target_fd = target_fd;

    return 0;
}

int
fsio_file_read(fsio_t *fsio,ssize_t *bytes)
{
    ssize_t         bytes_read = 0;
    off_t           file_size = 0;
    FSIO_Callback   hook;
    void            *anything;
    fsio_data_t     fsio_data = { .data = NULL, .data_sz = 0 };
    ssize_t         retval = 0;

    if(fsio == NULL) return -1;
    if(bytes == NULL) return -1;

    _fsio_recommend(fsio,BLOCK_SIZE_KB(128));

    fprintf(stderr,"fsio source blksz: %lu\n",fsio->source_blksz);

    // no-op
    if(*bytes == 0) return 0;

    if(fsio->source_fd <= 0) return -1;

    if(*bytes == -1)
    {
        retval = fsio_get_source_size(fsio,&file_size);

        // the above fuction will fail if the file is not a regular file
        if(retval == -1) return -1;

        *bytes = file_size;
    }

    fprintf(stderr,"fsio_file_read() %zd bytes\n",*bytes);

    // zero out our counters
    _fsio_reset_counters(fsio);

    while(fsio->bytes_read <= (size_t)*bytes)
    {
        bytes_read = _fsio_read_block(fsio);

        // were done
        if(bytes_read == 0) break;

        fsio->bytes_read += bytes_read;
    }

    if(fsio->bytes_read < (size_t)*bytes)
    {
        *bytes = fsio->bytes_read;
        return -1;
    }

    if(fsio->hook[FSIO_EVENT_FILE_READ] != NULL)
    {
        hook = fsio->hook[FSIO_EVENT_FILE_READ];
        anything = fsio->anything[FSIO_EVENT_FILE_READ];

        fsio_data.data = NULL;
        fsio_data.data_sz = fsio->bytes_read;
        fsio_data.anything = anything;

        hook(fsio,FSIO_EVENT_FILE_READ,&fsio_data);
    }

    *bytes = fsio->bytes_read;

    return 0;
}

int
fsio_file_copy(fsio_t *fsio,ssize_t *bytes)
{
    ssize_t         bytes_read = 0;
    ssize_t         bytes_written = 0;
    ssize_t         bytes_total = 0;
    off_t           file_size = 0;
    ssize_t         retval;

    if(fsio == NULL) return -1;
    if(bytes == NULL) return -1;

    _fsio_recommend(fsio,BLOCK_SIZE_KB(128));

    fprintf(stderr,"fsio source blksz: %lu\n",fsio->source_blksz);
    fprintf(stderr,"fsio target blksz: %lu\n",fsio->target_blksz);

    // no-op
    if(*bytes == 0) return 0;

    if((fsio->source_fd <= 0) && (fsio->target_fd <= 0)) return -1;

    if(*bytes == -1)
    {
        retval = fsio_get_source_size(fsio,&file_size);

        // the above fuction will fail if the file is not a regular file
        if(retval == -1) return -1;

        *bytes = file_size;
    }

    // zero out our counters
    _fsio_reset_counters(fsio);

    while(bytes_total < *bytes)
    {
        bytes_read = _fsio_read_block(fsio);

        // were done
        if(bytes_read == 0) break;

        bytes_written = _fsio_write_blocks(fsio);

        if(bytes_written != bytes_read) break;

        bytes_total += bytes_written;
    }

    if(bytes_total < *bytes)
    {
        *bytes = bytes_total;
        return -1;
    }

    *bytes = bytes_total;
    return 0;
}

int
fsio_get_source_size(fsio_t *fsio,off_t *size)
{
    struct stat     file_stats;

    if(fsio == NULL) return -1;
    if(size == NULL) return -1;

    if(fsio->source_fd <= 0) return -1;

    fstat(fsio->source_fd,&file_stats);

    if(!(S_ISREG(file_stats.st_mode))) return -1;

    *size = file_stats.st_size;

    return 0;
}

ssize_t
fsio_get_buffer(fsio_t *fsio,char **buffer)
{
    if(fsio == NULL) return -1;

    *buffer = fsio->buffer;

    return fsio->data_sz;
}

void
fsio_set_hook(fsio_t *fsio,int event,FSIO_Callback hook)
{
    if(fsio == NULL) return;
    if(hook == NULL) return;
    if(event > FSIO_EVENT_ENUM_MAX - 1) return;

    fsio->hook[event] = hook;

    return;
}

void
fsio_set_hook_data(fsio_t *fsio,int event,void *anything)
{
    if(fsio == NULL) return;
    if(event > FSIO_EVENT_ENUM_MAX - 1) return;

    fsio->anything[event] = anything;

    return;
}


void*
fsio_get_hook_data(fsio_t *fsio,int event)
{
    if(fsio == NULL) return NULL;
    if(event > FSIO_EVENT_ENUM_MAX - 1) return NULL;

    return fsio->anything[event];
}

void
fsio_destroy(fsio_t *fsio,bool close_fds)
{
    if(fsio == NULL) return;

    if(close_fds)
    {
        if(fsio->source_fd > 0) close(fsio->source_fd);

        if(fsio->target_fd > 0) close(fsio->target_fd);
    }

    if(fsio->buffer != NULL) free(fsio->buffer);

    free(fsio);
}

// end of public api funcs

static int
_fsio_recommend(fsio_t *fsio,unsigned long min_blksz)
{
    struct statvfs  vfs_info;
    int             retval;

    if(fsio == NULL) return -1;
    if(min_blksz == 0) return -1;

    if(fsio->source_fd > 0)
    {
        do
        {
            retval = fstatvfs(fsio->source_fd,&vfs_info);
        }
        while((retval != 1) && (errno == EINTR));

        if(retval == 0)
        {
            fsio->source_blksz = vfs_info.f_bsize;

            if(fsio->source_blksz < min_blksz)
            {
                fprintf(stderr,
                    "_fsio_recommend() low source: %lu\n",fsio->source_blksz);
                fsio->source_blksz = min_blksz;
            }
        }
    }

    if(fsio->target_fd > 0)
    {
        do
        {
            retval = fstatvfs(fsio->target_fd,&vfs_info);
        }
        while((retval == -1) && (errno == EINTR));

        if(retval == 0)
        {
            fsio->target_blksz = vfs_info.f_bsize;

            if(fsio->target_blksz < min_blksz)
            {
                fprintf(stderr,
                    "_fsio_recommend() low target: %lu\n",fsio->target_blksz);
                fsio->target_blksz = min_blksz;
            }
        }
    }

    return 0;
}

static void
_fsio_reset_counters(fsio_t *fsio)
{
    if(fsio == NULL) return;

    fsio->blocks_read = 0;
    fsio->bytes_read = 0;
    fsio->bytes_written = 0;

    return;
}

static ssize_t
_fsio_read_block(fsio_t *fsio)
{
    ssize_t         bytes_to_read = 0;
    ssize_t         bytes_read = 0;
    ssize_t         bytes_total = 0;
    FSIO_Callback   hook;
    void            *anything;
    fsio_data_t     fsio_data = { .data = NULL, .data_sz = 0};

    if(fsio == NULL) return -1;
    if(fsio->source_fd <= 0) return -1;

    // make sure source block size is set
    if(fsio->source_blksz == 0)
    {
        _fsio_recommend(fsio,BLOCK_SIZE_KB(128));
    }

    if(fsio->buffer_sz != fsio->source_blksz)
    {
        fsio->buffer = (char*)realloc(fsio->buffer,fsio->source_blksz);

        fsio->buffer_sz = fsio->source_blksz;
    }

    bytes_to_read = fsio->buffer_sz;

    while(bytes_to_read > 0)
    {
        bytes_read = read(fsio->source_fd,fsio->buffer,bytes_to_read);

        // read was interrupted by a signal.  try again.
        if((bytes_read == -1) && (errno == EINTR)) continue;

        // end of file reached
        if(bytes_read == 0) break;

        bytes_to_read -= bytes_read;
        bytes_total += bytes_read;
    }

    if(bytes_read == -1) return -1;

    if(fsio->hook[FSIO_EVENT_BLOCK_READ] != NULL)
    {
        hook = fsio->hook[FSIO_EVENT_BLOCK_READ];
        anything = fsio->anything[FSIO_EVENT_BLOCK_READ];

        fsio_data.data = fsio->buffer;
        fsio_data.data_sz = bytes_total;
        fsio_data.anything = anything;

        hook(fsio,FSIO_EVENT_BLOCK_READ,&fsio_data);
    }

    return bytes_total;
}

static ssize_t
_fsio_write_blocks(fsio_t *fsio)
{
    ssize_t         bytes_to_write = 0;
    ssize_t         bytes_written = 0;
    ssize_t         bytes_total = 0;
    FSIO_Callback   hook;
    void            *anything;
    fsio_data_t     fsio_data = { .data = NULL, .data_sz = 0 };

    if(fsio == NULL) return -1;
    if(fsio->buffer == NULL) return -1;

    if(fsio->target_fd <= 0) return -1;

    if(fsio->target_blksz == 0)
    {
        _fsio_recommend(fsio,BLOCK_SIZE_KB(128));
    }

    bytes_to_write = fsio->data_sz;

    while(bytes_to_write > 0)
    {
        bytes_written = write(fsio->target_fd,fsio->buffer,bytes_to_write);

        if((bytes_written == -1) && (errno == EINTR)) continue;

        if(bytes_written == -1) break;

        bytes_to_write -= bytes_written;
        bytes_total += bytes_written;
    }

    if(bytes_written == -1) return -1;

    if(fsio->hook[FSIO_EVENT_BLOCK_WRITTEN] != NULL)
    {
        hook = fsio->hook[FSIO_EVENT_BLOCK_WRITTEN];
        anything = fsio->anything[FSIO_EVENT_BLOCK_WRITTEN];

        fsio_data.data = fsio->buffer;
        fsio_data.data_sz = bytes_total;
        fsio_data.anything = anything;

        hook(fsio,FSIO_EVENT_BLOCK_WRITTEN,&fsio_data);
    }

    return bytes_written;
}
