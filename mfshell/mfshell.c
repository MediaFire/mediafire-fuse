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

#ifdef __linux
#define _POSIX_C_SOURCE 200809L // for strdup and getline
#define _DEFAULT_SOURCE         // for strsep
#else
#define _WITH_GETLINE           // on freebsd for getline
#endif
#define _GNU_SOURCE             // for strdup on old systems

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>
#include <signal.h>

#include "commands.h"
#include "mfshell.h"
#include "terminal.h"
#include "../mfapi/folder.h"

struct mfcmd    commands[] = {
    {"help", "", "show this help", mfshell_cmd_help},
    {"debug", "", "show debug information", mfshell_cmd_debug},
    {"host", "<server>", "change target server", mfshell_cmd_host},
    {"auth", "<user <pass>>", "authenticate with active server",
     mfshell_cmd_auth},
    {"whoami", "", "show basic user info", mfshell_cmd_whoami},
    {"ls", "", "show contents of active folder",
     mfshell_cmd_list},
    {"cd", "<folderkey>", "change active folder (default: root)",
     mfshell_cmd_chdir},
    {"pwd", "", "show the active folder", mfshell_cmd_pwd},
    {"lpwd", "", "show the local working directory",
     mfshell_cmd_lpwd},
    {"lcd", "[dir]", "change the local working directory",
     mfshell_cmd_lcd},
    {"mkdir", "[folder name]", "create a new folder", mfshell_cmd_mkdir},
    {"file", "[quickkey]", "show file information", mfshell_cmd_file},
    {"folder", "<folderkey>", "show folder information (default:root)",
     mfshell_cmd_folder},
    {"links", "[quickkey]", "show access urls for the file",
     mfshell_cmd_links},
    {"get", "[quickkey]", "download a file", mfshell_cmd_get},
    {"put", "[local filename]", "upload a file", mfshell_cmd_put},
    {"rmdir", "[folderkey]", "remove directory", mfshell_cmd_rmdir},
    {"rm", "[quickkey]", "remove file", mfshell_cmd_rm},
    {"status", "", "device status", mfshell_cmd_status},
    {"changes", "<revision>", "device changes (default: 0)",
     mfshell_cmd_changes},
    {"updates", "[quickkey] [revision]", "list available updates for a file",
     mfshell_cmd_updates},
    {NULL, NULL, NULL, NULL}
};

mfshell        *mfshell_create(int app_id, char *app_key, char *server,
                               unsigned int flags)
{
    mfshell        *shell;

    if (app_id <= 0)
        return NULL;
    if (server == NULL)
        return NULL;

    /*
       check to see if the server contains a forward-slash.  if so,
       the caller did not understand the API and passed in the wrong
       type of server resource.
     */
    if (strchr(server, '/') != NULL)
        return NULL;

    shell = (mfshell *) calloc(1, sizeof(mfshell));

    shell->app_id = app_id;
    if (app_key == NULL) {
        shell->app_key = NULL;
    } else {
        shell->app_key = strdup(app_key);
    }
    shell->server = strdup(server);

    // object to track folder location
    shell->folder_curr = folder_alloc();
    // set current folder to root
    folder_set_key(shell->folder_curr, NULL);

    // shell commands
    shell->commands = commands;

    // set flags bit mask
    shell->flags = flags;

    return shell;
}

int mfshell_exec(mfshell * shell, int argc, char *const argv[])
{
    mfcmd          *curr_cmd;

    if (shell == NULL)
        return -1;

    for (curr_cmd = shell->commands; curr_cmd->name != NULL; curr_cmd++) {
        if (strcmp(argv[0], curr_cmd->name) == 0) {
            return curr_cmd->handler(shell, argc, argv);
        }
    }
    fprintf(stderr, "Invalid command: %s", argv[0]);
    return -1;
}

int mfshell_exec_shell_command(mfshell * shell, char *command)
{
    wordexp_t       p;
    int             retval;

    if (shell == NULL)
        return -1;
    if (command == NULL)
        return -1;

    // FIXME: handle non-zero return value of wordexp
    retval = wordexp(command, &p, WRDE_SHOWERR | WRDE_UNDEF);
    if (retval != 0) {
        switch (retval) {
            case WRDE_BADCHAR:
                fprintf(stderr, "wordexp: WRDE_BADCHAR\n");
                break;
            case WRDE_BADVAL:
                fprintf(stderr, "wordexp: WRDE_BADVAL\n");
                break;
            case WRDE_CMDSUB:
                fprintf(stderr, "wordexp: WRDE_CMDSUB\n");
                break;
            case WRDE_NOSPACE:
                fprintf(stderr, "wordexp: WRDE_NOSPACE\n");
                break;
            case WRDE_SYNTAX:
                fprintf(stderr, "wordexp: WRDE_SYNTAX\n");
                break;
        }
    }

    if (p.we_wordc < 1)
        return 0;

    if (p.we_wordv[0] == NULL)
        return 0;

    // TODO: handle retval
    retval = mfshell_exec(shell, p.we_wordc, p.we_wordv);

    wordfree(&p);

    return 0;
}

void mfshell_parse_commands(mfshell * shell, char *command)
{
    char           *next;
    int             ret;
    wordexp_t       p;

    // FIXME: don't split by semicolon but by unescaped/unquoted semicolon
    while ((next = strsep(&command, ";")) != NULL) {
        // FIXME: handle non-zero return value of wordexp
        ret = wordexp(next, &p, WRDE_SHOWERR | WRDE_UNDEF);
        if (ret != 0) {
            switch (ret) {
                case WRDE_BADCHAR:
                    fprintf(stderr, "wordexp: WRDE_BADCHAR\n");
                    break;
                case WRDE_BADVAL:
                    fprintf(stderr, "wordexp: WRDE_BADVAL\n");
                    break;
                case WRDE_CMDSUB:
                    fprintf(stderr, "wordexp: WRDE_CMDSUB\n");
                    break;
                case WRDE_NOSPACE:
                    fprintf(stderr, "wordexp: WRDE_NOSPACE\n");
                    break;
                case WRDE_SYNTAX:
                    fprintf(stderr, "wordexp: WRDE_SYNTAX\n");
                    break;
            }
        }
        if (p.we_wordc < 1) {
            fprintf(stderr, "Need more than zero arguments\n");
            exit(1);
        }
        mfshell_exec(shell, p.we_wordc, p.we_wordv);
        wordfree(&p);
    }
}

void mfshell_run(mfshell * shell)
{
    char                   *cmd = NULL;
    size_t                  len;
    int                     abort = 0;
    int                     retval;
    extern sig_atomic_t     dirty_term_flag;

    // force the terminal size to be read at startup so we can get the
    // dimensions.
    dirty_term_flag = 1;
    terminal_rectify(shell);

    do {
        printf("\n\rmfshell > ");

        retval = getline(&cmd, &len, stdin);
        if (retval == -1) {
            exit(1);
        }

        if (cmd[strlen(cmd) - 1] == '\n')
            cmd[strlen(cmd) - 1] = '\0';

        printf("\n\r");

        if (strcmp(cmd, "exit") == 0) {
            abort = 1;
            continue;
        }

        if (strcmp(cmd, "quit") == 0) {
            abort = 1;
            continue;
        }

        // check if the terminal has changed since last command
        terminal_rectify(shell);

        retval = mfshell_exec_shell_command(shell, cmd);
        free(cmd);
        cmd = NULL;
    }
    while (abort == 0);

    return;
}

void mfshell_destroy(mfshell * shell)
{
    if (shell->app_key != NULL)
        free(shell->app_key);
    free(shell->server);
    free(shell->local_working_dir);
    folder_free(shell->folder_curr);
    if (shell->conn != NULL)
        mfconn_destroy(shell->conn);
    free(shell);
}
