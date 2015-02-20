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

#include <unistd.h>
#include <signal.h>

#ifdef __linux__
#include <sys/ioctl.h>
#endif

#include "mfshell.h"

sig_atomic_t     dirty_term_flag;

void terminal_rectify(mfshell * shell)
{
    extern sig_atomic_t     dirty_term_flag;

#ifdef TIOCGWINSZ

    if(dirty_term_flag == 1) {

        ioctl(STDOUT_FILENO, TIOCGWINSZ, &shell->terminal_sz);

        // clear the dirty terminal flag
        dirty_term_flag = 0;
    }

#else

    (void)shell;

#endif

    return;
}



