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

//#include <fuse/fuse.h>
//#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <string.h>
//#include <errno.h>
//#include <sys/stat.h>
//#include <fcntl.h>
//#include <fuse/fuse_common.h>
//#include <stdint.h>
//#include <libgen.h>
//#include <stdbool.h>
//#include <time.h>
//#include <openssl/sha.h>
//#include <sys/statvfs.h>

//#include "../../mfapi/account.h"
#include "../../mfapi/mfconn.h"
//#include "../../mfapi/apicalls.h"
//#include "../../utils/stringv.h"
//#include "../../utils/hash.h"
#include "../hashtbl.h"
#include "../operations.h"

void mediafirefs_destroy(void *user_ptr)
{
    printf("FUNCTION: destroy\n");
    FILE           *fd;
    struct mediafirefs_context_private *ctx;

    ctx = (struct mediafirefs_context_private *)user_ptr;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "storing hashtable\n");

    fd = fopen(ctx->dircache, "w+");

    if (fd == NULL) {
        fprintf(stderr, "cannot open %s for writing\n", ctx->dircache);
        pthread_mutex_unlock(&(ctx->mutex));
        return;
    }

    folder_tree_store(ctx->tree, fd);

    fclose(fd);

    folder_tree_destroy(ctx->tree);

    mfconn_destroy(ctx->conn);

    pthread_mutex_unlock(&(ctx->mutex));
}

