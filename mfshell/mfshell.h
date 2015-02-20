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

#ifndef _MFSHELL_H_
#define _MFSHELL_H_

#ifdef __linux__
#include <sys/ioctl.h>
#endif

#include "../mfapi/folder.h"
#include "../mfapi/mfconn.h"

struct mfcmd;
struct mfshell;

typedef struct mfcmd mfcmd;
typedef struct mfshell mfshell;

struct mfcmd {
    char           *name;
    char           *argstring;
    char           *help;
    int             (*handler) (mfshell * mfshell, int argc,
                                char *const argv[]);
};

struct mfshell {
    int             app_id;
    char           *app_key;
    char           *server;
    unsigned int    flags;

    /* REST API tracking */
    mffolder       *folder_curr;

    /* Local tracking */
    char           *local_working_dir;

    /* shell commands */
    mfcmd          *commands;

    mfconn         *conn;

#ifdef TIOCGWINSZ
    struct winsize  terminal_sz;
#endif
};

mfshell        *mfshell_create(int app_id, char *app_key, char *server,
                               unsigned int flags);

int             mfshell_authenticate_user(mfshell * mfshell);

int             mfshell_exec(mfshell * mfshell, int argc, char *const argv[]);

int             mfshell_exec_shell_command(mfshell * mfshell, char *command);

void            mfshell_parse_commands(mfshell * shell, char *command);

void            mfshell_run(mfshell * shell);

void            mfshell_destroy(mfshell * shell);

#endif
