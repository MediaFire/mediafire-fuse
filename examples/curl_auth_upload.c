/*
 * Copyright (C) 2013 Bryan Christ <bryan.christ@mediafire.com>
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


/*
    Summary:

    This program illustrates how to use libcurl to perform an authenticated
    upload to the root folder of your MediaFire account.  In order for this to
    work on your system, you will need to replace MF_USERNAME and
    MF_PASSWORD with your respective account credentials.
*/

/*
    Building:

    Something like this:

    gcc -o upload_auth curl_auth_upload.c -lcurl -lssl -lcrypto -ljansson
*/

/*
    Running:

    This program accepts only one argument which is the name of a file.
    The file *must* be located in the current directory because no
    attempt is made to parse a file path.
*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>

#include <jansson.h>

#include <curl/curl.h>
#include <curl/easy.h>
#include <openssl/sha.h>

#define MF_USERNAME     "fake.email@gmail.com"
#define MF_PASSWORD     "fakepwd"
#define MF_APP_ID       45905

#define MFAPI_ROOT      "https://www.mediafire.com/api/1.3/"
#define ENDPOINT_AUTH   "user/get_session_token.php"
#define ENDPOINT_UPLOAD "upload/simple.php"

typedef struct  _credentials_s  credentials_t;
typedef struct  _response_buf_s response_buf_t;

struct _credentials_s
{
    int         app_id;
    char        *username;
    char        *password;
    char        *user_signature;
    char        *session_token;
};

struct _response_buf_s
{
    size_t  size;
    char    *data;
};

void    compute_user_signature(credentials_t *);
CURL*   curl_init_set(CURL *);
int     user_get_session_token(CURL *, credentials_t *, char *, char *);
char*   strdup_printf(char *, ...);
char*   urlencode(const char *);
size_t  response_buf_cb(char *, size_t, size_t, void *);
int     decode_get_session_token(char *, credentials_t *);
int     upload_simple(CURL *, credentials_t *, char *, char *, char *);
size_t  http_read_file_cb(char *, size_t, size_t, void *);

int
main(int argc, char **argv)
{
    credentials_t   *credentials;

    char    *session_token = NULL;  // only good for about 7 minutes
    CURL    *curl_handle = NULL;

    // arg[1] should be a file to upload.  must be in current dir.
    if(argc < 2) return -1;

    credentials = (credentials_t*)calloc(1,sizeof(credentials_t));

    credentials->app_id = MF_APP_ID;
    credentials->username = strdup(MF_USERNAME);
    credentials->password = strdup(MF_PASSWORD);

    // compute and authorization signature
    compute_user_signature(credentials);

    user_get_session_token(curl_handle, credentials,
                            MFAPI_ROOT, ENDPOINT_AUTH);

    if(credentials->session_token == NULL)
    {
        printf("[EE] auth failed\n");
        exit(-1);
    }

    upload_simple(curl_handle, credentials,
                    MFAPI_ROOT, ENDPOINT_UPLOAD, argv[1]);

    return 0;
}

void
compute_user_signature(credentials_t *credentials)
{
    char           *signature_raw;
    unsigned char   signature_enc[20];  // sha1 is 160 bits
    char            signature_hex[41];
    int             i;

    signature_raw = strdup_printf(  "%s%s%d",
                                    credentials->username,
                                    credentials->password,
                                    credentials->app_id);

    SHA1((const unsigned char *)signature_raw,
         strlen(signature_raw), signature_enc);

    free(signature_raw);

    for (i = 0; i < 20; i++)
    {
        sprintf(&signature_hex[i * 2], "%02x", signature_enc[i]);
    }
    signature_hex[40] = '\0';

    credentials->user_signature = strdup((const char *)signature_hex);

    return;
}

CURL*
curl_init_set(CURL *curl_handle)
{
    curl_version_info_data      *cv_info = NULL;

    cv_info = curl_version_info(CURLVERSION_NOW);

    fprintf(stderr,"resetting curl\n");
    fprintf(stderr,"curl version is %s\n", cv_info->version);

    if(curl_handle == NULL)
    {
        curl_handle = curl_easy_init();
    }
    else
    {
        curl_easy_reset(curl_handle);
    }

    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl_handle, CURLOPT_SSLENGINE, NULL);
    curl_easy_setopt(curl_handle, CURLOPT_SSLENGINE_DEFAULT, 1L);

    // it should *never* take 5 seconds to establish a connection to the server
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5);

    return curl_handle;
}

int
user_get_session_token(CURL *curl_handle, credentials_t *credentials,
                        char *api_root, char *endpoint)
{
    response_buf_t  *response_buf = NULL;
    char            *url = NULL;
    char            *username_urlenc = NULL;
    char            *password_urlenc = NULL;
    char            *post_args = NULL;
    int             retval = 0;


    url = strdup_printf("%s%s", api_root, endpoint);
    username_urlenc = urlencode(credentials->username);
    password_urlenc = urlencode(credentials->password);

    post_args = strdup_printf(  "email=%s"
                                "&password=%s"
                                "&application_id=%d"
                                "&signature=%s"
                                "&token_version=1"
                                "&response_format=json",
                                username_urlenc,
                                password_urlenc,
                                credentials->app_id,
                                credentials->user_signature);

    if(username_urlenc != NULL) free(username_urlenc);
    if(password_urlenc != NULL) free(password_urlenc);

    response_buf = (response_buf_t*)calloc(1, sizeof(response_buf_t));
    curl_handle = curl_init_set(curl_handle);

    fprintf(stderr, "[II] %s\n", url);
    fprintf(stderr, "[II] %s\n", post_args);

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_args);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, response_buf_cb);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)response_buf);
    // curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);

    retval = curl_easy_perform(curl_handle);

    if(url != NULL) free(url);
    if(post_args != NULL) free(post_args);

    if(retval == CURLE_OK)
    {
        fprintf(stderr,"[II] get_session_token = SUCCESS\n");
    }

    if(response_buf->data != NULL)
    {
        if(retval == CURLE_OK)
        {
            decode_get_session_token(response_buf->data, credentials);

            fprintf(stderr, "[II] session token = %s\n",
                    credentials->session_token);

            retval = 0;
        }
        else retval = -1;

        free(response_buf->data);
    }

    if(response_buf != NULL) free(response_buf);

    return retval;
}


char*
strdup_printf(char *fmt, ...)
{
    // Good for glibc 2.1 and above. Fedora5 is 2.4.

    char           *ret_str = NULL;
    va_list         ap;
    int             bytes_to_allocate;

    va_start(ap, fmt);
    bytes_to_allocate = vsnprintf(ret_str, 0, fmt, ap);

    // Add one for '\0'
    bytes_to_allocate++;

    ret_str = (char *)malloc(bytes_to_allocate * sizeof(char));
    if (ret_str == NULL) {
        fprintf(stderr, "failed to allocate memory\n");
        return NULL;
    }

    va_start(ap, fmt);
    bytes_to_allocate = vsnprintf(ret_str, bytes_to_allocate, fmt, ap);
    va_end(ap);

    return ret_str;
}


char*
urlencode(const char *inp)
{
    char           *buf;
    char           *bufp;
    char            hex[] = "0123456789abcdef";

    // allocating three times the length of the input because in the worst
    // case each character must be urlencoded and add a byte for the
    // terminating zero
    bufp = buf = (char *)malloc(strlen(inp) * 3 + 1);

    if (buf == NULL)
    {
        fprintf(stderr, "malloc failed\n");
        return NULL;
    }

    while (*inp)
    {
        if ((*inp >= '0' && *inp <= '9')
            || (*inp >= 'A' && *inp <= 'Z')
            || (*inp >= 'a' && *inp <= 'z')
            || *inp == '-' || *inp == '_' || *inp == '.' || *inp == '~') {
            *bufp++ = *inp;
        }
        else
        {
            *bufp++ = '%';
            *bufp++ = hex[(*inp >> 4) & 0xf];
            *bufp++ = hex[*inp & 0xf];
        }
        inp++;
    }
    *bufp = '\0';

    return buf;
}

size_t
response_buf_cb(char *data, size_t size, size_t nmemb, void *anything)
{
    response_buf_t  *response_buf;
    char            *pos;
    size_t          old_size;
    size_t          new_size;
    size_t          payload_size;

    fprintf(stderr, "[II] getting data\n");

    if(anything == NULL) return 0;
    if(size == 0) return 0;

    response_buf = (response_buf_t*)anything;
    payload_size = (size * nmemb);

    old_size = response_buf->size;
    new_size = old_size + payload_size;

    response_buf->data = realloc(response_buf->data, new_size);

    if(old_size > 0)
        pos = &response_buf->data[old_size - 1];
    else
        pos = &response_buf->data[0];

    memcpy(pos, data, payload_size);

    return payload_size;
}

int
decode_get_session_token(char *response_buf, credentials_t *credentials)
{
    json_error_t    error;
    json_t         *root = NULL;
    json_t         *node;
    json_t         *j_obj;
    int             retval;
    size_t          buf_sz = 0;

    buf_sz = strlen(response_buf);

    root = json_loadb((const char*)response_buf, buf_sz, 0, &error);

    if (root == NULL)
    {
        fprintf(stderr, "http_parse_buf_json failed at line %d\n", error.line);
        fprintf(stderr, "error message: %s\n", error.text);
        return -1;
    }

    node = json_object_get(root, "response");

    j_obj = json_object_get(node, "session_token");
    if (j_obj == NULL) {
        json_decref(root);
        fprintf(stderr, "json: no /session_token content\n");
        return -1;
    }
    credentials->session_token = strdup(json_string_value(j_obj));

    json_decref(root);

    return 0;
}

int
upload_simple(CURL *curl_handle, credentials_t *credentials,
                        char *api_root, char *endpoint, char *filepath)
{
    FILE                *fh = NULL;
    struct curl_slist   *custom_headers = NULL;
    uint64_t            l_file_size;
    int                 retval;
    char                *url;
    char                *api_call;
    double              upload_speed;
    double              upload_time;
    char                *tmpstr = NULL;

    fprintf(stderr, "[II] opening %s\n", filepath);

    fh = fopen(filepath, "r");

    if(fh == NULL)
    {
        fprintf(stderr, "[EE] couldn't open specified file!\n");
        return -1;
    }

    // get the size of the file and then rewind
    retval = fseek(fh, 0, SEEK_END);
    l_file_size = ftell(fh);
    rewind(fh);

    curl_handle = curl_init_set(curl_handle);

    url = strdup_printf("%s%s", api_root, endpoint);

    api_call = strdup_printf("%s?action_on_duplicate=replace"
                                "&session_token=%s",
                                 url, credentials->session_token);

    custom_headers = curl_slist_append(custom_headers,
                    "Content-Type: application/octet-stream");

    custom_headers = curl_slist_append(custom_headers,
                    "Expect:");

    tmpstr = strdup_printf("x-filesize: %zd", l_file_size);
    custom_headers = curl_slist_append(custom_headers, tmpstr);
    free(tmpstr);

    tmpstr = strdup_printf("x-filename: %s", filepath);
    custom_headers = curl_slist_append(custom_headers, tmpstr);
    free(tmpstr);

    curl_easy_setopt(curl_handle, CURLOPT_POST, 1);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, custom_headers);
    curl_easy_setopt(curl_handle, CURLOPT_URL, api_call);
    curl_easy_setopt(curl_handle, CURLOPT_READFUNCTION,
                     http_read_file_cb);
    curl_easy_setopt(curl_handle, CURLOPT_READDATA, (void *)fh);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, l_file_size);
    curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);

    retval = curl_easy_perform(curl_handle);

    curl_easy_getinfo(curl_handle, CURLINFO_SPEED_UPLOAD, &upload_speed);
    curl_easy_getinfo(curl_handle, CURLINFO_TOTAL_TIME, &upload_time);

    fprintf(stderr, "Speed: %.3f bytes/sec during %.3f seconds\n",
            upload_speed, upload_time);

    curl_slist_free_all(custom_headers);
    custom_headers = NULL;

    fclose(fh);

    return 0;
}


size_t
http_read_file_cb(char *data, size_t size, size_t nmemb, void *user_ptr)
{
    FILE        *fh;
    size_t      ret;

    fh = (FILE*)user_ptr;

    ret = fread(data, size, nmemb, fh);

    return size * ret;
}

