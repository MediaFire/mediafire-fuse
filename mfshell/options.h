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

#ifndef _MFSHELL_OPTIONS_H_
#define _MFSHELL_OPTIONS_H_

// bit flags for controlling runtime tweaks
#define MFOPTS_LAZY_SSL         (1U << 0)

struct mfshell_user_options {
    char           *username;
    char           *password;
    char           *command;
    char           *server;
    char           *config;
    int             app_id;
    char           *api_key;
    unsigned int    flags;
};

void            print_help(const char *cmd);

void            parse_argv(int argc, char *const argv[],
                           struct mfshell_user_options *opts);

#endif
