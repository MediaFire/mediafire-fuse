/*
 * Copyright (C) 2013 Bryan Christ <bryan.christ@mediafire.com>
 *               2014 Johannes Schauer <j.schauer@email.de>
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

#ifndef _MFAPI_APICALLS_H_
#define _MFAPI_APICALLS_H_

#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "user.h"
#include "file.h"
#include "folder.h"
#include "patch.h"
#include "mfconn.h"
#include "../utils/http.h"

#define MFAPI_MAX_LEN_KEY 15
#define MFAPI_MAX_LEN_NAME 255

#define MFAPI_VERSION "1.4"

enum mfconn_device_change_type {
    MFCONN_DEVICE_CHANGE_DELETED_FOLDER,
    MFCONN_DEVICE_CHANGE_DELETED_FILE,
    MFCONN_DEVICE_CHANGE_UPDATED_FOLDER,
    MFCONN_DEVICE_CHANGE_UPDATED_FILE,
    MFCONN_DEVICE_CHANGE_END
};

enum mfconn_file_link_type {
    MFCONN_FILE_LINK_TYPE_NORMAL_DOWNLOAD,
    MFCONN_FILE_LINK_TYPE_DIRECT_DOWNLOAD,
    MFCONN_FILE_LINK_TYPE_VIEW,
    MFCONN_FILE_LINK_TYPE_EDIT,
    MFCONN_FILE_LINK_TYPE_WATCH,
    MFCONN_FILE_LINK_TYPE_LISTEN,
    MFCONN_FILE_LINK_TYPE_STREAMING,
    MFCONN_FILE_LINK_TYPE_ONE_TIME_DOWNLOAD
};

// bit flags for controlling runtime tweaks
//#define MFOPTS_LAZY_SSL         (1U << 0)


extern const char *mfconn_file_link_types[];    // declared in apicalls.c

struct mfconn_device_change {
    enum mfconn_device_change_type change;
    char            key[16];
    uint64_t        revision;
    char            parent[16];
};

struct mfconn_upload_check_result {
    bool            hash_exists;
    bool            in_account;
    bool            file_exists;
    bool            different_hash;
    /* TODO: add resumable_upload */
};

int             mfapi_check_response(json_t * response, const char *apicall);

int             mfapi_decode_common(mfhttp * conn, void *user_ptr);

int             mfconn_api_file_get_info(mfconn * conn, mffile * file,
                                         const char *quickkey);

int             mfconn_api_file_get_links(mfconn * conn, mffile * file,
                                          const char *quickkey,
                                          enum mfconn_file_link_type
                                          link_mask);

int             mfconn_api_file_move(mfconn * conn, const char *quickkey,
                                     const char *folderkey);

int             mfconn_api_file_update(mfconn * conn, const char *quickkey,
                                       const char *filename,
                                       const char *mtime, bool truncate);

int             mfconn_api_folder_create(mfconn * conn, const char *parent,
                                         const char *name);

long            mfconn_api_folder_get_content(mfconn * conn, const int mode,
                                              const char *folderkey,
                                              mffolder *** folder_result,
                                              mffile *** file_result);

int             mfconn_api_folder_get_info(mfconn * conn, mffolder * folder,
                                           const char *folderkey);

int             mfconn_api_folder_move(mfconn * conn,
                                       const char *folder_key_src,
                                       const char *folder_key_dst);

int             mfconn_api_folder_update(mfconn * conn, const char *folder_key,
                                         const char *foldername,
                                         const char *mtime);

int             mfconn_api_user_get_info(mfconn * conn, mfuser_t * user);

int             mfconn_api_user_get_session_token(mfconn * conn,
                                                  const char *server,
                                                  const char *username,
                                                  const char *password,
                                                  int app_id,
                                                  const char *app_key,
                                                  uint32_t * secret_key,
                                                  char **secret_time,
                                                  char **session_token,
                                                  char **ekey);

int             mfconn_api_folder_delete(mfconn * conn, const char *folderkey);

int             mfconn_api_file_delete(mfconn * conn, const char *quickkey);

int             mfconn_api_device_get_status(mfconn * conn,
                                             uint64_t * revision);

int             mfconn_api_device_get_changes(mfconn * conn, uint64_t revision, struct mfconn_device_change
                                              **changes);

int             mfconn_api_device_get_updates(mfconn * conn,
                                              const char *quickkey,
                                              uint64_t revision,
                                              uint64_t target_revision,
                                              mfpatch *** patches);

int             mfconn_api_device_get_patch(mfconn * conn, mfpatch * patch,
                                            const char *quickkey,
                                            uint64_t source_revision,
                                            uint64_t target_revision);

int             mfconn_api_upload_check(mfconn * conn, const char *filename,
                                        const char *hash,
                                        uint64_t size, const char *folder_key,
                                        struct mfconn_upload_check_result
                                        *result);

int             mfconn_api_upload_instant(mfconn * conn, const char *quick_key,
                                          const char *filename,
                                          const char *hash, uint64_t size,
                                          const char *folder_key);

int             mfconn_api_upload_simple(mfconn * conn, const char *folderkey,
                                         FILE * fh, const char *file_name,
                                         char **upload_key);

int             mfconn_api_upload_patch(mfconn * conn, const char *quickkey,
                                        const char *source_hash,
                                        const char *target_hash,
                                        uint64_t target_size,
                                        const char *patch_path,
                                        char **upload_key);

int             mfconn_api_upload_poll_upload(mfconn * conn,
                                              const char *upload_key,
                                              int *status, int *fileerror);

#endif
