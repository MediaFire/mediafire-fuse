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
#include <unistd.h>
#include <string.h>
//#include <errno.h>
//#include <sys/stat.h>
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
#include "../../mfapi/apicalls.h"
#include "../../utils/stringv.h"
//#include "../../utils/hash.h"
#include "../hashtbl.h"
#include "../operations.h"


/*
 * note: the return value of release() is ignored by fuse
 *
 */
int mediafirefs_release(const char *path, struct fuse_file_info *file_info)
{
    printf("FUNCTION: release. path: %s\n", path);

    (void)path;

    struct mediafirefs_context_private *ctx;
    struct mediafirefs_openfile *openfile;
    struct mfconn_upload_check_result check_result;

    /* filesystems should not assume that flush will ever be called.
     * since we do our uploading in flush, we must make sure flush is called*/
    mediafirefs_flush(path, file_info);

    ctx = fuse_get_context()->private_data;

    // zero out check result to prevent spurious results later
    memset(&check_result,0,sizeof(check_result));

    pthread_mutex_lock(&(ctx->mutex));

    openfile = (struct mediafirefs_openfile *)(uintptr_t) file_info->fh;

    // if file was opened as readonly then it just has to be closed
    if (openfile->is_readonly) {
        // remove this entry from readonlyfiles
        if (stringv_del(ctx->sv_readonlyfiles, openfile->path) != 0) {
            fprintf(stderr, "FATAL: readonly entry %s not found\n",
                    openfile->path);
            exit(1);
        }

        close(openfile->fd);
        free(openfile->path);
        free(openfile);
        pthread_mutex_unlock(&(ctx->mutex));
        return 0;
    }
    // if the file is not readonly, its entry in writefiles has to be removed
    if (stringv_del(ctx->sv_writefiles, openfile->path) != 0) {
        fprintf(stderr, "FATAL: writefiles entry %s not found\n",
                openfile->path);
        exit(1);
    }

    close(openfile->fd);

    free(openfile->path);
    free(openfile);

    folder_tree_update(ctx->tree, ctx->conn, true);

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

