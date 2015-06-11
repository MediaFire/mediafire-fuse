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
#include <pthread.h>
#include <string.h>
#include <errno.h>
//#include <sys/stat.h>
//#include <fcntl.h>
//#include <fuse/fuse_common.h>
//#include <stdint.h>
#include <libgen.h>
#include <stdbool.h>
//#include <time.h>
//#include <openssl/sha.h>
//#include <sys/statvfs.h>

//#include "../../mfapi/account.h"
//#include "../../mfapi/mfconn.h"
#include "../../mfapi/apicalls.h"
//#include "../../utils/stringv.h"
//#include "../../utils/hash.h"
#include "../hashtbl.h"
#include "../operations.h"

int mediafirefs_rename(const char *oldpath, const char *newpath)
{
    printf("FUNCTION: rename. oldpath: %s, newpath %s\n", oldpath, newpath);

    char           *temp1;
    char           *temp2;
    char           *olddir;
    char           *newdir;
    char           *oldname;
    char           *newname;
    int             retval;
    struct mediafirefs_context_private *ctx;
    bool            is_file;
    const char     *key;
    const char     *key1;
    const char     *folderkey;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    is_file = folder_tree_path_is_file(ctx->tree, ctx->conn, oldpath);

    key = folder_tree_path_get_key(ctx->tree, ctx->conn, oldpath);
    if (key == NULL) {
        fprintf(stderr, "key is NULL\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOENT;
    }
    // check if the directory changed
    temp1 = strdup(oldpath);
    temp2 = strdup(newpath);
    olddir = strdup(dirname(temp1));
    newdir = strdup(dirname(temp2));

    if (strcmp(olddir, newdir) != 0) {
        folderkey = folder_tree_path_get_key(ctx->tree, ctx->conn, newdir);
        if (key == NULL) {
            fprintf(stderr, "key is NULL\n");
            free(temp1);
            free(temp2);
        free(olddir);
        free(newdir);
            pthread_mutex_unlock(&(ctx->mutex));
            return -ENOENT;
        }

        if (is_file) {
            retval = mfconn_api_file_move(ctx->conn, key, folderkey);
        } else {
            retval = mfconn_api_folder_move(ctx->conn, key, folderkey);
        }
        if (retval != 0) {
            if (is_file) {
                fprintf(stderr, "mfconn_api_file_move failed\n");
            } else {
                fprintf(stderr, "mfconn_api_folder_move failed\n");
            }
            free(temp1);
            free(temp2);
        free(olddir);
        free(newdir);
            pthread_mutex_unlock(&(ctx->mutex));
            return -ENOENT;
        }
    }

    free(temp1);
    free(temp2);
    free(olddir);
    free(newdir);


    // check if the name changed
    temp1 = strdup(oldpath);
    temp2 = strdup(newpath);
    oldname = strdup(basename(temp1));
    newname = strdup(basename(temp2));

    // check if the name changed
    temp1 = strdup(oldpath);
    temp2 = strdup(newpath);
    oldname = strdup(basename(temp1));
    newname = strdup(basename(temp2));

    if (strcmp(oldname, newname) != 0) {
        if (is_file) {
        key1 = folder_tree_path_get_key(ctx->tree, ctx->conn,
                        newpath);
        if (key1) {
        /* delete existing destination file */
        mfconn_api_file_delete(ctx->conn, key1);
        }
            retval = mfconn_api_file_update(ctx->conn, key, newname, NULL, false);
        } else {
            retval = mfconn_api_folder_update(ctx->conn, key, newname, NULL);
        }
        if (retval != 0) {
            if (is_file) {
                fprintf(stderr, "mfconn_api_file_update failed\n");
            } else {
                fprintf(stderr, "mfconn_api_folder_update failed\n");
            }

            free(temp1);
            free(temp2);
            free(oldname);
            free(newname);

            pthread_mutex_unlock(&(ctx->mutex));
            return -ENOENT;
        }
    }

    free(temp1);
    free(temp2);
    free(oldname);
    free(newname);

    folder_tree_update(ctx->tree, ctx->conn, true);

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

