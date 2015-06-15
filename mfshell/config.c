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

#define _POSIX_C_SOURCE 200809L // for getline
#define _GNU_SOURCE             // for getline on old systems

#include <stdio.h>
#include <stdlib.h>
//#include <pwd.h>
//#include <unistd.h>
//#include <wordexp.h>
#include <string.h>
//#include <stddef.h>
#include <getopt.h>

#include "config.h"
#include "options.h"

#include "../utils/strings.h"
#include "../utils/config.h"
#include "../utils/http.h"

void parse_config(char *configfile,struct mfshell_user_options *opts)
{
    FILE            *fp;

    char            **argv = NULL;              // create our own argv
    int             argc;                       // create one own argc
    int             new_items = 0;

    fp = fopen(configfile,"r");
    if(fp == NULL) return;

    // getopt_long() expect at least argc >= 1 and argv[0] != NULL
    argv = (char**)calloc(1,sizeof(char*));
    argv[0] = strdup("mediafire-shell");
    argc = 1;

    new_items = config_file_read(fp, &argc, &argv);
    fprintf(stderr,"argc = %d\n", argc);

    parse_argv(argc, argv, opts);

    if(new_items > 0)
    {
        string_array_free(argv);
    }
    else
    {
        free(argv[0]);
        free(argv);
    }

    return;
}

void parse_argv(int argc, char *const argv[],
                struct mfshell_user_options *opts)
{
    static struct option long_options[] =
    {
        {"command", required_argument, 0, 'c'},
        {"config", required_argument, 0, 'f'},
        {"username", required_argument, 0, 'u'},
        {"password", required_argument, 0, 'p'},
        {"server", required_argument, 0, 's'},
        {"app-id", required_argument, 0, 'i'},
        {"api-key", required_argument, 0, 'k'},
        {"lazy-ssl", no_argument, 0, 'l'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    int             c;

    // resetting optind because we want to be able to call getopt_long
    // multiple times for the commandline arguments as well as for the
    // configuration file
    optind = 0;
    for (;;) {
        c = getopt_long(argc, argv, "c:u:p:s:lhv", long_options, NULL);
        if (c == -1)
            break;

        switch (c) {
            case 'c':
                if (opts->command == NULL)
                    opts->command = strdup(optarg);
                break;
            case 'u':
                if (opts->username == NULL)
                    opts->username = strdup(optarg);
                break;
            case 'p':
                if (opts->password == NULL)
                    opts->password = strdup(optarg);
                break;
            case 's':
                if (opts->server == NULL)
                    opts->server = strdup(optarg);
                break;
            case 'l':
                opts->http_flags |= HTTP_FLAG_LAZY_SSL;
                break;
            case 'f':
                if (opts->config == NULL)
                    opts->config = strdup(optarg);
            case 'i':
                if (opts->app_id == -1)
                    opts->app_id = atoi(optarg);
            case 'k':
                if (opts->api_key == NULL)
                    opts->api_key = strdup(optarg);
            case 'h':
                print_help(argv[0]);
                exit(0);
            case 'v':
                exit(0);
            case '?':
                exit(1);
                break;
            default:
                fprintf(stderr, "getopt_long returned character code %c\n", c);
        }
    }

    if (optind < argc) {
        // TODO: handle non-option argv elements
        fprintf(stderr, "Unexpected positional arguments\n");
        exit(1);
    }

    if (opts->password != NULL && opts->username == NULL) {
        fprintf(stderr, "You cannot pass the password without the username\n");
        exit(1);
    }
}



