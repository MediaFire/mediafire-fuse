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

#define _POSIX_C_SOURCE 200809L // for strdup
#define _DEFAULT_SOURCE         // for strdup on old systems

#include <jansson.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <curl/curl.h>
#include <sys/stat.h>

#include "../../utils/http.h"
#include "../../utils/strings.h"
#include "../mfconn.h"
#include "../apicalls.h"        // IWYU pragma: keep

static int      _decode_upload_patch(mfhttp * conn, void *data);

int
mfconn_api_upload_patch(mfconn * conn, const char *quickkey,
                        const char *source_hash, const char *target_hash,
                        uint64_t target_size,
                        const char *patch_path, char **upload_key)
{
    const char     *api_call;
    int             retval;
    mfhttp         *http;
    int             i;
    FILE           *patch_fh;
    struct curl_slist *custom_headers = NULL;
    char           *tmpheader;
    uint64_t        patch_size;
    struct stat     file_info;

    if (conn == NULL)
        return -1;

    memset(&file_info, 0, sizeof(file_info));
    retval = stat(patch_path, &file_info);
    if (retval != 0) {
        fprintf(stderr, "stat failed\n");
        return -1;
    }

    patch_size = file_info.st_size;

    for (i = 0; i < mfconn_get_max_num_retries(conn); i++) {
        if (*upload_key != NULL) {
            free(*upload_key);
            *upload_key = NULL;
        }
        if (custom_headers != NULL) {
            curl_slist_free_all(custom_headers);
            custom_headers = NULL;
        }

        patch_fh = fopen(patch_path, "r");
        if (patch_fh == NULL) {
            fprintf(stderr, "cannot open %s\n", patch_path);
            return -1;
        }

        api_call = mfconn_create_signed_get(conn, 0,
                                            "upload/patch.php",
                                            "?response_format=json"
                                            "&source_hash=%s"
                                            "&target_hash=%s"
                                            "&target_size=%" PRIu64
                                            "&quickkey=%s", source_hash,
                                            target_hash, target_size,
                                            quickkey);
        if (api_call == NULL) {
            fprintf(stderr, "mfconn_create_signed_get failed\n");
            return -1;
        }

        custom_headers = curl_slist_append(custom_headers,
                                           "x-filename: dummy.patch");
        tmpheader = strdup_printf("x-filesize: %" PRIu64, patch_size);
        custom_headers = curl_slist_append(custom_headers, tmpheader);
        free(tmpheader);

        http = http_create();

        if (mfconn_get_http_flags(conn) & HTTP_FLAG_LAZY_SSL) {

            http_set_connect_flags(http, HTTP_FLAG_LAZY_SSL);
        }

        http_set_data_handler(http, _decode_upload_patch, upload_key);

        retval = http_post_file(http, api_call, patch_fh, &custom_headers,
                                patch_size);

        http_destroy(http);
        mfconn_update_secret_key(conn);

        fclose(patch_fh);
        free((void *)api_call);

        if (retval != 127 && retval != 28)
            break;

        // if there was either a curl timeout or a token error, get a new
        // token and try again
        //
        // on a curl timeout we get a new token because it is likely that we
        // lost signature synchronization (we don't know whether the server
        // accepted or rejected the last call)
        fprintf(stderr, "got error %d - negotiate a new token\n", retval);
        retval = mfconn_refresh_token(conn);
        if (retval != 0) {
            fprintf(stderr, "failed to get a new token\n");
            break;
        }
    }

    return retval;
}

static int _decode_upload_patch(mfhttp * conn, void *user_ptr)
{
    json_error_t    error;
    json_t         *root;
    json_t         *node;
    json_t         *j_obj;
    int             retval;

    char          **upload_key;

    upload_key = (char **)user_ptr;
    if (upload_key == NULL)
        return -1;

    root = http_parse_buf_json(conn, 0, &error);

    if (root == NULL) {
        fprintf(stderr, "http_parse_buf_json failed at line %d\n", error.line);
        fprintf(stderr, "error message: %s\n", error.text);
        return -1;
    }

    node = json_object_get(root, "response");

    retval = mfapi_check_response(node, "upload/patch");
    if (retval != 0) {
        fprintf(stderr, "invalid response\n");
        json_decref(root);
        return retval;
    }

    node = json_object_get(node, "doupload");

    j_obj = json_object_get(node, "key");
    if (j_obj != NULL) {
        if (strcmp(json_string_value(j_obj), "") == 0) {
            *upload_key = NULL;
        } else {
            *upload_key = strdup(json_string_value(j_obj));
        }
    } else {
        *upload_key = NULL;
    }

    json_decref(root);

    return 0;
}
