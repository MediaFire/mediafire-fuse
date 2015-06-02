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

#include <stdio.h>

#include "../../mfapi/apicalls.h"
#include "../../mfapi/account.h"
#include "../mfshell.h"
#include "../commands.h"        // IWYU pragma: keep

int mfshell_cmd_whoami(mfshell * mfshell, int argc, char *const argv[])
{
    account_t       *account;

    (void)argv;
    int             retval;

    if (mfshell == NULL) {
        fprintf(stderr, "mfshell is NULL\n");
        return -1;
    }

    if (mfshell->conn == NULL) {
        fprintf(stderr, "conn is NULL\n");
        return -1;
    }

    if (argc != 1) {
        fprintf(stderr, "Invalid number of arguments\n");
        return -1;
    }

    account = account_alloc();

    retval = mfconn_api_user_get_info(mfshell->conn, account);

    if (account != NULL)
        account_free(account);

    return retval;
}
