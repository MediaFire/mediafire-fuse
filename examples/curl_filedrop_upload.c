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

    This program illustrates how to use libcurl to perform a simple file
    upload to a MediaFire filedrop.  It is the most basic of upload operations
    because it does not require any authenticaition.  In order for this to
    work on your system you will need to replace MF_FILEDROP with an
    active filedrop key.
*/

/*
    Building:

    Something like this:

    gcc -o upload_filedrop curl_filedrop_upload.c -lcurl
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

#include <curl/curl.h>
#include <curl/easy.h>

#define MF_FILEDROP     "1619f03927bdbf4a2470703ab1a746b8725d07e878206d27"

#define MFAPI_ROOT      "https://www.mediafire.com/api/1.3/"
#define ENDPOINT_UPLOAD "upload/simple.php"


CURL*   curl_init_set(CURL *);
char*   strdup_printf(char *, ...);
int     upload_filedrop(CURL *, char *, char *, char *, char *);
size_t  http_read_file_cb(char *, size_t, size_t, void *);

int
main(int argc, char **argv)
{
    CURL    *curl_handle = NULL;

    // arg[1] should be a file to upload.  must be in current dir.
    if(argc < 2) return -1;

    upload_filedrop(curl_handle,
                    MFAPI_ROOT, ENDPOINT_UPLOAD, argv[1], MF_FILEDROP);

    return 0;
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

int
upload_filedrop(CURL *curl_handle, char *api_root, char *endpoint,
                char *filepath, char *filedrop)
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
                                "&filedrop_key=%s",
                                 url, filedrop);

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

