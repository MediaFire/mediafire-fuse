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

#define _POSIX_C_SOURCE 200809L // for strdup and struct timespec
#define _XOPEN_SOURCE 700       // for S_IFDIR and S_IFREG (on linux,
                                // posix_c_source is enough but this is needed
                                // on freebsd)

#define FUSE_USE_VERSION 30

#include <fuse/fuse.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
//#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
//#include <sys/stat.h>
//#include <fcntl.h>
//#include <fuse/fuse_common.h>
//#include <stdint.h>
//#include <libgen.h>
//#include <stdbool.h>
//#include <time.h>
//#include <openssl/sha.h>
#include <sys/statvfs.h>

#include "../../mfapi/account.h"
//#include "../../mfapi/mfconn.h"
#include "../../mfapi/apicalls.h"
//#include "../../utils/stringv.h"
//#include "../../utils/hash.h"
//#include "../hashtbl.h"
#include "../operations.h"

int mediafirefs_statfs(const char *path, struct statvfs *buf)
{
    printf("FUNCTION: statfs. path: %s\n", path);

    // declare this static to cache results across repeated calls
    static char         space_total[128];
    static char         space_used[128];
    static uintmax_t    bytes_total = 0;
    static uintmax_t    bytes_used = 0;
    static uintmax_t    bytes_free = 0;

    unsigned int        state_flags = 0;

    (void)path;
    (void)buf;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;


    pthread_mutex_lock(&(ctx->mutex));

    // instantiate an account object and set the dirty flag on the size
    if(ctx->account == NULL)
    {
        ctx->account = account_alloc();
        account_add_state_flags(ctx->account, ACCOUNT_FLAG_DIRTY_SIZE);
    }

    state_flags = account_get_state_flags(ctx->account);

    if(state_flags & ACCOUNT_FLAG_DIRTY_SIZE)
    {
        memset(space_total, 0, sizeof(space_total));
        memset(space_used, 0, sizeof(space_used));

        // TODO:  it's bad practice for the API to modify the object directly
        mfconn_api_user_get_info(ctx->conn, ctx->account);
        account_del_state_flags(ctx->account, ACCOUNT_FLAG_DIRTY_SIZE); 

        account_get_space_total(ctx->account, space_total, 128);
        account_get_space_used(ctx->account, space_used, 128);

        bytes_total = atoll(space_total);
        bytes_used = atoll(space_used);

        bytes_free = bytes_total - bytes_used;
    }


    if (bytes_total == 0) {

        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOSYS;         // returning -ENOENT might make more sense
    }

    fprintf(stderr, "account size: %ju bytes\n", bytes_total);
    fprintf(stderr, "account used: %ju bytes\n", bytes_used);
    fprintf(stderr, "account free: %ju bytes\n", bytes_free);

    // there is no block size on the remote so we will fake it with a
    // block size of 64k.  FUSE requires that block size be a multiple
    // of 4096 bytes (4k)
    buf->f_bsize = 65536;
    buf->f_frsize = 65536;
    buf->f_blocks = (bytes_total / 65535);
    buf->f_bfree = (bytes_free / 65536);
    buf->f_bavail = (bytes_free / 65536);

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

