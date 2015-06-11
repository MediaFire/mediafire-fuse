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

#include <sys/ioctl.h>

#include "../../mfapi/apicalls.h"
#include "../mfshell.h"
#include "../../mfapi/folder.h"
#include "../../mfapi/file.h"
#include "../commands.h"        // IWYU pragma: keep

int mfshell_cmd_list(mfshell * mfshell, int argc, char *const argv[])
{
    (void)argv;
    int             retval;
    mffolder      **folder_result;
    mffile        **file_result;
    int             i;
    int             term_width = 80;        // use ansi tty width by default

    if (mfshell == NULL)
        return -1;

    if (mfshell->conn == NULL) {
        fprintf(stderr, "conn is NULL\n");
        return -1;
    }

    if (argc != 1) {
        fprintf(stderr, "Invalid number of arguments\n");
        return -1;
    }
    // first folders
    folder_result = NULL;
    retval =
        mfconn_api_folder_get_content(mfshell->conn, 0,
                                      folder_get_key(mfshell->folder_curr),
                                      &folder_result, NULL);

    if (folder_result == NULL) {
        return -1;
    }

/*
    for (i = 0; folder_result[i] != NULL; i++) {
        printf("%s %s\n", folder_get_name(folder_result[i]),
               folder_get_key(folder_result[i]));
    }
*/

//#ifdef TIOCGWINSZ

    term_width = mfshell->terminal_sz.ws_col;

//#endif

    for (i = 0; folder_result[i] != NULL; i++) {
        printf("%-15s %.*s\n", folder_get_key(folder_result[i]),
               (term_width - 18),
               folder_get_name(folder_result[i]));
    }

    for (i = 0; folder_result[i] != NULL; i++) {
        folder_free(folder_result[i]);
    }
    free(folder_result);

    // then files
    file_result = NULL;
    retval =
        mfconn_api_folder_get_content(mfshell->conn, 1,
                                      folder_get_key(mfshell->folder_curr),
                                      NULL, &file_result);
/*
    for (i = 0; file_result[i] != NULL; i++) {
        printf("%s %s\n", file_get_name(file_result[i]),
               file_get_key(file_result[i]));
    }
*/

    for (i = 0; file_result[i] != NULL; i++) {
        printf("%-15s %.*s\n", file_get_key(file_result[i]),
               (term_width - 18),
               file_get_name(file_result[i]));
    }

    for (i = 0; file_result[i] != NULL; i++) {
        file_free(file_result[i]);
    }
    free(file_result);

    return retval;
}
