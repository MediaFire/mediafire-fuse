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
#include <pthread.h>
#include <unistd.h>
//#include <string.h>
//#include <errno.h>
#include <sys/stat.h>
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
//#include "../../mfapi/apicalls.h"
#include "../../utils/stringv.h"
//#include "../../utils/hash.h"
#include "../hashtbl.h"
#include "../operations.h"


int mediafirefs_getattr(const char *path, struct stat *stbuf)
{
    printf("FUNCTION: getattr. path: %s\n", path);
    /*
     * since getattr is called before every other call (except for getattr,
     * read and write) wee only call folder_tree_update in the getattr call
     * and not the others
     */
    struct mediafirefs_context_private *ctx;
    int             retval;
    time_t          now;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    now = time(NULL);
    if (now - ctx->last_status_check > ctx->interval_status_check) {
        folder_tree_update(ctx->tree, ctx->conn, false);
        ctx->last_status_check = now;
    }

    retval = folder_tree_getattr(ctx->tree, ctx->conn, path, stbuf);

    if (retval != 0 && stringv_mem(ctx->sv_writefiles, path)) {
        stbuf->st_uid = geteuid();
        stbuf->st_gid = getegid();
        stbuf->st_ctime = 0;
        stbuf->st_mtime = 0;
        stbuf->st_mode = S_IFREG | 0666;
        stbuf->st_nlink = 1;
        stbuf->st_atime = 0;
        stbuf->st_size = 0;
        retval = 0;
    }

    pthread_mutex_unlock(&(ctx->mutex));

    return retval;
}


