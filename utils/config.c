/*
 * Copyright (C) 2014, 2015
 * Johannes Schauer <j.schauer@email.de>
 * Bryan Christ <bryan.christ@gmail.com>
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
#include <unistd.h>
#include <pwd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <wordexp.h>
#include <string.h>
#include <stddef.h>

//#include <sys/types.h>
#include <sys/stat.h>

#include "config.h"

#include "strings.h"


void config_file_init(char **configfile)
{
    const char     *homedir;
    const char     *configdir;
    int             fd;

    homedir = getenv("HOME");
    if (homedir == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }

    configdir = getenv("XDG_CONFIG_HOME");
    if (configdir == NULL) {
        configdir = strdup_printf("%s/.config", homedir);
        /* EEXIST is okay, so only fail if it is something else */
        if (mkdir(configdir, 0755) != 0 && errno != EEXIST) {
            perror("mkdir");
            fprintf(stderr, "cannot create %s\n", configdir);
            exit(1);
        }
        free((void *)configdir);
        configdir = strdup_printf("%s/.config/mediafire-tools", homedir);
    } else {
        // $XDG_CONFIG_HOME/mediafire-tools
        if (mkdir(configdir, 0755) != 0 && errno != EEXIST) {
            perror("mkdir");
            fprintf(stderr, "cannot create %s\n", configdir);
            exit(1);
        }
        configdir = strdup_printf("%s/mediafire-tools", configdir);
    }
    /* EEXIST is okay, so only fail if it is something else */
    if (mkdir(configdir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
        fprintf(stderr, "cannot create %s\n", configdir);
        exit(1);
    }

    *configfile = strdup_printf("%s/config", configdir);
    /* test if the configuration file can be opened */
    fd = open(*configfile, O_RDONLY);
    if (fd < 0) {
        free(*configfile);
        *configfile = NULL;
    } else {
        close(fd);
    }

    free((void *)configdir);
}

int config_file_read(FILE *fp,int *argc, char ***argv)
{
    // read the config file line by line and pass each line to wordexp to
    // retain proper quoting
    char           *line = NULL;
    size_t          len = 0;
    ssize_t         read;
    wordexp_t       p;
    int             ret;
    size_t          i;
    int             item_count;
    int             new_items = 0;

    item_count = *argc;

    while ((read = getline(&line, &len, fp)) != -1) {

        // skip lines that are commented out
        if (line[0] == '#' || line[0] == ';')
            continue;

        // replace possible trailing newline by zero
        if (line[strlen(line) - 1] == '\n')
            line[strlen(line) - 1] = '\0';
        ret = wordexp(line, &p, WRDE_SHOWERR | WRDE_UNDEF);

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
            wordfree(&p);
            continue;
        }

        // items found
        if(p.we_wordc > 0)
        {
            new_items = p.we_wordc;

            // add to argc plus NULL terminating char
            item_count += p.we_wordc;

            // realloc after wordexp() expansion
            *argv = (char **)realloc(*argv, sizeof(char *) * (item_count + 1));

            // now insert those arguments into argv right after the first
            memmove((*argv) + p.we_wordc + 1, (*argv) + 1,
                sizeof(char *) * (*argc - 1));
            *argc += p.we_wordc;

            // copy wordexp() results into argv vectors
            for (i = 0; i < p.we_wordc; i++) {
                (*argv)[i + 1] = strdup(p.we_wordv[i]);
            }
        }
        wordfree(&p);
    }
    free(line);

    return new_items;
}
