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

#define _POSIX_C_SOURCE 200809L // for strdup
#define _GNU_SOURCE             // for strdup on old systems

#include <openssl/ssl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../utils/strings.h"
#include "../utils/http.h"
#include "mfshell.h"
#include "config.h"
#include "options.h"

int main(int argc, char *const argv[])
{
    mfshell        *shell;
    char           *auth_cmd;
    unsigned int    flags = 0;

    struct mfshell_user_options opts = {
        .username = NULL,
        .password = NULL,
        .command = NULL,
        .server = NULL,
        .config = NULL,
        .app_id = -1,
        .api_key = NULL,
        .flags = 0,
    };

    SSL_library_init();

    parse_argv(argc, argv, &opts);

    parse_config(&opts);

    // if server was neither set on the commandline nor in the config, set it
    // to the default value
    if (opts.server == NULL) {
        opts.server = strdup("www.mediafire.com");
    }
    // if app_id was neither set on the commandline nor in the config, set it
    // to the default value
    if (opts.app_id == -1)
        opts.app_id = 42709;

    if (opts.flags & MFOPTS_LAZY_SSL) {

        flags |= HTTP_CONN_LAZY_SSL;
    }

    shell = mfshell_create(opts.app_id, opts.api_key, opts.server, flags);
    if (shell == NULL) {
        fprintf(stderr, "cannot create shell\n");
        exit(1);
    }
    // if at least username was set, authenticate automatically
    if (opts.username != NULL) {
        if (opts.password != NULL) {
            auth_cmd = strdup_printf("auth %s %s",
                                     opts.username, opts.password);
        } else {
            auth_cmd = strdup_printf("auth %s", opts.username);
        }
        mfshell_parse_commands(shell, auth_cmd);
        free(auth_cmd);
    }

    if (opts.command == NULL) {
        // begin shell mode
        mfshell_run(shell);
    } else {
        // interpret command
        mfshell_parse_commands(shell, opts.command);
    }

    mfshell_destroy(shell);

    if (opts.server != NULL)
        free(opts.server);
    if (opts.username != NULL)
        free(opts.username);
    if (opts.password != NULL)
        free(opts.password);
    if (opts.command != NULL)
        free(opts.command);
    if (opts.config != NULL)
        free(opts.config);
    if (opts.api_key != NULL)
        free(opts.api_key);

    fclose(stderr);

    return 0;
}
