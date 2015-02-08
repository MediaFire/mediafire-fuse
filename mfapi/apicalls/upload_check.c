/*
 * Copyright (C) 2015 Johannes Schauer <j.schauer@email.de>
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

#include <jansson.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>

#include "../../utils/http.h"
#include "../mfconn.h"
#include "../apicalls.h"        // IWYU pragma: keep

static int      _decode_upload_check(mfhttp * conn, void *data);

int mfconn_api_upload_check(mfconn * conn, const char *filename,
                            const char *hash, uint64_t size,
                            const char *folder_key,
                            struct mfconn_upload_check_result *result)
{
    const char     *api_call;
    int             retval;
    mfhttp         *http;
    int             i;
    char           *filename_urlenc;

    if (conn == NULL)
        return -1;

    if (hash == NULL || hash[0] == '\0') {
        fprintf(stderr, "hash must not be empty\n");
        return -1;
    }

    if (filename == NULL || filename[0] == '\0') {
        fprintf(stderr, "filename must not be empty\n");
        return -1;
    }

    if (folder_key == NULL) {
        fprintf(stderr, "folder_key must not be empty\n");
        return -1;
    }

    for (i = 0; i < mfconn_get_max_num_retries(conn); i++) {
        filename_urlenc = urlencode(filename);
        if (filename_urlenc == NULL) {
            fprintf(stderr, "urlencode failed\n");
            return -1;
        }
        api_call = mfconn_create_signed_get(conn, 0, "upload/check.php",
                                            "?response_format=json"
                                            "&filename=%s"
                                            "&size=%" PRIu64
                                            "&hash=%s"
                                            "&folder_key=%s", filename_urlenc,
                                            size, hash, folder_key);
        free(filename_urlenc);
        if (api_call == NULL) {
            fprintf(stderr, "mfconn_create_signed_get failed\n");
            return -1;
        }

        http = http_create();

        if (mfconn_get_flags(conn) & HTTP_CONN_LAZY_SSL) {

            http_set_connect_flags(http, HTTP_CONN_LAZY_SSL);
        }

        http_set_data_handler(http, _decode_upload_check, (void *)result);

        retval = http_get_buf(http, api_call);

        http_destroy(http);

        mfconn_update_secret_key(conn);

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

static int _decode_upload_check(mfhttp * conn, void *data)
{
    json_error_t    error;
    json_t         *obj;
    struct mfconn_upload_check_result *result;
    json_t         *root;
    json_t         *node;
    int             retval;

    if (data == NULL)
        return -1;

    result = (struct mfconn_upload_check_result *)data;

    root = http_parse_buf_json(conn, 0, &error);

    if (root == NULL) {
        fprintf(stderr, "http_parse_buf_json failed at line %d\n", error.line);
        fprintf(stderr, "error message: %s\n", error.text);
        return -1;
    }

    node = json_object_get(root, "response");

    retval = mfapi_check_response(node, "upload/check");
    if (retval != 0) {
        fprintf(stderr, "invalid response\n");
        json_decref(root);
        return retval;
    }

    /* retrieve response/hash_exists */
    obj = json_object_get(node, "hash_exists");
    if (obj == NULL) {
        fprintf(stderr, "cannot get node response/hash_exists\n");
        json_decref(root);
        return -1;
    }
    if (!json_is_string(obj)) {
        fprintf(stderr, "response/hash_exists is not expected type string\n");
        json_decref(root);
        return -1;
    }
    if (strcmp(json_string_value(obj), "yes") == 0)
        result->hash_exists = true;
    else if (strcmp(json_string_value(obj), "no") == 0)
        result->hash_exists = false;
    else {
        fprintf(stderr, "response/hash_exists is neither yes nor no\n");
        json_decref(root);
        return -1;
    }

    /* retrieve response/in_account */
    if (result->hash_exists) {
        obj = json_object_get(node, "in_account");
        if (obj == NULL) {
            fprintf(stderr, "cannot get node response/in_account\n");
            json_decref(root);
            return -1;
        }
        if (!json_is_string(obj)) {
            fprintf(stderr,
                    "response/in_account is not expected type string\n");
            json_decref(root);
            return -1;
        }
        if (strcmp(json_string_value(obj), "yes") == 0)
            result->in_account = true;
        else if (strcmp(json_string_value(obj), "no") == 0)
            result->in_account = false;
        else {
            fprintf(stderr, "response/in_account is neither yes nor no\n");
            json_decref(root);
            return -1;
        }
    }

    /* retrieve response/file_exists */
    obj = json_object_get(node, "file_exists");
    if (obj == NULL) {
        fprintf(stderr, "cannot get node response/file_exists\n");
        json_decref(root);
        return -1;
    }
    if (!json_is_string(obj)) {
        fprintf(stderr, "response/file_exists is not expected type string\n");
        json_decref(root);
        return -1;
    }
    if (strcmp(json_string_value(obj), "yes") == 0)
        result->file_exists = true;
    else if (strcmp(json_string_value(obj), "no") == 0)
        result->file_exists = false;
    else {
        fprintf(stderr, "response/file_exists is neither yes nor no\n");
        json_decref(root);
        return -1;
    }

    /* retrieve response/different_hash */
    if (result->file_exists) {
        obj = json_object_get(node, "different_hash");
        if (obj == NULL) {
            fprintf(stderr, "cannot get node response/different_hash\n");
            json_decref(root);
            return -1;
        }
        if (!json_is_string(obj)) {
            fprintf(stderr,
                    "response/different_hash is not expected type string\n");
            json_decref(root);
            return -1;
        }
        if (strcmp(json_string_value(obj), "yes") == 0)
            result->different_hash = true;
        else if (strcmp(json_string_value(obj), "no") == 0)
            result->different_hash = false;
        else {
            fprintf(stderr, "response/different_hash is neither yes nor no\n");
            json_decref(root);
            return -1;
        }
    }

    json_decref(root);

    return 0;
}
