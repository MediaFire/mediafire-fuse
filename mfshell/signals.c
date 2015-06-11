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

#include <signal.h>
// #include <stdio.h>

void signal_sigwinch(int signo)
{
    extern sig_atomic_t dirty_term_flag;

    // this should never ever be true
    if(signo != SIGWINCH) return;

    dirty_term_flag = 1;

    // fprintf() is not signal safe.  this should always be comment out
    // when not debugging.
    // fprintf(stderr,"term resize detected\n");
}



