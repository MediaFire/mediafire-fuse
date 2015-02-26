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

#define _POSIX_C_SOURCE 200809L // for getline
#define _GNU_SOURCE             // for getline on old systems

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#include "strings.h"
#include "stringv.h"

char*
strdup_printf(char *fmt, ...)
{
    // Good for glibc 2.1 and above. Fedora5 is 2.4.

    char           *ret_str = NULL;
    va_list         ap;
    int             bytes_to_allocate;

    va_start(ap, fmt);
    bytes_to_allocate = vsnprintf(ret_str, 0, fmt, ap);
    va_end(ap);

    // Add one for '\0'
    bytes_to_allocate++;

    ret_str = (char *)malloc(bytes_to_allocate * sizeof(char));
    if (ret_str == NULL) {
        fprintf(stderr, "failed to allocate memory\n");
        return NULL;
    }

    va_start(ap, fmt);
    bytes_to_allocate = vsnprintf(ret_str, bytes_to_allocate, fmt, ap);
    va_end(ap);

    return ret_str;
}

char*
string_line_from_stdin(bool hide)
{
    char           *line = NULL;
    size_t          len;
    ssize_t         bytes_read;
    struct termios  old,
                    new;

    if (hide) {
        if (tcgetattr(STDIN_FILENO, &old) != 0)
            return NULL;
        new = old;
        new.c_lflag &= ~ECHO;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new) != 0)
            return NULL;
    }

    bytes_read = getline(&line, &len, stdin);

    if (hide) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
    }

    if (bytes_read < 3) {
        if (line != NULL) {
            free(line);
            line = NULL;
        }
    }

    if (line[strlen(line) - 1] == '\n')
        line[strlen(line) - 1] = '\0';

    return line;
}


void
string_array_free(char **array)
{
    char    **rewind = NULL;

    if(array == NULL) return;

    rewind = array;

    while(*array != NULL)
    {
        // fprintf(stderr,"free\n");
        free(*array);
        array++;
    }

    free(rewind);

    return;
}
