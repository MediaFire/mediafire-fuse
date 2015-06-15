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
//#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
//#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
//#include <fcntl.h>
#include <fuse/fuse_common.h>
#include <stdint.h>
//#include <libgen.h>
#include <stdbool.h>
//#include <time.h>
//#include <openssl/sha.h>
//#include <sys/statvfs.h>

//#include "../../mfapi/account.h"
//#include "../../mfapi/mfconn.h"
//#include "../../mfapi/apicalls.h"
#include "../../utils/stringv.h"
//#include "../../utils/hash.h"
#include "../hashtbl.h"
#include "../operations.h"


/*
 * this is called if the file does not exist yet. It will create a temporary
 * file and open it.
 * once the file gets closed, it will be uploaded.
 */
int mediafirefs_create(const char *path, mode_t mode,
                       struct fuse_file_info *file_info)
{
    printf("FUNCTION: create. path: %s\n", path);

    (void)mode;

    int             fd;
    struct mediafirefs_openfile *openfile;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fd = folder_tree_tmp_open(ctx->tree);
    if (fd < 0) {
        fprintf(stderr, "folder_tree_tmp_open failed\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return -EACCES;
    }

    openfile = malloc(sizeof(struct mediafirefs_openfile));
    openfile->fd = fd;
    openfile->is_local = true;
    openfile->is_readonly = false;
    openfile->path = strdup(path);
    openfile->is_flushed = false;
    file_info->fh = (uintptr_t) openfile;

    // add to writefiles
    stringv_add(ctx->sv_writefiles, path);

    pthread_mutex_unlock(&(ctx->mutex));

    mediafirefs_flush(path, file_info);

    return 0;
}

