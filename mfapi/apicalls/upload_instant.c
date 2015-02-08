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

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>

#include "../../utils/http.h"
#include "../mfconn.h"
#include "../apicalls.h"        // IWYU pragma: keep

int mfconn_api_upload_instant(mfconn * conn, const char *quick_key,
                              const char *filename, const char *hash,
                              uint64_t size, const char *folder_key)
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

    for (i = 0; i < mfconn_get_max_num_retries(conn); i++) {
        if (quick_key != NULL && quick_key[0] != '\0') {
            // update an existing file
            api_call = mfconn_create_signed_get(conn, 0, "upload/instant.php",
                                                "?quick_key=%s"
                                                "&size=%" PRIu64
                                                "&hash=%s"
                                                "&response_format=json",
                                                quick_key, size, hash);
        } else if (filename != NULL && filename[0] != '\0' && folder_key != 0) {
            // upload a new file
            filename_urlenc = urlencode(filename);
            if (filename_urlenc == NULL) {
                fprintf(stderr, "urlencode failed\n");
                return -1;
            }
            api_call = mfconn_create_signed_get(conn, 0, "upload/instant.php",
                                                "?folder_key=%s"
                                                "&filename=%s"
                                                "&size=%" PRIu64
                                                "&hash=%s"
                                                "&response_format=json",
                                                folder_key, filename_urlenc,
                                                size, hash);
            free(filename_urlenc);
        } else {
            fprintf(stderr, "you must either pass a quick_key or a filename "
                    "and folder_key\n");
            return -1;
        }

        if (api_call == NULL) {
            fprintf(stderr, "mfconn_create_signed_get failed\n");
            return -1;
        }

        http = http_create();

        if(mfconn_get_flags(conn) & HTTP_CONN_LAZY_SSL) {

            http_set_connect_flags(http, HTTP_CONN_LAZY_SSL);
        }

        http_set_data_handler(http, mfapi_decode_common, "upload/instant");

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
