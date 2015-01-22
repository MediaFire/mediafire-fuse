/*
 * Copyright (C) 2014 Johannes Schauer <j.schauer@email.de>
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
#define _GNU_SOURCE             // for strdup

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include "../../mfapi/apicalls.h"
#include "../../mfapi/mfconn.h"
#include "../mfshell.h"
#include "../../mfapi/folder.h"
#include "../commands.h"        // IWYU pragma: keep

int mfshell_cmd_put(mfshell * mfshell, int argc, char *const argv[])
{
    int             retval;
    const char     *file_path;
    char           *temp;
    char           *file_name;
    char           *upload_key = NULL;
    FILE           *fh;

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

    if (argv[1] == NULL)
        return -1;

    file_path = argv[1];

    fh = fopen(file_path, "r");
    if (fh == NULL) {
        perror("fopen");
        fprintf(stderr, "cannot open %s\n", file_path);
        return -1;
    }
    // create copies because basename modifies it
    temp = strdup(argv[1]);
    file_name = basename(temp);

    retval = mfconn_api_upload_simple(mfshell->conn,
                                      folder_get_key(mfshell->folder_curr),
                                      fh, file_name, &upload_key);

    fclose(fh);
    free(temp);

    if (retval != 0 || upload_key == NULL) {
        fprintf(stderr, "mfconn_api_upload_simple failed\n");
        return -1;
    }

    fprintf(stderr, "upload_key: %s\n", upload_key);

    // poll for completion
    retval = mfconn_upload_poll_for_completion(mfshell->conn, upload_key);
    free(upload_key);

    if (retval != 0) {
        fprintf(stderr, "mfconn_upload_poll_for_completion failed\n");
        return -1;
    }

    return 0;
}
