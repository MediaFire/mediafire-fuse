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

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

#include "../mfshell.h"
#include "../options.h"
#include "../../utils/strings.h"
#include "../../utils/http.h"
#include "../../mfapi/mfconn.h"
#include "../commands.h"        // IWYU pragma: keep

int mfshell_cmd_auth(mfshell * mfshell, int argc, char *const argv[])
{
    char           *username;
    char           *password;
    unsigned int    flags = 0;

    if (mfshell == NULL)
        return -1;
    if (mfshell->server == NULL)
        return -1;

    switch (argc) {
        case 1:
            printf("login: ");
            username = string_line_from_stdin(false);
            printf("passwd: ");
            password = string_line_from_stdin(true);
            break;
        case 2:
            username = argv[1];
            printf("passwd: ");
            password = string_line_from_stdin(true);
            break;
        case 3:
            username = argv[1];
            password = argv[2];
            break;
        default:
            fprintf(stderr, "Invalid number of arguments\n");
            return -1;
    }

    if (username == NULL || password == NULL)
        return -1;

    // there's probably a much better way to pass this along but it's
    // handled deep inside the http layer.
    if (mfshell->flags & MFOPTS_LAZY_SSL) {

        flags |= HTTP_CONN_LAZY_SSL;
    }
    // create an auth configuration with a resilience of 3 retries
    // per API call
    mfshell->conn = mfconn_create(mfshell->server, username, password,
                                  mfshell->app_id, mfshell->app_key, 3);

    mfconn_set_flags(mfshell->conn, flags);

    if (mfshell->conn != NULL)
        printf("\n\rAuthentication SUCCESS\n\r");
    else
        printf("\n\rAuthentication FAILURE\n\r");

    return (mfshell->conn != NULL);
}
