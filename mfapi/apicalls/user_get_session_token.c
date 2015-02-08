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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../utils/http.h"
#include "../../utils/strings.h"
#include "../mfconn.h"
#include "../apicalls.h"        // IWYU pragma: keep

static int      _decode_get_session_token(mfhttp * conn, void *data);

struct user_get_session_token_response {
    uint32_t        secret_key;
    char           *secret_time;
    char           *session_token;
    char           *ekey;
};

int
mfconn_api_user_get_session_token(mfconn * conn, const char *server,
                                  const char *username, const char *password,
                                  int app_id, const char *app_key,
                                  uint32_t * secret_key,
                                  char **secret_time, char **session_token,
                                  char **ekey)
{
    char           *login_url;
    char           *post_args;
    const char     *user_signature;
    int             retval;
    struct user_get_session_token_response response;
    mfhttp         *http;
    int             i;
    char           *username_urlenc;
    char           *password_urlenc;

    if (conn == NULL)
        return -1;

    for (i = 0; i < mfconn_get_max_num_retries(conn); i++) {
        if (*secret_time != NULL) {
            free(*secret_time);
            *secret_time = NULL;
        }
        if (*session_token != NULL) {
            free(*session_token);
            *session_token = NULL;
        }
        if (*ekey != NULL) {
            free(*ekey);
            *ekey = NULL;
        }
        // configure url for operation
        login_url = strdup_printf("https://%s/api/user/get_session_token.php",
                                  server);

        // create user signature
        user_signature =
            mfconn_create_user_signature(conn, username, password, app_id,
                                         app_key);

        username_urlenc = urlencode(username);
        if (username_urlenc == NULL) {
            fprintf(stderr, "urlencode failed\n");
            return -1;
        }
        password_urlenc = urlencode(password);
        if (password_urlenc == NULL) {
            fprintf(stderr, "urlencode failed\n");
            return -1;
        }
        post_args = strdup_printf("email=%s"
                                  "&password=%s"
                                  "&application_id=%d"
                                  "&signature=%s"
                                  "&token_version=2"
                                  "&response_format=json",
                                  username_urlenc, password_urlenc, app_id,
                                  user_signature);
        free(username_urlenc);
        free(password_urlenc);
        free((void *)user_signature);

        http = http_create();

        if (mfconn_get_flags(conn) & HTTP_CONN_LAZY_SSL) {

            http_set_connect_flags(http, HTTP_CONN_LAZY_SSL);
        }

        http_set_data_handler(http, _decode_get_session_token,
                              (void *)(&response));

        retval = http_post_buf(http, login_url, post_args);

        http_destroy(http);

        free(login_url);
        free(post_args);

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

    *secret_key = response.secret_key;
    *secret_time = response.secret_time;
    *session_token = response.session_token;
    *ekey = response.ekey;

    return retval;
}

static int _decode_get_session_token(mfhttp * conn, void *user_ptr)
{
    json_error_t    error;
    json_t         *root = NULL;
    json_t         *node;
    json_t         *j_obj;
    struct user_get_session_token_response *response;
    int             retval;

    if (user_ptr == NULL)
        return -1;

    response = (struct user_get_session_token_response *)user_ptr;

    root = http_parse_buf_json(conn, 0, &error);

    if (root == NULL) {
        fprintf(stderr, "http_parse_buf_json failed at line %d\n", error.line);
        fprintf(stderr, "error message: %s\n", error.text);
        return -1;
    }

    node = json_object_get(root, "response");

    retval = mfapi_check_response(node, "user/get_session_token");
    if (retval != 0) {
        fprintf(stderr, "invalid response\n");
        json_decref(root);
        return retval;
    }

    j_obj = json_object_get(node, "session_token");
    if (j_obj == NULL) {
        json_decref(root);
        fprintf(stderr, "json: no /session_token content\n");
        return -1;
    }
    response->session_token = strdup(json_string_value(j_obj));

    j_obj = json_object_get(node, "secret_key");
    if (j_obj == NULL) {
        json_decref(root);
        fprintf(stderr, "json: no /secret_key content\n");
        return -1;
    }
    response->secret_key = atoll(json_string_value(j_obj));

    /*
       time looks like a float but we must store it as a string to
       remain congruent with the server on decimal place presentation.
     */
    j_obj = json_object_get(node, "time");
    if (j_obj == NULL) {
        json_decref(root);
        fprintf(stderr, "json: no /time content\n");
        return -1;
    }
    response->secret_time = strdup(json_string_value(j_obj));

    j_obj = json_object_get(node, "ekey");
    if (j_obj == NULL) {
        json_decref(root);
        fprintf(stderr, "json: no /ekey content\n");
        return -1;
    }
    response->ekey = strdup(json_string_value(j_obj));

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
