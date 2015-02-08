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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../utils/http.h"
#include "../mfconn.h"
#include "../apicalls.h"        // IWYU pragma: keep

int mfconn_api_file_update(mfconn * conn, const char *quickkey,
                           const char *filename, const char *mtime)
{
    const char     *api_call;
    int             retval;
    mfhttp         *http;
    int             i;
    char           *filename_urlenc = NULL;

    if (conn == NULL)
        return -1;

    if (quickkey == NULL)
        return -1;

    if (strlen(quickkey) != 15)
        return -1;

    if (filename == NULL && mtime == NULL)
        return -1;

    if (filename != NULL) {
        if (strlen(filename) < 1 || strlen(filename) > 255)
            return -1;
    }

    for (i = 0; i < mfconn_get_max_num_retries(conn); i++) {

        if (filename != NULL) {
            filename_urlenc = urlencode(filename);
            if (filename_urlenc == NULL) {
                fprintf(stderr, "urlencode failed\n");
                return -1;
            }
        }
        // this is the ugly way of doing it
        if (filename != NULL && mtime != NULL) {
            api_call = mfconn_create_signed_get(conn, 0, "file/update.php",
                                                "?quick_key=%s"
                                                "&filename=%s"
                                                "&mtime=%s"
                                                "&response_format=json",
                                                quickkey,
                                                filename_urlenc, mtime);
        }

        if (filename == NULL && mtime != NULL) {
            api_call = mfconn_create_signed_get(conn, 0, "file/update.php",
                                                "?quick_key=%s"
                                                "&mtime=%s"
                                                "&response_format=json",
                                                quickkey, mtime);
        }

        if (filename != NULL && mtime == NULL) {
            api_call = mfconn_create_signed_get(conn, 0, "file/update.php",
                                                "?quick_key=%s"
                                                "&filename=%s"
                                                "&response_format=json",
                                                quickkey, filename_urlenc);
        }

        if (filename_urlenc != NULL)
            free(filename_urlenc);

        if (api_call == NULL) {
            fprintf(stderr, "mfconn_create_signed_get failed\n");
            return -1;
        }

        http = http_create();

        if (mfconn_get_flags(conn) & HTTP_CONN_LAZY_SSL) {

            http_set_connect_flags(http, HTTP_CONN_LAZY_SSL);
        }

        http_set_data_handler(http, mfapi_decode_common, "file/update");

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
