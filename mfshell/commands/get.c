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

#define _POSIX_C_SOURCE 200809L // for PATH_MAX

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../../mfapi/apicalls.h"
#include "../mfshell.h"
#include "../../mfapi/file.h"
#include "../commands.h"        // IWYU pragma: keep
#include "../../utils/strings.h"
#include "../../utils/http.h"

int mfshell_cmd_get(mfshell * mfshell, int argc, char *const argv[])
{
    mffile         *file;
    int             len;
    int             retval;
    ssize_t         bytes_read;
    const char     *quickkey;
    const char     *file_path;
    const char     *file_name;
    const char     *url;
    struct stat     file_info;
    mfhttp         *http;

    if (mfshell == NULL)
        return -1;

    if (mfshell->conn == NULL) {
        fprintf(stderr, "conn is NULL\n");
        return -1;
    }

    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments\n");
        return -1;
    }

    quickkey = argv[1];
    if (quickkey == NULL)
        return -1;

    len = strlen(quickkey);

    if (len != 11 && len != 15)
        return -1;

    file = file_alloc();

    // get file name
    retval = mfconn_api_file_get_info(mfshell->conn, file, (char *)quickkey);

    if (retval != 0) {
        file_free(file);
        return -1;
    }
    // request a direct download (streaming) link
    retval = mfconn_api_file_get_links(mfshell->conn, file,
                                       (char *)quickkey,
                                       MFCONN_FILE_LINK_TYPE_DIRECT_DOWNLOAD);

    if (retval != 0) {
        file_free(file);
        return -1;
    }
    // make sure we have a valid working directory to download to
    if (mfshell->local_working_dir == NULL) {
        mfshell->local_working_dir =
            (char *)calloc(PATH_MAX + 1, sizeof(char));
        (void*)getcwd(mfshell->local_working_dir, PATH_MAX);
    }

    file_name = file_get_name(file);
    if (file_name == NULL)
        return -1;
    if (strlen(file_name) < 1)
        return -1;

    file_path = strdup_printf("%s/%s", mfshell->local_working_dir, file_name);

    url = file_get_direct_link(file);
    if (url == NULL)
        return -1;

    http = http_create();
    retval = http_get_file(http, url, file_path);
    http_destroy(http);

    if (retval != 0)
        return -1;

    memset(&file_info, 0, sizeof(file_info));
    retval = stat(file_path, &file_info);

    free((void *)file_path);

    if (retval != 0)
        return -1;

    bytes_read = file_info.st_size;

    if (bytes_read != -1)
        printf("\r   Downloaded %zd bytes OK!\n\r", bytes_read);
    else
        printf("\r\n   Download FAILED!\n\r");

    file_free(file);

    return 0;
}
