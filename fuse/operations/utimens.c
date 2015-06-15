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
//#include <stdlib.h>
//#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
//#include <sys/stat.h>
//#include <fcntl.h>
//#include <fuse/fuse_common.h>
//#include <stdint.h>
//#include <libgen.h>
#include <stdbool.h>
#include <time.h>
//#include <openssl/sha.h>
//#include <sys/statvfs.h>

//#include "../../mfapi/account.h"
//#include "../../mfapi/mfconn.h"
#include "../../mfapi/apicalls.h"
//#include "../../utils/stringv.h"
//#include "../../utils/hash.h"
#include "../hashtbl.h"
#include "../operations.h"


int mediafirefs_utimens(const char *path, const struct timespec tv[2])
{
    printf("FUNCTION: utimens. path: %s\n", path);

    time_t          since_epoch;
    struct tm       local_time;
    static int      b_tzset = 0;        // has tzset() been called
    char            print_time[24];     // buf for print friendly local time
    bool            is_file = 0;
    const char     *key = NULL;
    int             retval;

    struct mediafirefs_context_private *ctx;

    (void)path;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    is_file = folder_tree_path_is_file(ctx->tree, ctx->conn, path);

    // look up the key
    key = folder_tree_path_get_key(ctx->tree, ctx->conn, path);
    if (key == NULL) {
        fprintf(stderr, "key is NULL\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOENT;
    }
    // call tzset if needed
    if (b_tzset != 1) {
        tzset();
        b_tzset = 1;
    }
    // we only care about mtime (not atime) and seconds because the
    // could doesn't handle nanoseconds
    memcpy(&since_epoch, &tv[1].tv_sec, sizeof(time_t));

    if (localtime_r((const time_t *)&since_epoch, &local_time) == NULL) {
        fprintf(stderr, "utimens not implemented\n");
        pthread_mutex_unlock(&(ctx->mutex));

        return -ENOSYS;
    }

    memset(print_time, 0, sizeof(print_time));
    strftime(print_time, sizeof(print_time) - 1, "%F %T", &local_time);

    fprintf(stderr, "utimens file set: %s\n", print_time);

    if (is_file) {
        retval = mfconn_api_file_update(ctx->conn, key, NULL, print_time, false);
    } else {
        retval = mfconn_api_folder_update(ctx->conn, key, NULL, print_time);
    }

    if (retval == -1) {
        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOENT;
    }

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}
