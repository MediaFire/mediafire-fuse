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

#include <openssl/md5.h>
#include <openssl/sha.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../utils/strings.h"
#include "apicalls.h"
#include "mfconn.h"

struct mfconn {
    char           *server;
    uint32_t        secret_key;
    char           *secret_time;
    char           *session_token;
    char           *ekey;
    char           *username;
    char           *password;
    int             app_id;
    char           *app_key;
    int             max_num_retries;
    unsigned int    flags;
};

mfconn         *mfconn_create(const char *server, const char *username,
                              const char *password, int app_id,
                              const char *app_key, int max_num_retries)
{
    mfconn         *conn;
    int             retval;

    if (server == NULL)
        return NULL;

    if (username == NULL)
        return NULL;

    if (password == NULL)
        return NULL;

    if (app_id < 0)
        return NULL;

    conn = (mfconn *) calloc(1, sizeof(mfconn));

    conn->server = strdup(server);
    conn->username = strdup(username);
    conn->password = strdup(password);
    conn->app_id = app_id;
    if (app_key != NULL)
        conn->app_key = strdup(app_key);
    else
        conn->app_key = NULL;
    conn->max_num_retries = max_num_retries;
    conn->secret_time = NULL;
    conn->session_token = NULL;
    conn->ekey = NULL;
    retval = mfconn_api_user_get_session_token(conn, conn->server,
                                               conn->username, conn->password,
                                               conn->app_id, conn->app_key,
                                               &(conn->secret_key),
                                               &(conn->secret_time),
                                               &(conn->session_token),
                                               &(conn->ekey));

    if (retval != 0) {
        fprintf(stderr, "error: mfconn_api_user_get_session_token\n");
        return NULL;
    }

    return conn;
}

int mfconn_refresh_token(mfconn * conn)
{
    int             retval;

    free(conn->secret_time);
    conn->secret_time = NULL;
    free(conn->session_token);
    conn->session_token = NULL;
    free(conn->ekey);
    conn->ekey = NULL;
    retval = mfconn_api_user_get_session_token(conn, conn->server,
                                               conn->username, conn->password,
                                               conn->app_id, conn->app_key,
                                               &(conn->secret_key),
                                               &(conn->secret_time),
                                               &(conn->session_token),
                                               &(conn->ekey));
    if (retval != 0) {
        fprintf(stderr, "user/get_session_token failed\n");
        return -1;
    }
    return 0;
}

void mfconn_set_flags(mfconn * conn, unsigned int flags)
{
    if(conn == NULL) return;

    conn->flags = flags;
}

unsigned int mfconn_get_flags(mfconn * conn)
{
    if(conn == NULL) return 0;

    return conn->flags;
}

void mfconn_destroy(mfconn * conn)
{
    free(conn->server);
    free(conn->username);
    free(conn->password);
    if (conn->app_key != NULL)
        free(conn->app_key);
    free(conn->secret_time);
    free(conn->session_token);
    free(conn->ekey);
    free(conn);
}

void mfconn_update_secret_key(mfconn * conn)
{
    uint64_t        new_val;

    if (conn == NULL)
        return;

    new_val = ((uint64_t) conn->secret_key) * 16807;
    new_val %= 0x7FFFFFFF;

    conn->secret_key = new_val;

    return;
}

const char     *mfconn_create_user_signature(mfconn * conn,
                                             const char *username,
                                             const char *password, int app_id,
                                             const char *app_key)
{
    char           *signature_raw;
    unsigned char   signature_enc[20];  // sha1 is 160 bits
    char            signature_hex[41];
    int             i;

    if (conn == NULL)
        return NULL;

    if (app_key == NULL) {
        signature_raw = strdup_printf("%s%s%d", username, password, app_id);
    } else {
        signature_raw = strdup_printf("%s%s%d%s",
                                      username, password, app_id, app_key);
    }

    SHA1((const unsigned char *)signature_raw,
         strlen(signature_raw), signature_enc);

    free(signature_raw);

    for (i = 0; i < 20; i++) {
        sprintf(&signature_hex[i * 2], "%02x", signature_enc[i]);
    }
    signature_hex[40] = '\0';

    return strdup((const char *)signature_hex);
}

const char     *mfconn_create_call_signature(mfconn * conn, const char *url,
                                             const char *args)
{
    char           *signature_raw;
    unsigned char   signature_enc[16];  // md5 is 128 bits
    char            signature_hex[33];
    char           *api;
    int             i;

    if (conn == NULL)
        return NULL;
    if (url == NULL)
        return NULL;
    if (args == NULL)
        return NULL;

    // printf("url: %s\n\rargs: %s\n\r",url,args);

    api = strstr(url, "/api/");

    if (api == NULL)
        return NULL;

    signature_raw = strdup_printf("%d%s%s%s",
                                  (conn->secret_key % 256),
                                  conn->secret_time, api, args);
    if (signature_raw == NULL) {
        fprintf(stderr, "strdup_printf failed\n");
        return NULL;
    }

    MD5((const unsigned char *)signature_raw,
        strlen(signature_raw), signature_enc);

    free(signature_raw);

    for (i = 0; i < 16; i++) {
        sprintf(&signature_hex[i * 2], "%02x", signature_enc[i]);
    }
    signature_hex[32] = '\0';

    return strdup((const char *)signature_hex);
}

const char     *mfconn_create_unsigned_get(mfconn * conn, int ssl,
                                           const char *api, const char *fmt,
                                           ...)
{
    char           *api_request = NULL;
    char           *api_args = NULL;
    int             bytes_to_alloc;
    int             api_args_len;
    int             api_len;
    va_list         ap;

    if (conn == NULL) {
        fprintf(stderr, "conn cannot be NULL\n");
        return NULL;
    }
    if (conn->server == NULL) {
        fprintf(stderr, "server cannot be NULL\n");
        return NULL;
    }
    // make sure the api (ex: user/get_info.php) is sane
    if (api == NULL) {
        fprintf(stderr, "api call cannot be NULL\n");
        return NULL;
    }
    api_len = strlen(api);
    if (api_len < 3) {
        fprintf(stderr, "api call length cannot be less than 3\n");
        return NULL;
    }
    // calculate how big of a buffer we need
    va_start(ap, fmt);
    api_args_len = (vsnprintf(NULL, 0, fmt, ap) + 1);   // + 1 for NULL
    va_end(ap);

    // create the correctly sized buffer and process the args
    api_args = (char *)calloc(api_args_len, sizeof(char));

    // printf("\n\r%d\n\r",api_args_len);

    va_start(ap, fmt);
    vsnprintf(api_args, api_args_len, fmt, ap);
    va_end(ap);

    // correct user error of trailing slash
    if (api[api_len - 1] == '/') {
        fprintf(stderr, "api call must not end with slash\n");
        return NULL;
    }

    api_request = strdup_printf("%s//%s/api/%s/%s",
                                (ssl ? "https:" : "http:"),
                                conn->server, MFAPI_VERSION, api);

    // compute the amount of space requred to realloc() the request
    bytes_to_alloc = api_args_len;
    bytes_to_alloc += strlen(api_request);
    bytes_to_alloc += 1;        // null termination

    // append api GET args to api request
    api_request = (char *)realloc(api_request, bytes_to_alloc);
    if (api_request == NULL) {
        fprintf(stderr, "cannot allocate memory\n");
        return NULL;
    }

    strncat(api_request, api_args, api_args_len);

    free(api_args);

    return api_request;
}

const char     *mfconn_create_signed_get(mfconn * conn, int ssl,
                                         const char *api, const char *fmt, ...)
{
    char           *api_request = NULL;
    char           *api_args = NULL;
    char           *signature;
    const char     *call_hash;
    char           *session_token;
    int             bytes_to_alloc;
    int             api_args_len;
    int             api_len;
    va_list         ap;

    if (conn == NULL) {
        fprintf(stderr, "conn cannot be NULL\n");
        return NULL;
    }
    if (conn->server == NULL) {
        fprintf(stderr, "server cannot be NULL\n");
        return NULL;
    }
    if (conn->secret_time == NULL) {
        fprintf(stderr, "secret_time cannot be NULL\n");
        return NULL;
    }
    if (conn->session_token == NULL) {
        fprintf(stderr, "session_token cannot be NULL\n");
        return NULL;
    }
    // make sure the api (ex: user/get_info.php) is sane
    if (api == NULL) {
        fprintf(stderr, "api name cannot be NULL\n");
        return NULL;
    }
    api_len = strlen(api);
    if (api_len < 3) {
        fprintf(stderr, "api name length cannot be less than 3\n");
        return NULL;
    }
    // calculate how big of a buffer we need
    va_start(ap, fmt);
    api_args_len = (vsnprintf(NULL, 0, fmt, ap) + 1);   // + 1 for NULL
    va_end(ap);

    session_token = strdup_printf("&session_token=%s", conn->session_token);

    api_args_len += strlen(session_token);

    // create the correctly sized buffer and process the args
    api_args = (char *)calloc(api_args_len, sizeof(char));

    // printf("\n\r%d\n\r",api_args_len);

    va_start(ap, fmt);
    vsnprintf(api_args, api_args_len, fmt, ap);
    va_end(ap);

    strcat(api_args, session_token);
    free(session_token);

    // correct user error of trailing slash
    if (api[api_len - 1] == '/') {
        fprintf(stderr, "api name cannot end with slash\n");
        return NULL;
    }

    api_request = strdup_printf("%s//%s/api/%s/%s",
                                (ssl ? "https:" : "http:"),
                                conn->server, MFAPI_VERSION, api);

    call_hash = mfconn_create_call_signature(conn, api_request, api_args);
    signature = strdup_printf("&signature=%s", call_hash);
    free((void *)call_hash);

    // compute the amount of space requred to realloc() the request
    bytes_to_alloc = api_args_len;
    bytes_to_alloc += strlen(api_request);
    bytes_to_alloc += strlen(signature);
    bytes_to_alloc += 1;        // null termination

    // append api GET args to api request
    api_request = (char *)realloc(api_request, bytes_to_alloc);
    if (api_request == NULL) {
        fprintf(stderr, "cannot allocate memory\n");
        return NULL;
    }

    strncat(api_request, api_args, api_args_len);
    strcat(api_request, signature);

    free(signature);
    free(api_args);

    return api_request;
}

const char     *mfconn_get_session_token(mfconn * conn)
{
    return conn->session_token;
}

const char     *mfconn_get_secret_time(mfconn * conn)
{
    return conn->secret_time;
}

uint32_t mfconn_get_secret_key(mfconn * conn)
{
    return conn->secret_key;
}

int mfconn_get_max_num_retries(mfconn * conn)
{
    return conn->max_num_retries;
}

const char     *mfconn_get_ekey(mfconn * conn)
{
    return conn->ekey;
}

int mfconn_upload_poll_for_completion(mfconn * conn, const char *upload_key)
{
    int             status;
    int             fileerror;
    int             retval;

    for (;;) {
        // no need to update the secret key after this
        retval = mfconn_api_upload_poll_upload(conn, upload_key, &status,
                                               &fileerror);
        if (retval != 0) {
            fprintf(stderr, "mfconn_api_upload_poll_upload failed\n");
            return -1;
        }
        fprintf(stderr, "status: %d, filerror: %d\n", status, fileerror);

        // values 98 and 99 are terminal states for a completed upload
        if (status == 99 || status == 98) {
            fprintf(stderr, "done\n");
            break;
        }
        sleep(1);
    }
    return 0;
}

