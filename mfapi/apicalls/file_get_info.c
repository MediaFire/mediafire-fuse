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

#ifndef __OpenBSD__
#define _XOPEN_SOURCE           // for strptime
#endif

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../utils/http.h"
#include "../mfconn.h"
#include "../file.h"
#include "../apicalls.h"        // IWYU pragma: keep

static int      _decode_file_get_info(mfhttp * conn, void *data);

int mfconn_api_file_get_info(mfconn * conn, mffile * file,
                             const char *quickkey)
{
    const char     *api_call;
    int             retval;
    int             len;
    mfhttp         *http;
    int             i;

    if (conn == NULL)
        return -1;

    if (file == NULL)
        return -1;
    if (quickkey == NULL)
        return -1;

    len = strlen(quickkey);

    // key must either be 11 or 15 chars
    if (len != 11 && len != 15)
        return -1;

    for (i = 0; i < mfconn_get_max_num_retries(conn); i++) {
        api_call = mfconn_create_signed_get(conn, 0, "file/get_info.php",
                                            "?quick_key=%s"
                                            "&response_format=json", quickkey);
        if (api_call == NULL) {
            fprintf(stderr, "mfconn_create_signed_get failed\n");
            return -1;
        }

        http = http_create();
        retval = http_get_buf(http, api_call, _decode_file_get_info, file);
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

static int _decode_file_get_info(mfhttp * conn, void *data)
{
    json_error_t    error;
    json_t         *root;
    json_t         *node;
    json_t         *obj;
    json_t         *quickkey;
    int             retval = 0;
    mffile         *file;
    char           *ret;
    struct tm       tm;

    if (data == NULL)
        return -1;

    file = (mffile *) data;

    root = http_parse_buf_json(conn, 0, &error);

    if (root == NULL) {
        fprintf(stderr, "http_parse_buf_json failed at line %d\n", error.line);
        fprintf(stderr, "error message: %s\n", error.text);
        return -1;
    }

    node = json_object_get(root, "response");

    retval = mfapi_check_response(node, "file/get_info");
    if (retval != 0) {
        fprintf(stderr, "invalid response\n");
        json_decref(root);
        return retval;
    }

    node = json_object_get(node, "file_info");

    quickkey = json_object_get(node, "quickkey");
    if (quickkey != NULL)
        file_set_key(file, json_string_value(quickkey));

    obj = json_object_get(node, "filename");
    if (obj != NULL)
        file_set_name(file, json_string_value(obj));

    obj = json_object_get(node, "hash");
    if (obj != NULL) {
        file_set_hash(file, json_string_value(obj));
    }

    obj = json_object_get(node, "parent_folderkey");
    if (obj != NULL) {
        file_set_parent(file, json_string_value(obj));
    }
    // infer that the parent folder must be root
    if (obj == NULL && quickkey != NULL)
        file_set_parent(file, NULL);

    obj = json_object_get(node, "created");
    if (obj != NULL) {
        memset(&tm, 0, sizeof(struct tm));
        ret = (char *) strptime(json_string_value(obj), "%F %T", &tm);
        if (ret[0] != '\0') {
            fprintf(stderr, "cannot parse time\n");
        } else {
            file_set_created(file, mktime(&tm));
        }
    }

    obj = json_object_get(node, "revision");
    if (obj != NULL) {
        file_set_revision(file, atoll(json_string_value(obj)));
    }

    obj = json_object_get(node, "size");
    if (obj != NULL) {
        file_set_size(file, atoll(json_string_value(obj)));
    }

    if (quickkey == NULL)
        retval = -1;

    json_decref(root);

    return retval;
}
