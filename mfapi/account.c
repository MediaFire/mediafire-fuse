/* Copyright (C) 2013 Bryan Christ <bryan.christ@mediafire.com>
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
#define _DEFAULT_SOURCE         // for strdup on old systems

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <openssl/sha.h>

#include "account.h"
//#include "apicalls.h"

struct _account_s
{
    char            *username;
    char            *password;
    char            *ekey;

    char            *first_name;
    char            *last_name;

    // a uintmax_t would probably be more efficient for these, but to make
    // conversion easier accross platforms, and to deal with the nuances
    // of libjannson, the values will be stored a string and converted later.
    char            *space_total;
    char            *space_used;

    unsigned int    state_flags;
};

account_t*
account_alloc(void)
{
    account_t   *account;

    account = (account_t *)calloc(1, sizeof(account_t));

    return account;
}

void
account_free(account_t *account)
{
    if(account == NULL)
    {
        fprintf(stderr, "user cannot be NULL\n");
        return;
    }

    if(account->username != NULL)
        free(account->username);

    if(account->password != NULL)
        free(account->password);

    if(account->ekey != NULL)
        free(account->ekey);

    if(account->first_name != NULL)
        free(account->first_name);

    if(account->last_name != NULL)
        free(account->last_name);

    if(account->space_total != NULL)
        free(account->space_total);

    if(account->space_used != NULL)
        free(account->space_used);

    return;
}

int
account_set_username(account_t *account, const char *username)
{
    if (account == NULL || username == NULL)
        return -1;

    if (account->username != NULL) {

        free(account->username);
        account->username = NULL;
    }

    account->username = strdup(username);

    return 0;
}

int
account_get_username(account_t *account, char *buf, int buf_sz)
{
    if(account == NULL || buf == NULL)
        return -1;

    // username cannot be shorter than 3 bytes even on legacy accounts
    if(buf_sz < 3)
        return -1;

    if(account->username == NULL)
        return 0;

    memset(buf, 0, buf_sz);

    strncpy(buf, account->username, buf_sz - 1);

    return strlen(buf);
}

int
account_set_password(account_t *account, const char *password)
{
    if (account == NULL || password == NULL)
        return -1;

    if (account->password != NULL)
    {
        free(account->password);
        account->password = NULL;
    }

    account->password = strdup(password);

    return 0;
}

int
account_get_password(account_t *account, char *buf, int buf_sz)
{
    if(account == NULL || buf == NULL)
        return -1;

    // password cannot be shorter than 5 bytes even on legacy accounts
    if(buf_sz < 5)
        return -1;

    if(account->password == NULL)
        return 0;

    memset(buf, 0, buf_sz);

    strncpy(buf, account->password, buf_sz - 1);

    return strlen(buf);
}

int
account_set_ekey(account_t *account, const char *ekey)
{
    if(account == NULL || ekey == NULL)
        return -1;

    if(account->ekey != NULL)
    {
        free(account->ekey);
        account->ekey = NULL;
    }

    account->ekey = strdup(ekey);

    return 0;
}

int
account_get_ekey(account_t *account, char *buf, int buf_sz)
{
    if(account == NULL || buf == NULL)
        return -1;

    if(account->ekey == NULL)
        return 0;

    memset(buf, 0, buf_sz);

    strncpy(buf, account->ekey, buf_sz - 1);

    return strlen(buf);
}


int
account_set_first_name(account_t *account, const char *first_name)
{
    if(account == NULL || first_name == NULL)
        return -1;

    if(account->first_name != NULL)
    {
        free(account->first_name);
        account->first_name = NULL;
    }

    account->first_name = strdup(first_name);

    return 0;
}

int
account_get_first_name(account_t *account, char *buf, int buf_sz)
{
    if(account == NULL || buf == NULL)
        return -1;

    if(buf_sz < 2)
        return -1;

    if(account->first_name == NULL)
        return 0;

    memset(buf, 0, buf_sz);

    strncpy(buf, account->first_name, buf_sz - 1);

    return strlen(buf);
}

int
account_set_last_name(account_t *account, const char *last_name)
{
    if(account == NULL || last_name == NULL)
        return -1;

    if(account->last_name != NULL)
    {
        free(account->last_name);
        account->last_name = NULL;
    }

    account->last_name = strdup(last_name);

    return 0;
}

int
account_get_last_name(account_t *account, char *buf, int buf_sz)
{
    if(account == NULL || buf == NULL)
        return -1;

    if(buf_sz < 2)
        return -1;

    if(account->last_name == NULL)
        return 0;

    memset(buf, 0, buf_sz);

    strncpy(buf, account->last_name, buf_sz - 1);

    return strlen(buf);
}

int
account_get_space_total(account_t *account, char *buf, int buf_sz)
{
    if(account == NULL || buf == NULL)
        return -1;

    if(buf_sz < 2)
        return -1;

    if(account->space_total == NULL)
        return 0;

    memset(buf, 0, buf_sz);

    strncpy(buf, account->space_total, buf_sz - 1);

    return strlen(buf);
}

int
account_set_space_total(account_t *account, const char *bytes_total)
{
    if(account == NULL || bytes_total == NULL)
        return -1;

    if(account->space_total != NULL)
    {
        free(account->space_total);
        account->space_total = NULL;
    }

    account->space_total = strdup(bytes_total);

    return 0;
}

int
account_get_space_used(account_t *account, char *buf, int buf_sz)
{
    if(account == NULL || buf == NULL)
        return -1;

    if(buf_sz < 2)
        return -1;

    if(account->space_used == NULL)
        return 0;

    memset(buf, 0, buf_sz);

    strncpy(buf, account->space_used, buf_sz - 1);

    return strlen(buf);
}

int
account_set_space_used(account_t *account, const char *bytes_used)
{
    if(account == NULL || bytes_used == NULL)
        return -1;

    if(account->space_used != NULL)
    {
        free(account->space_used);
        account->space_used = NULL;
    }

    account->space_used = strdup(bytes_used);

    return 0;
}

uint16_t
account_get_state_flags(account_t *account)
{
    if(account == NULL) return 0;

    return account->state_flags;
}

int
account_add_state_flags(account_t *account, uint16_t flags)
{
    if(account == NULL) return -1;

    account->state_flags |= flags;

    return 0;
}

int
account_del_state_flags(account_t *account, uint16_t flags)
{
    if(account == NULL) return -1;

    account->state_flags &= ~(flags);

    return 0;
}
