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
//#include <sys/stat.h>
//#include <fcntl.h>
#include <fuse/fuse_common.h>
#include <stdint.h>
#include <libgen.h>
#include <stdbool.h>
//#include <time.h>
#include <openssl/sha.h>
//#include <sys/statvfs.h>

#include "../../mfapi/account.h"
#include "../../mfapi/mfconn.h"
#include "../../mfapi/apicalls.h"
//#include "../../utils/stringv.h"
#include "../../utils/hash.h"
#include "../hashtbl.h"
#include "../operations.h"

int mediafirefs_flush(const char *path, struct fuse_file_info *file_info)
{
    printf("FUNCTION: flush. path: %s\n", path);

    FILE           *fh;
    char           *file_name;
    char           *dir_name;
    const char     *folder_key;
    char           *upload_key;
    char           *temp1;
    char           *temp2;
    int             retval;
    struct mediafirefs_context_private *ctx;
    struct mediafirefs_openfile *openfile;
    struct mfconn_upload_check_result check_result;
    unsigned char   bhash[SHA256_DIGEST_LENGTH];
    char           *hash;
    uint64_t        size;

    openfile = (struct mediafirefs_openfile *)(uintptr_t) file_info->fh;

    if (openfile->is_flushed) {
	return 0;
    }

    ctx = fuse_get_context()->private_data;

    // zero out check result to prevent spurious results later
    memset(&check_result,0,sizeof(check_result));

    pthread_mutex_lock(&(ctx->mutex));


    if (openfile->is_readonly) {
	/* nothing to do here */
	pthread_mutex_unlock(&(ctx->mutex));
	return 0;
    }
        // if the file only exists locally, an initial upload has to be done
    if (openfile->is_local) {
        // pass a copy because dirname and basename may modify their argument
        temp1 = strdup(openfile->path);
        file_name = basename(temp1);
        temp2 = strdup(openfile->path);
        dir_name = dirname(temp2);

        fh = fdopen(openfile->fd, "r");
        rewind(fh);

        folder_key = folder_tree_path_get_key(ctx->tree, ctx->conn, dir_name);

	    size = -1;
        retval = calc_sha256(fh, bhash, &size);
        rewind(fh);

        if (retval != 0) {
            fprintf(stderr, "failed to calculate hash\n");
            free(temp1);
            free(temp2);
            pthread_mutex_unlock(&(ctx->mutex));
            return -EACCES;
        }

        hash = binary2hex(bhash, SHA256_DIGEST_LENGTH);

        retval = mfconn_api_upload_check(ctx->conn, file_name, hash, size,
                                         folder_key, &check_result);

        if (retval != 0) {
            free(temp1);
            free(temp2);
            free(hash);
            fprintf(stderr, "mfconn_api_upload_check failed\n");
            fprintf(stderr, "file_name: %s\n",file_name);
            fprintf(stderr, "hash: %s\n",hash);
            fprintf(stderr, "size: %jd\n",size);
            fprintf(stderr, "folder_key: %s\n",folder_key);
            pthread_mutex_unlock(&(ctx->mutex));
            return -EACCES;
        }

        if (check_result.hash_exists) {
            // hash exists, so use upload/instant

            retval = mfconn_api_upload_instant(ctx->conn,
                                               file_name, hash, size,
                                               folder_key);

            free(temp1);
            free(temp2);
            free(hash);

            if (retval != 0) {
                fprintf(stderr, "mfconn_api_upload_instant failed\n");
                pthread_mutex_unlock(&(ctx->mutex));
                return -EACCES;
            }
        } else {
            // hash does not exist, so do full upload
            upload_key = NULL;
            retval = mfconn_api_upload_simple(ctx->conn, folder_key,
                                              fh, file_name, true, &upload_key);

            free(temp1);
            free(temp2);
            free(hash);

            if (retval != 0 || upload_key == NULL) {

                fprintf(stderr, "mfconn_api_upload_simple failed\n");
                fprintf(stderr, "file_name: %s\n",file_name);
                fprintf(stderr, "hash: %s\n",hash);
                fprintf(stderr, "size: %jd\n",size);
                fprintf(stderr, "folder_key: %s\n",folder_key);

                pthread_mutex_unlock(&(ctx->mutex));
                return -EACCES;
            }
            // poll for completion
            retval = mfconn_upload_poll_for_completion(ctx->conn, upload_key);
            free(upload_key);

            if (retval != 0) {
                fprintf(stderr, "mfconn_upload_poll_for_completion failed\n");
                pthread_mutex_unlock(&(ctx->mutex));
                return -1;
            }
            else
            {
                account_add_state_flags(ctx->account, ACCOUNT_FLAG_DIRTY_SIZE);
            }
        }

        folder_tree_update(ctx->tree, ctx->conn, true);
	    openfile->is_flushed = true;
        pthread_mutex_unlock(&(ctx->mutex));
        return 0;
    }

    // the file was not opened readonly and also existed on the remote
    // thus, we have to check whether any changes were made and if yes, upload
    // a patch

    retval = folder_tree_upload_patch(ctx->tree, ctx->conn, openfile->path);

    if (retval != 0) {
	    fprintf(stderr, "folder_tree_upload_patch failed\n");
	    pthread_mutex_unlock(&(ctx->mutex));
	    return -EACCES;
    }
    else
    {
        account_add_state_flags(ctx->account, ACCOUNT_FLAG_DIRTY_SIZE);
    }


    folder_tree_update(ctx->tree, ctx->conn, true);

    openfile->is_flushed = true;
    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

