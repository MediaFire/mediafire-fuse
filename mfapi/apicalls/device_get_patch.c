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

#include <jansson.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>

#include "../../utils/http.h"
#include "../mfconn.h"
#include "../patch.h"
#include "../apicalls.h"        // IWYU pragma: keep

static int      _decode_device_get_patch(mfhttp * conn, void *data);

int mfconn_api_device_get_patch(mfconn * conn, mfpatch * patch,
                                const char *quickkey, uint64_t source_revision,
                                uint64_t target_revision)
{
    const char     *api_call;
    int             len;
    mfhttp         *http;
    int             retval;
    int             i;

    if (conn == NULL)
        return -1;

    if (patch == NULL)
        return -1;

    if (quickkey == NULL)
        return -1;

    len = strlen(quickkey);

    if (len != 15)
        return -1;

    for (i = 0; i < mfconn_get_max_num_retries(conn); i++) {
        api_call = mfconn_create_signed_get(conn, 0, "device/get_patch.php",
                                            "?quick_key=%s"
                                            "&source_revision=%" PRIu64
                                            "&target_revision=%" PRIu64
                                            "&response_format=json", quickkey,
                                            source_revision, target_revision);
        if (api_call == NULL) {
            fprintf(stderr, "mfconn_create_signed_get failed\n");
            return -1;
        }

        http = http_create();

        if(mfconn_get_flags(conn) & HTTP_CONN_LAZY_SSL) {

            http_set_connect_flags(http, HTTP_CONN_LAZY_SSL);
        }

        http_set_data_handler(http, _decode_device_get_patch, (void *)patch);

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

static int _decode_device_get_patch(mfhttp * conn, void *data)
{
    json_error_t    error;
    json_t         *obj;
    mfpatch        *patch;
    json_t         *root;
    json_t         *node;
    int             retval;

    if (data == NULL)
        return -1;

    patch = (mfpatch *) data;

    root = http_parse_buf_json(conn, 0, &error);

    if (root == NULL) {
        fprintf(stderr, "http_parse_buf_json failed at line %d\n", error.line);
        fprintf(stderr, "error message: %s\n", error.text);
        return -1;
    }

    node = json_object_get(root, "response");

    retval = mfapi_check_response(node, "device/get_patch");
    if (retval != 0) {
        fprintf(stderr, "invalid response\n");
        json_decref(root);
        return retval;
    }

    obj = json_object_get(node, "patch_hash");
    if (obj != NULL)
        patch_set_hash(patch, json_string_value(obj));

    obj = json_object_get(node, "patch_link");
    if (obj != NULL)
        patch_set_link(patch, json_string_value(obj));

    json_decref(root);

    return 0;
}
