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
#include "../folder.h"
#include "../mfconn.h"
#include "../apicalls.h"        // IWYU pragma: keep

static int      _decode_folder_get_info(mfhttp * conn, void *data);

int
mfconn_api_folder_get_info(mfconn * conn, mffolder * folder,
                           const char *folderkey)
{
    const char     *api_call;
    int             retval;
    mfhttp         *http;
    int             i;

    if (conn == NULL)
        return -1;

    if (folder == NULL)
        return -1;

    // key must either be 13 chars or NULL
    if (folderkey != NULL && strlen(folderkey) != 13) {
        return -1;
    }

    for (i = 0; i < mfconn_get_max_num_retries(conn); i++) {
        if (folderkey == NULL) {
            api_call = mfconn_create_signed_get(conn, 0, "folder/get_info.php",
                                                "?response_format=json");
        } else {
            api_call = mfconn_create_signed_get(conn, 0, "folder/get_info.php",
                                                "?folder_key=%s"
                                                "&response_format=json",
                                                folderkey);
        }
        if (api_call == NULL) {
            fprintf(stderr, "mfconn_create_signed_get failed\n");
            return -1;
        }

        http = http_create();
        retval = http_get_buf(http, api_call, _decode_folder_get_info, folder);
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

static int _decode_folder_get_info(mfhttp * conn, void *data)
{
    json_error_t    error;
    json_t         *root;
    json_t         *node;
    json_t         *folderkey;
    json_t         *folder_name;
    json_t         *revision;
    json_t         *created;
    json_t         *parent_folder;
    int             retval = 0;
    mffolder       *folder;
    char           *ret;
    struct tm       tm;

    if (data == NULL)
        return -1;

    folder = (mffolder *) data;

    root = http_parse_buf_json(conn, 0, &error);

    if (root == NULL) {
        fprintf(stderr, "http_parse_buf_json failed at line %d\n", error.line);
        fprintf(stderr, "error message: %s\n", error.text);
        return -1;
    }

    node = json_object_get(root, "response");

    retval = mfapi_check_response(node, "folder/get_info");
    if (retval != 0) {
        fprintf(stderr, "invalid response\n");
        json_decref(root);
        return retval;
    }

    node = json_object_get(node, "folder_info");

    folderkey = json_object_get(node, "folderkey");
    if (folderkey != NULL)
        folder_set_key(folder, json_string_value(folderkey));

    folder_name = json_object_get(node, "name");
    if (folder_name != NULL)
        folder_set_name(folder, json_string_value(folder_name));

    parent_folder = json_object_get(node, "parent_folderkey");
    if (parent_folder != NULL) {
        folder_set_parent(folder, json_string_value(parent_folder));
    }
    // infer that the parent folder must be root
    if (parent_folder == NULL && folderkey != NULL)
        folder_set_parent(folder, NULL);

    revision = json_object_get(node, "revision");
    if (revision != NULL) {
        folder_set_revision(folder, atoll(json_string_value(revision)));
    }

    created = json_object_get(node, "created");
    if (created != NULL) {
        memset(&tm, 0, sizeof(struct tm));
        ret = (char *) strptime(json_string_value(created), "%F %T", &tm);
        if (ret[0] != '\0') {
            fprintf(stderr, "cannot parse time\n");
        } else {
            folder_set_created(folder, mktime(&tm));
        }
    }

    if (folderkey == NULL)
        retval = -1;

    json_decref(root);

    return retval;
}

// sample user callback
/*
static void
_mycallback(char *data,size_t sz,cmffile *cfile)
{
    double  bytes_read;
    double  bytes_total;

    bytes_read = cfile_get_rx_count(cfile);
    bytes_total = cfile_get_rx_length(cfile);

    printf("bytes read: %.0f\n\r",bytes_read);

    if(bytes_read == bytes_total)
    {
        printf("transfer complete!\n\r");
    }

    return;
}
*/
