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

#include "../../utils/http.h"
#include "../../utils/strings.h"
#include "../mfconn.h"
#include "../apicalls.h"        // IWYU pragma: keep

static int      _decode_upload_simple(mfhttp * conn, void *data);

int
mfconn_api_upload_simple(mfconn * conn, const char *folderkey,
                         FILE * fh, const char *file_name, bool replace,
			 char **upload_key)
{
    const char     *api_call;
    int             retval;
    mfhttp         *http;
    long            l_file_size;
    uint64_t        file_size;
    int             i;
    struct curl_slist *custom_headers = NULL;
    char           *tmpheader;

    if (conn == NULL)
        return -1;

    if (fh == NULL)
        return -1;

    retval = fseek(fh, 0, SEEK_END);
    if (retval != 0) {
        fprintf(stderr, "fseek failed\n");
        return -1;
    }

    l_file_size = ftell(fh);
    if (l_file_size == -1) {
        fprintf(stderr, "ftell failed\n");
        return -1;
    }
    file_size = l_file_size;

    // make sure that we are at the beginning of the file
    rewind(fh);

    for (i = 0; i < mfconn_get_max_num_retries(conn); i++) {
        if (*upload_key != NULL) {
            free(*upload_key);
            *upload_key = NULL;
        }
        if (custom_headers != NULL) {
            curl_slist_free_all(custom_headers);
            custom_headers = NULL;
        }

        if (folderkey == NULL) {
            api_call = mfconn_create_signed_get(conn, 0,
                                                "upload/simple.php",
                                                "?response_format=json");
        } else {
	    if (replace) {
		api_call = mfconn_create_signed_get(conn, 0,
						    "upload/simple.php",
						    "?response_format=json"
						    "&action_on_duplicate=replace"
						    "&folder_key=%s", folderkey);
	    } else {
		api_call = mfconn_create_signed_get(conn, 0,
						    "upload/simple.php",
						    "?response_format=json"
						    "&folder_key=%s", folderkey);
	    }
        }
        if (api_call == NULL) {
            fprintf(stderr, "mfconn_create_signed_get failed\n");
            return -1;
        }
        // make sure that we are at the beginning of the file
        rewind(fh);

        // the following three pseudo headers are interpreted by the mediafire
        // server
        tmpheader = strdup_printf("x-filename: %s", file_name);
        custom_headers = curl_slist_append(custom_headers, tmpheader);
        free(tmpheader);
        tmpheader = strdup_printf("x-filesize: %" PRIu64, file_size);
        custom_headers = curl_slist_append(custom_headers, tmpheader);
        free(tmpheader);

        http = http_create();

        if (mfconn_get_http_flags(conn) & HTTP_FLAG_LAZY_SSL) {

            http_set_connect_flags(http, HTTP_FLAG_LAZY_SSL);
        }

        http_set_data_handler(http, _decode_upload_simple, upload_key);

        retval = http_post_file(http, api_call, fh, &custom_headers,
                                file_size);

        http_destroy(http);
        mfconn_update_secret_key(conn);

        if (custom_headers != NULL) {
            curl_slist_free_all(custom_headers);
            custom_headers = NULL;
        }
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

static int _decode_upload_simple(mfhttp * conn, void *user_ptr)
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

    retval = mfapi_check_response(node, "upload/simple");
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
