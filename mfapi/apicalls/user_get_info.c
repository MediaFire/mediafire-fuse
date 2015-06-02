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
#include <stdio.h>
#include <stdlib.h>

#include "../../utils/http.h"
#include "../mfconn.h"
#include "../account.h"
#include "../apicalls.h"        // IWYU pragma: keep

static int      _decode_user_get_info(mfhttp * conn, void *data);

int mfconn_api_user_get_info(mfconn * conn, account_t * account)
{
    const char     *api_call;
    int             retval;
    mfhttp         *http;

    // char        *rx_buffer;

    if (conn == NULL)
        return -1;

    api_call = mfconn_create_signed_get(conn, 0, "user/get_info.php",
                                        "?response_format=json");
    if (api_call == NULL) {
        fprintf(stderr, "mfconn_create_signed_get failed\n");
        return -1;
    }

    http = http_create();

    if (mfconn_get_http_flags(conn) & HTTP_FLAG_LAZY_SSL) {

        http_set_connect_flags(http, HTTP_FLAG_LAZY_SSL);
    }

    http_set_data_handler(http, _decode_user_get_info, (void *)account);

    retval = http_get_buf(http, api_call);

    http_destroy(http);

    mfconn_update_secret_key(conn);

    free((void *)api_call);

    return retval;
}

static int _decode_user_get_info(mfhttp * conn, void *data)
{
    account_t      *account;
    json_error_t    error;
    json_t         *root;
    json_t         *node;
    json_t         *email;
    json_t         *first_name;
    json_t         *last_name;
    json_t         *used_storage_size;
    json_t         *storage_limit;
    int             retval;

    if (data == NULL)
        return -1;
    else
        account = (account_t *) data;

    root = http_parse_buf_json(conn, 0, &error);

    if (root == NULL) {
        fprintf(stderr, "http_parse_buf_json failed at line %d\n", error.line);
        fprintf(stderr, "error message: %s\n", error.text);
        return -1;
    }

    node = json_object_get(root, "response");

    retval = mfapi_check_response(node, "user/get_info");
    if (retval != 0) {
        fprintf(stderr, "invalid response\n");
        json_decref(root);
        return retval;
    }

    node = json_object_get(node, "user_info");

    email = json_object_get(node, "email");
    if (email != NULL)
        printf("Email: %s\n\r", json_string_value(email));

    // parse and store first name
    first_name = json_object_get(node, "first_name");
    if (first_name != NULL) {
        account_set_first_name(account, (json_string_value(first_name)));
    }
    // parse and store last name
    last_name = json_object_get(node, "last_name");
    if (last_name != NULL) {
        account_set_last_name(account, (json_string_value(last_name)));
    }
    // parse and store amount of storage space used
    used_storage_size = json_object_get(node, "used_storage_size");
    if (used_storage_size != NULL) {
        account_set_space_used(account, (json_string_value(used_storage_size)));
    }

    storage_limit = json_object_get(node, "storage_limit");
    if (storage_limit != NULL) {
        account_set_space_total(account, (json_string_value(storage_limit)));
    }

    printf("\n\r");

    json_decref(root);

    return 0;
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
