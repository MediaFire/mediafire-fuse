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

#include <stddef.h>
#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <string.h>
#include <errno.h>
//#include <sys/stat.h>
//#include <fcntl.h>
//#include <fuse/fuse_common.h>
//#include <stdint.h>
//#include <libgen.h>
#include <stdbool.h>
#include <pthread.h>

#include <fuse/fuse.h>

//#include <time.h>
//#include <openssl/sha.h>
//#include <sys/statvfs.h>

#include "../../mfapi/account.h"
//#include "../../mfapi/mfconn.h"
#include "../../mfapi/apicalls.h"
//#include "../../utils/stringv.h"
//#include "../../utils/hash.h"
#include "../hashtbl.h"
#include "../operations.h"


int mediafirefs_unlink(const char *path)
{
    printf("FUNCTION: unlink. path: %s\n", path);
    const char     *key;
    int             retval;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    /* no need to check
     *  - if path is directory
     *  - if directory is empty
     *  - if directory is root
     *
     * because getattr was called before and already made sure
     */

    key = folder_tree_path_get_key(ctx->tree, ctx->conn, path);
    if (key == NULL) {
        fprintf(stderr, "key is NULL\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOENT;
    }

    retval = mfconn_api_file_delete(ctx->conn, key);
    if (retval != 0) {
        fprintf(stderr, "mfconn_api_file_create unsuccessful\n");
        pthread_mutex_unlock(&(ctx->mutex));
        // FIXME: find better errno in this case
        return -EAGAIN;
    }
    else
    {
        account_add_state_flags(ctx->account, ACCOUNT_FLAG_DIRTY_SIZE);
    }

    /* retrieve remote changes to not get out of sync */
    folder_tree_update(ctx->tree, ctx->conn, true);

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

