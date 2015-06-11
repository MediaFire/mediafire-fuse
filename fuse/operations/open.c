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
//#include <errno.h>
//#include <sys/stat.h>
//#include <fcntl.h>
#include <stdint.h>
//#include <libgen.h>
#include <stdbool.h>
//#include <time.h>
//#include <openssl/sha.h>
//#include <sys/statvfs.h>

#ifdef __linux
#include <bits/fcntl-linux.h>
#endif

#include <fuse/fuse_common.h>


//#include "../../mfapi/account.h"
//#include "../../mfapi/mfconn.h"
//#include "../../mfapi/apicalls.h"
#include "../../utils/stringv.h"
//#include "../../utils/hash.h"
#include "../hashtbl.h"
#include "../operations.h"



/*
 * the following restrictions apply:
 *  1. a file can be opened in read-only mode more than once at a time
 *  2. a file can only be be opened in write-only or read-write mode
 *     more than once at a time.
 *  3. a file that is only local and has not been uploaded yet cannot be read
 *     from
 *  4. a file that has opened in any way will not be updated to its latest
 *     remote revision until all its opened handles are closed
 *
 *  Point 3 is enforced by the lookup in the hashtable failing.
 *
 *  Point 4 is enforced by checking if the current path is in the writefiles or
 *  readonlyfiles string vector and if yes, no updating will be done.
 */
int mediafirefs_open(const char *path, struct fuse_file_info *file_info)
{
    printf("FUNCTION: open. path: %s\n", path);
    int             fd;
    struct mediafirefs_openfile *openfile;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fd = folder_tree_open_file(ctx->tree, ctx->conn, path, file_info->flags,
                               true);
    if (fd < 0) {
        fprintf(stderr, "folder_tree_file_open unsuccessful\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return fd;
    }

    openfile = malloc(sizeof(struct mediafirefs_openfile));
    openfile->fd = fd;
    openfile->is_local = false;
    openfile->path = strdup(path);
    openfile->is_flushed = true;

    if ((file_info->flags & O_ACCMODE) == O_RDONLY) {
        openfile->is_readonly = true;
        // add to readonlyfiles
        stringv_add(ctx->sv_readonlyfiles, path);
    } else {
        openfile->is_readonly = false;
        // add to writefiles
        stringv_add(ctx->sv_writefiles, path);
    }

    file_info->fh = (uintptr_t) openfile;

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

