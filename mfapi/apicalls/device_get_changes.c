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

#include <jansson.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

#include "../../utils/http.h"
#include "../mfconn.h"
#include "../apicalls.h"        // IWYU pragma: keep

static int      _decode_device_get_changes(mfhttp * conn, void *data);

/*
 * the mfconn_device_change array will be realloc'ed so it must either point
 * to a malloc'ed memory or be NULL
 *
 * the returned array will be sorted by revision
 */
int mfconn_api_device_get_changes(mfconn * conn, uint64_t revision,
                                  struct mfconn_device_change **changes)
{
    const char     *api_call;
    int             retval;
    mfhttp         *http;
    int             i;

    if (conn == NULL)
        return -1;

    for (i = 0; i < mfconn_get_max_num_retries(conn); i++) {
        if (*changes != NULL) {
            free(*changes);
            *changes = NULL;
        }

        api_call = mfconn_create_signed_get(conn, 0, "device/get_changes.php",
                                            "?revision=%" PRIu64
                                            "&response_format=json", revision);
        if (api_call == NULL) {
            fprintf(stderr, "mfconn_create_signed_get failed\n");
            return -1;
        }

        http = http_create();

        if (mfconn_get_flags(conn) & HTTP_CONN_LAZY_SSL) {

            http_set_connect_flags(http, HTTP_CONN_LAZY_SSL);
        }

        http_set_data_handler(http, _decode_device_get_changes,
                              (void *)changes);

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

static void aux(json_t * key, json_t * parent, json_t * revision,
                enum mfconn_device_change_type change,
                struct mfconn_device_change **changes, size_t * len_changes)
{
    struct mfconn_device_change *tmp_change;

    if (key == NULL || revision == NULL || parent == NULL) {
        fprintf(stderr, "change without either key, revision or parent");
        return;
    }
    (*len_changes)++;
    *changes = (struct mfconn_device_change *)
        realloc(*changes,
                (*len_changes) * sizeof(struct mfconn_device_change));
    tmp_change = *changes + (*len_changes) - 1;
    tmp_change->change = change;
    strncpy(tmp_change->key, json_string_value(key), sizeof(tmp_change->key));
    strncpy(tmp_change->parent, json_string_value(parent),
            sizeof(tmp_change->parent));
    tmp_change->revision = atoll(json_string_value(revision));
}

static int change_compare(const void *a, const void *b)
{
    return ((struct mfconn_device_change *)a)->revision
        - ((struct mfconn_device_change *)b)->revision;
}

static int _decode_device_get_changes(mfhttp * conn, void *user_ptr)
{
    json_error_t    error;
    json_t         *root;
    json_t         *node;
    json_t         *response;
    json_t         *data;

    json_t         *obj_array;
    json_t         *key;
    json_t         *parent;
    json_t         *revision;
    json_t         *device_revision;
    int             retval;

    int             array_sz;
    int             i = 0;

    struct mfconn_device_change **changes;
    size_t          len_changes;

    changes = (struct mfconn_device_change **)user_ptr;
    if (changes == NULL)
        return -1;

    root = http_parse_buf_json(conn, 0, &error);

    if (root == NULL) {
        fprintf(stderr, "http_parse_buf_json failed at line %d\n", error.line);
        fprintf(stderr, "error message: %s\n", error.text);
        return -1;
    }

    response = json_object_get(root, "response");

    retval = mfapi_check_response(response, "device/get_changes");
    if (retval != 0) {
        fprintf(stderr, "invalid response\n");
        json_decref(root);
        return retval;
    }

    device_revision = json_object_get(response, "device_revision");
    if (device_revision == NULL) {
        fprintf(stderr,
                "response/device_revision is not part of the result\n");
        json_decref(root);
        return -1;
    }

    len_changes = 0;

    node = json_object_get(response, "updated");

    obj_array = json_object_get(node, "files");
    if (json_is_array(obj_array)) {
        array_sz = json_array_size(obj_array);
        for (i = 0; i < array_sz; i++) {
            data = json_array_get(obj_array, i);
            if (!json_is_object(data))
                continue;
            key = json_object_get(data, "quickkey");
            parent = json_object_get(data, "parent_folderkey");
            revision = json_object_get(data, "revision");
            aux(key, parent, revision, MFCONN_DEVICE_CHANGE_UPDATED_FILE,
                changes, &len_changes);
        }
    }

    obj_array = json_object_get(node, "folders");
    if (json_is_array(obj_array)) {
        array_sz = json_array_size(obj_array);
        for (i = 0; i < array_sz; i++) {
            data = json_array_get(obj_array, i);
            if (!json_is_object(data))
                continue;
            key = json_object_get(data, "folderkey");
            parent = json_object_get(data, "parent_folderkey");
            revision = json_object_get(data, "revision");
            aux(key, parent, revision, MFCONN_DEVICE_CHANGE_UPDATED_FOLDER,
                changes, &len_changes);
        }
    }

    node = json_object_get(response, "deleted");

    obj_array = json_object_get(node, "files");
    if (json_is_array(obj_array)) {
        array_sz = json_array_size(obj_array);
        for (i = 0; i < array_sz; i++) {
            data = json_array_get(obj_array, i);
            if (!json_is_object(data))
                continue;
            key = json_object_get(data, "quickkey");
            parent = json_object_get(data, "parent_folderkey");
            revision = json_object_get(data, "revision");
            aux(key, parent, revision, MFCONN_DEVICE_CHANGE_DELETED_FILE,
                changes, &len_changes);
        }
    }

    obj_array = json_object_get(node, "folders");
    if (json_is_array(obj_array)) {
        array_sz = json_array_size(obj_array);
        for (i = 0; i < array_sz; i++) {
            data = json_array_get(obj_array, i);
            if (!json_is_object(data))
                continue;
            key = json_object_get(data, "folderkey");
            parent = json_object_get(data, "parent_folderkey");
            revision = json_object_get(data, "revision");
            aux(key, parent, revision, MFCONN_DEVICE_CHANGE_DELETED_FOLDER,
                changes, &len_changes);
        }
    }
    // sort
    qsort(*changes, len_changes, sizeof(struct mfconn_device_change),
          change_compare);

    // put an entry with change type MFCONN_DEVICE_CHANGE_END at the end
    // encode the current device revision as its revision
    len_changes++;
    *changes = (struct mfconn_device_change *)
        realloc(*changes, len_changes * sizeof(struct mfconn_device_change));
    memset(*changes + len_changes - 1, 0, sizeof(struct mfconn_device_change));
    (*changes)[len_changes - 1].change = MFCONN_DEVICE_CHANGE_END;
    (*changes)[len_changes - 1].revision =
        atoll(json_string_value(device_revision));

    json_decref(root);

    return 0;
}
