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
#include <string.h>

#include "../../mfapi/apicalls.h"
#include "../mfshell.h"
#include "../../mfapi/folder.h"
#include "../commands.h"        // IWYU pragma: keep

int mfshell_cmd_chdir(mfshell * mfshell, int argc, char *const argv[])
{
    mffolder       *folder_new;
    const char     *folder_curr;
    const char     *folder_parent;
    const char     *folderkey;
    int             retval;

    if (mfshell == NULL)
        return -1;

    switch (argc) {
        case 1:
            folderkey = NULL;
            break;
        case 2:
            folderkey = argv[1];
            break;
        default:
            fprintf(stderr, "Invalid number of arguments\n");
            return -1;
    }

    // user wants to navigate up a level
    if (strcmp(folderkey, "..") == 0) {
        // do several sanity checks to see if we're already at the root
        folder_curr = folder_get_key(mfshell->folder_curr);

        if (folder_curr == NULL)
            return 0;

        folder_parent = folder_get_parent(mfshell->folder_curr);

        if (folder_parent == NULL)
            return 0;

        // it's pretty sure that we're not at the root
        folderkey = folder_parent;
    }

    // handle return to root
    if (strcmp(folderkey, "/") == 0) {

        folderkey = NULL;
    }

    // check the length of the key
    if (folderkey != NULL) {

        if(strlen(folderkey) != 13) return -1;
    }

    // create a new folder object to store the results
    folder_new = folder_alloc();

    // navigate to root is a special case
    if (folderkey == NULL) {
        folder_set_key(folder_new, NULL);
        retval = 0;
    } else {
        if (mfshell->conn == NULL) {
            fprintf(stderr, "conn is NULL\n");
            return -1;
        }
        retval = mfconn_api_folder_get_info(mfshell->conn,
                                            folder_new, (char *)folderkey);

        if (retval != 0) {
            fprintf(stderr, "mfconn_api_folder_get_info failed\n");
            return -1;
        }
    }

    if (retval == 0) {
        if (mfshell->folder_curr != NULL) {
            folder_free(mfshell->folder_curr);
            mfshell->folder_curr = NULL;
        }

        mfshell->folder_curr = folder_new;
    } else {
        folder_free(folder_new);
    }

    return retval;
}
