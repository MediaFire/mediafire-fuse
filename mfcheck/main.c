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

#define _POSIX_C_SOURCE 200809L     // for strdup
#define _GNU_SOURCE                 // for strdup on old systems
#define __BSD_VISIBLE   200809L     // required for SIGWINCH on BSD

#include <openssl/ssl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

#include "../utils/strings.h"
#include "../utils/http.h"
#include "../utils/config.h"

int main(int argc, char *const argv[])
{
    char    *configfile = NULL;
    FILE    *fp;

    int     argc_copy = 0;
    char    **argv_copy = NULL;

    (void)argc;
    (void)argv;

    argv_copy = (char **)calloc(1,sizeof(char*));
    argv_copy[0] = strdup("mediafire-check");
    argc_copy = 1;

    SSL_library_init();

    config_file_init(&configfile);

    if(configfile != NULL)
    {
        fp = fopen(configfile, "r");
        config_file_read(fp, &argc_copy, &argv_copy);
    }

    return 0;
}
