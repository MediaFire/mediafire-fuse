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

#define _POSIX_C_SOURCE 200809L // for strdup and getline
#define _GNU_SOURCE             // for strdup on old systems

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mfshell.h"
#include "../../mfapi/mfconn.h"
#include "../commands.h"        // IWYU pragma: keep
#include "../../utils/helpers.h"

static char    *_get_host_from_user(void);

int mfshell_cmd_host(mfshell * mfshell, int argc, char *const argv[])
{
    char           *alt_host = NULL;
    char           *host;

    switch (argc) {
        case 1:
            alt_host = _get_host_from_user();
            host = alt_host;
            break;
        case 2:
            host = argv[1];
            break;
        default:
            fprintf(stderr, "Invalid number of arguments\n");
            return -1;
    }

    if (mfshell->server != NULL) {
        // do nothing if the server is exactly the same
        if (strcmp(mfshell->server, host) == 0) {
            if (alt_host != NULL)
                free(alt_host);
            return 0;
        }

        if (mfshell->server != NULL) {
            free(mfshell->server);
            mfshell->server = NULL;
        }
    }

    mfshell->server = strdup(host);

    mfconn_destroy(mfshell->conn);

    if (alt_host != NULL)
        free(alt_host);

    return 0;
}

char *
_get_host_from_user(void)
{
    size_t          len = 0;
    char           *host = NULL;
    retval_t        retval;

    printf("host: [www.mediafire.com] ");

    retval.i = (int)getline(&host, &len, stdin);
    if(retval.i == 0) return NULL;

    if (host[strlen(host) - 1] == '\n')
        host[strlen(host) - 1] = '\0';

    if (host == NULL)
        return strdup("www.mediafire.com");

    if (strlen(host) < 2) {
        free(host);
        return strdup("www.mediafire.com");
    }

    return host;
}
