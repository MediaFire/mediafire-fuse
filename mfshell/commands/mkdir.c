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
#include <stdlib.h>

#include "../mfshell.h"
#include "../commands.h"
#include "../../mfapi/apicalls.h"

int
mfshell_cmd_mkdir(mfshell *mfshell, int argc, char **argv)
{
    int         retval;
    const char  *folder_curr;
    const char *name;

    if(mfshell == NULL) return -1;

    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments\n");
        return -1;
    }

    name = argv[1];
    if (name == NULL) return -1;

    folder_curr = folder_get_key(mfshell->folder_curr);

    // safety check... this should never happen
    if(folder_curr == NULL)
        folder_set_key(mfshell->folder_curr,"myfiles");

    // safety check... this should never happen
    if(folder_curr[0] == '\0')
        folder_set_key(mfshell->folder_curr,"myfiles");

    folder_curr = folder_get_key(mfshell->folder_curr);

    retval = mfconn_api_folder_create(mfshell->conn,(char*)folder_curr,(char*)name);
    mfconn_update_secret_key(mfshell->conn);

    return retval;
}

