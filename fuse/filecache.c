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

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "../utils/hash.h"
#include "../utils/xdelta3.h"
#include "../mfapi/file.h"
#include "../mfapi/apicalls.h"
#include "../mfapi/patch.h"
#include "../mfapi/mfconn.h"
#include "../utils/http.h"
#include "../utils/strings.h"
#include "../utils/fsio.h"

#ifndef TRUE
#define TRUE true
#endif

static int      filecache_update_file(const char *filecache_path,
                                      mfconn * conn, const char *quickkey,
                                      uint64_t local_revision,
                                      uint64_t remote_revision);
static int      filecache_download_file(const char *filecache_path,
                                        const char *quickkey,
                                        uint64_t remote_revision,
                                        mfconn * conn);
static int      filecache_download_patch(mfconn * conn, const char *quickkey,
                                         uint64_t source_revision,
                                         uint64_t target_revision,
                                         const char *phash,
                                         const char *filecache_path);
static int      filecache_patch_file(const char *filecache_path,
                                     const char *quickkey,
                                     uint64_t source_revision,
                                     uint64_t target_revision);

int filecache_upload_patch(const char *quickkey, uint64_t local_revision,
                           const char *filecache_path, mfconn * conn)
{
    FILE           *source_fh;
    FILE           *target_fh;
    FILE           *patchfile_fh;
    unsigned char   hash[SHA256_DIGEST_LENGTH];
    char           *source_hash;
    char           *target_hash;
    uint64_t        target_size;
    char           *cachefile;
    char           *newfile;
    char           *patch_file;
    int             retval;
    char           *upload_key;

    cachefile = strdup_printf("%s/%s_%d", filecache_path, quickkey,
                              local_revision);

    source_fh = fopen(cachefile, "r");
    if (source_fh == NULL) {
        fprintf(stderr, "cannot open %s\n", cachefile);
        free(cachefile);
        return -1;
    }
    free(cachefile);

    newfile = strdup_printf("%s/%s_%d_new", filecache_path, quickkey,
                            local_revision);

    target_fh = fopen(newfile, "r");
    if (target_fh == NULL) {
        fprintf(stderr, "cannot open %s\n", newfile);
        free(newfile);
        fclose(source_fh);
        return -1;
    }
    free(newfile);

    retval = calc_sha256(source_fh, hash, NULL);

    if (retval != 0) {
        fprintf(stderr, "failed to calculate hash\n");
        fclose(source_fh);
        fclose(target_fh);
        return -1;
    }

    source_hash = binary2hex(hash, SHA256_DIGEST_LENGTH);

    target_size = -1;
    retval = calc_sha256(target_fh, hash, &target_size);

    if (retval != 0) {
        fprintf(stderr, "failed to calculate hash\n");
        fclose(source_fh);
        fclose(target_fh);
        return -1;
    }

    target_hash = binary2hex(hash, SHA256_DIGEST_LENGTH);

    if (strcmp(source_hash, target_hash) == 0) {
        // no changes were done
        free(source_hash);
        free(target_hash);
        return 0;
    }

    patch_file = strdup_printf("%s/%s_patch_%d_new", filecache_path, quickkey,
                               local_revision);

    patchfile_fh = fopen(patch_file, "w");
    if (patchfile_fh == NULL) {
        fprintf(stderr, "cannot open %s\n", patch_file);
        fclose(source_fh);
        fclose(target_fh);
        return -1;
    }

    rewind(source_fh);
    rewind(target_fh);
    retval = xdelta3_diff(source_fh, target_fh, patchfile_fh);
    fclose(source_fh);
    fclose(target_fh);
    fclose(patchfile_fh);

    upload_key = NULL;
    retval = mfconn_api_upload_patch(conn, quickkey, source_hash, target_hash,
                                     target_size, patch_file, &upload_key);

    if (retval != 0 || upload_key == NULL) {
        fprintf(stderr, "mfconn_api_upload_patch failed\n");
        return -1;
    }
    // poll for completion
    retval = mfconn_upload_poll_for_completion(conn, upload_key);
    free(upload_key);

    if (retval != 0) {
        fprintf(stderr, "mfconn_upload_poll_for_completion failed\n");
        return -1;
    }

    return 0;
}

int filecache_get_new_and_old_sizes(const char *quickkey,
				    uint64_t local_revision,
				    const char *filecache_path,
				    off_t *newsize, off_t *oldsize)
{
    char           *cachefile;
    char           *newfile;
    int            retval;

    cachefile = strdup_printf("%s/%s_%d", filecache_path, quickkey,
                              local_revision);
    newfile = strdup_printf("%s/%s_%d_new", filecache_path, quickkey,
                            local_revision);
    
    struct stat st;		/* count file size */
    retval = stat(cachefile, &st);
    if (retval == -1) {
	perror("fstat");
	*oldsize = -1;
	return -1;
    }
    *oldsize = st.st_size;
    fprintf(stderr, "Old file size is: %zd\n", st.st_size);

    retval = stat(newfile, &st);
    if (retval == -1) {
	perror("fstat");
	*newsize = -1;
	return -1;
    }
    *newsize = st.st_size;
    fprintf(stderr, "New file size is: %zd\n", st.st_size);

    return 0;
}
int filecache_truncate_file(const char *quickkey, const char *key,
			    uint64_t local_revision,
			    uint64_t remote_revision,
			    const char *filecache_path, mfconn * conn)
{
    char           *filepath;
    int             retval;
    int             fd;

    /* truncate file on remote */
    retval = mfconn_api_file_update(conn, key, NULL, NULL, true);
    if (retval == -1) {
	fprintf(stderr, "file update failed\n");
	return -1;
    }

    /* truncate file on local */
    filepath = strdup_printf("%s/%s_%d", filecache_path, quickkey,
			      local_revision);
    fd = open(filepath, O_TRUNC | O_WRONLY);
    free(filepath);
    close(fd);

    filepath = strdup_printf("%s/%s_%d", filecache_path, quickkey,
			      remote_revision);
    fd = open(filepath, O_TRUNC | O_WRONLY);
    free(filepath);
    close(fd);

    filepath = strdup_printf("%s/%s_%d_new", filecache_path, quickkey,
			      local_revision);
    fd = open(filepath, O_TRUNC | O_WRONLY);
    free(filepath);
    close(fd);

    filepath = strdup_printf("%s/%s_%d_new", filecache_path, quickkey,
			      remote_revision);
    fd = open(filepath, O_TRUNC | O_WRONLY);
    free(filepath);
    close(fd);

    return 0;
}

int filecache_open_file(const char *quickkey, uint64_t local_revision,
                        uint64_t remote_revision, uint64_t fsize,
                        const unsigned char *fhash,
                        const char *filecache_path, mfconn * conn, mode_t mode,
                        bool update)
{
    char           *cachefile;
    char           *newfile;
    int             fd;
    int             retval;
    int             source;
    int             dest;
    fsio_t          *fsio = NULL;
    ssize_t         bytes_to_copy = -1;     // -1 indicates entire file

    if (update) {
        cachefile = strdup_printf("%s/%s_%d", filecache_path, quickkey,
                                  remote_revision);
    } else {
        cachefile = strdup_printf("%s/%s_%d", filecache_path, quickkey,
                                  local_revision);
    }
    /* check if the requested file is already in the cache */
    if ((mode & O_ACCMODE) == O_RDONLY) {
        // if file is opened in readonly mode, we try to open it directly
        fd = open(cachefile, mode);
        free(cachefile);
        if (fd > 0) {
            /* file existed - return handle */
            return fd;
        }
        // if the file cannot be opened, then it has to be retrieved
    } else {
        // if file is opened writable then a temporary file has to be opened
        // instead to upload a patch if necessary
        if (update) {
            newfile = strdup_printf("%s/%s_%d_new", filecache_path, quickkey,
                                    remote_revision);
        } else {
            newfile = strdup_printf("%s/%s_%d_new", filecache_path, quickkey,
                                    local_revision);
        }

        fd = open(newfile, mode);

        if (fd > 0) {
            /* file existed - return handle */
            free(newfile);
            free(cachefile);
            return fd;
        }
        // the temporary file wasn't available, so we copy it from the
        // original
        source = open(cachefile, O_RDONLY);
        free(cachefile);

        fsio = fsio_create();

        if(source > 0)
            fsio_set_source(fsio,source);

        dest = open(newfile, O_WRONLY | O_CREAT, 0644);

        if(dest > 0)
            fsio_set_target(fsio,dest);

        if (source > 0 && dest > 0) {

            fsio_file_copy(fsio,&bytes_to_copy);

            // fsio_destroy() will close 'source' and 'dest' descriptors when
            // TRUE is passed
            if(fsio != NULL) fsio_destroy(fsio,TRUE);

            fd = open(newfile, mode);
            free(newfile);
            return fd;
        }

        if(fsio != NULL) fsio_destroy(fsio,TRUE);

        free(newfile);
        // if the source file cannot be opened for copying, then it has to be
        // retrieved
    }

    // if no updating is requested and we end up here, then something failed
    // but since we must not update, this is a failure
    if (!update) {
        fprintf(stderr, "no updating but cannot open\n");
        return -1;
    }

    /* if the file with remote revision didn't exist, then check whether an
     * old revision exists and in that case update that.
     *
     * Otherwise, download the file anew */

    cachefile =
        strdup_printf("%s/%s_%d", filecache_path, quickkey, local_revision);
    fd = open(cachefile, O_RDONLY);
    free(cachefile);
    if (fd > 0) {
        close(fd);
        /* file exists, so we have to update it with one or more patches from
         * the remote */
        retval = filecache_update_file(filecache_path, conn, quickkey,
                                       local_revision, remote_revision);
        if (retval != 0) {
            fprintf(stderr, "update_file failed\n");
            return -1;
        }

    } else {
        /* download the file */
        retval = filecache_download_file(filecache_path, quickkey,
                                         remote_revision, conn);
        if (retval != 0) {
            fprintf(stderr, "filecache_download_file failed\n");
            return -1;
        }
    }

    /* check whether the patched or newly downloaded file matches the hash we
     * have stored */
    cachefile =
        strdup_printf("%s/%s_%d", filecache_path, quickkey, remote_revision);
    retval = file_check_integrity(cachefile, fsize, fhash);
    if (retval != 0) {
        fprintf(stderr, "checking integrity failed\n");
        free(cachefile);
        return -1;
    }

    if ((mode & O_ACCMODE) == O_RDONLY) {
        // if file is opened in readonly mode, we open it directly
        fd = open(cachefile, mode);
    } else {
        // if file is opened writable then a temporary file has to be opened
        // instead to upload a patch if necessary
        newfile = strdup_printf("%s/%s_%d_new", filecache_path, quickkey,
                                remote_revision);
        source = open(cachefile, O_RDONLY);
        dest = open(newfile, O_WRONLY | O_CREAT, 0644);

        fsio = fsio_create();

        if(source > 0)
            fsio_set_source(fsio,source);

        if(dest > 0)
            fsio_set_target(fsio,dest);

        if (source > 0 && dest > 0) {

            fsio_file_copy(fsio,&bytes_to_copy);

            if(fsio != NULL) fsio_destroy(fsio,TRUE);
        }

        close(source);
        close(dest);
        fd = open(newfile, mode);
        free(newfile);
    }

    free(cachefile);

    /* return the file handle */
    return fd;
}

static int filecache_download_file(const char *filecache_path,
                                   const char *quickkey,
                                   uint64_t remote_revision, mfconn * conn)
{
    const char     *url;
    mffile         *file;
    mfhttp         *http;
    char           *cachefile;
    int             retval;

    cachefile = strdup_printf("%s/%s_%d", filecache_path, quickkey,
                              remote_revision);

    file = file_alloc();
    retval = mfconn_api_file_get_links(conn, file,
                                       (char *)quickkey,
                                       MFCONN_FILE_LINK_TYPE_DIRECT_DOWNLOAD);

    if (retval != 0) {
        fprintf(stderr, "mfconn_api_file_get_links failed\n");
        free(cachefile);
        file_free(file);
        return -1;
    }

    url = file_get_direct_link(file);

    if (url == NULL) {
        fprintf(stderr, "file_get_direct_link failed\n");
        free(cachefile);
        file_free(file);
        return -1;
    }

    http = http_create();
    retval = http_get_file(http, url, cachefile);
    http_destroy(http);

    if (retval != 0) {
        fprintf(stderr, "download failed\n");
        free(cachefile);
        file_free(file);
        return -1;
    }

    free(cachefile);
    file_free(file);

    return 0;
}

static int filecache_update_file(const char *filecache_path, mfconn * conn,
                                 const char *quickkey,
                                 uint64_t local_revision,
                                 uint64_t remote_revision)
{
    unsigned char   hash2[SHA256_DIGEST_LENGTH];
    int             retval;
    int             i;
    uint64_t        last_target_revision;
    char           *cachefile;

    mfpatch       **patches = NULL;

    retval = mfconn_api_device_get_updates(conn, quickkey,
                                           local_revision, remote_revision,
                                           &patches);

    if (retval != 0) {
        fprintf(stderr, "device/get_updates api call unsuccessful\n");
        return -1;
    }
    // if no patches are returned, then the full file has to be downloaded
    if (patches[0] == NULL) {
        free(patches);

        retval = filecache_download_file(filecache_path, quickkey,
                                         remote_revision, conn);
        if (retval != 0) {
            fprintf(stderr, "filecache_download_file failed\n");
            return -1;
        }

        return 0;
    }

    last_target_revision = local_revision;
    // else, go through all patches and download and apply them
    for (i = 0; patches[i] != NULL; i++) {
        /* verify that the source revision is equal to the last target
         * revision */
        if (patch_get_source_revision(patches[i]) != last_target_revision) {
            fprintf(stderr, "the source revision is unequal the last "
                    "target revision\n");
            break;
        }
        last_target_revision = patch_get_target_revision(patches[i]);

        retval =
            filecache_download_patch(conn, quickkey,
                                     patch_get_source_revision(patches[i]),
                                     patch_get_target_revision(patches[i]),
                                     patch_get_hash(patches[i]),
                                     filecache_path);
        if (retval != 0) {
            fprintf(stderr, "filecache_download_patch failed\n");
            break;
        }

        /* verify that the file to patch has the right hash */
        cachefile =
            strdup_printf("%s/%s_%d", filecache_path, quickkey,
                          patch_get_source_revision(patches[i]));
        hex2binary(patch_get_source_hash(patches[i]), hash2);
        retval = file_check_integrity_hash(cachefile, hash2);
        free(cachefile);
        if (retval != 0) {
            fprintf(stderr, "the source file has the wrong hash\n");
            break;
        }

        /* now apply the patch in patchfile to the file in cachefile */
        retval = filecache_patch_file(filecache_path, quickkey,
                                      patch_get_source_revision(patches[i]),
                                      patch_get_target_revision(patches[i]));
        if (retval != 0) {
            fprintf(stderr, "filecache_patch_file failed\n");
            break;
        }

        /* verify that the patched file has the right hash */
        cachefile =
            strdup_printf("%s/%s_%d", filecache_path, quickkey,
                          patch_get_target_revision(patches[i]));
        hex2binary(patch_get_target_hash(patches[i]), hash2);
        retval = file_check_integrity_hash(cachefile, hash2);
        free(cachefile);
        if (retval != 0) {
            fprintf(stderr, "the target file has the wrong hash\n");
            break;
        }

        free(patches[i]);
    }

    /* check if the terminating NULL was reached or if processing was aborted
     * before that */
    if (patches[i] != NULL) {
        for (; patches[i] != NULL; i++)
            free(patches[i]);
        free(patches);
        return -1;
    }

    free(patches);

    /* verify that the last target revision is equal to the requested remote
     * revision */
    if (last_target_revision != remote_revision) {
        fprintf(stderr, "last_target_revision is not equal to the requested "
                "remote revision\n");
        return -1;
    }

    return 0;
}

static int filecache_download_patch(mfconn * conn, const char *quickkey,
                                    uint64_t source_revision,
                                    uint64_t target_revision,
                                    const char *phash,
                                    const char *filecache_path)
{
    mfpatch        *patch;
    const char     *url;
    mfhttp         *http;
    int             retval;
    char           *patchfile;
    unsigned char   hash2[SHA256_DIGEST_LENGTH];

    /* first retrieve the patch url */
    patch = patch_alloc();
    retval = mfconn_api_device_get_patch(conn, patch, quickkey,
                                         source_revision, target_revision);

    if (retval != 0) {
        fprintf(stderr, "mfconn_api_device_get_patch failed\n");
        patch_free(patch);
        return -1;
    }

    /* verify if the retrieved patch hash is the expected patch hash */
    if (strcmp(phash, patch_get_hash(patch)) != 0) {
        fprintf(stderr, "the expected patch hash is not equal the hash "
                "returned by device/get_patch\n");
        patch_free(patch);
        return -1;
    }

    /* then download the patch */
    url = patch_get_link(patch);

    if (url == NULL || url[0] == '\0') {
        fprintf(stderr, "patch_get_link failed\n");
        patch_free(patch);
        return -1;
    }

    patchfile =
        strdup_printf("%s/%s_patch_%d_%d", filecache_path, quickkey,
                      source_revision, target_revision);

    http = http_create();
    retval = http_get_file(http, url, patchfile);
    http_destroy(http);

    if (retval != 0) {
        fprintf(stderr, "download failed\n");
        free(patchfile);
        patch_free(patch);
        return -1;
    }

    /* verify the integrity of the patch */
    hex2binary(patch_get_hash(patch), hash2);
    retval = file_check_integrity_hash(patchfile, hash2);

    if (retval != 0) {
        fprintf(stderr, "file_check_integrity_hash failed for patch\n");
        patch_free(patch);
        return -1;
    }

    patch_free(patch);

    return 0;
}

static int filecache_patch_file(const char *filecache_path,
                                const char *quickkey,
                                uint64_t source_revision,
                                uint64_t target_revision)
{
    char           *patchfile;
    char           *sourcefile;
    char           *targetfile;
    FILE           *sourcefile_fh;
    FILE           *patchfile_fh;
    FILE           *targetfile_fh;
    int             retval;

    sourcefile =
        strdup_printf("%s/%s_%d", filecache_path, quickkey, source_revision);
    sourcefile_fh = fopen(sourcefile, "r");
    if (sourcefile_fh == NULL) {
        fprintf(stderr, "cannot open %s\n", sourcefile);
        free(sourcefile);
        return -1;
    }

    free(sourcefile);

    patchfile =
        strdup_printf("%s/%s_patch_%d_%d", filecache_path, quickkey,
                      source_revision, target_revision);
    patchfile_fh = fopen(patchfile, "r");
    if (patchfile_fh == NULL) {
        fprintf(stderr, "cannot open %s\n", patchfile);
        free(patchfile);
        fclose(sourcefile_fh);
        return -1;
    }

    free(patchfile);

    targetfile =
        strdup_printf("%s/%s_%d", filecache_path, quickkey, target_revision);
    targetfile_fh = fopen(targetfile, "w");
    if (targetfile_fh == NULL) {
        fprintf(stderr, "cannot open %s\n", targetfile);
        fclose(sourcefile_fh);
        fclose(patchfile_fh);
        free(targetfile);
        return -1;
    }

    free(targetfile);

    retval = xdelta3_patch(sourcefile_fh, patchfile_fh, targetfile_fh);
    if (retval != 0) {
        fprintf(stderr, "unable to patch\n");
        fclose(sourcefile_fh);
        fclose(patchfile_fh);
        fclose(targetfile_fh);
        return -1;
    }

    fclose(sourcefile_fh);
    fclose(patchfile_fh);
    fclose(targetfile_fh);

    return 0;
}
