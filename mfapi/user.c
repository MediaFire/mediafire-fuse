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

#define _POSIX_C_SOURCE 200809L // for strdup
#define _DEFAULT_SOURCE         // for strdup on old systems

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>

#include "user.h"
#include "apicalls.h"

struct _mfuser_s {
    char            *first_name;
    char            *last_name;

    // a uintmax_t would probably be more efficient for these, but to make
    // conversion easier accross platforms, and to deal with the nuances
    // of libjannson, the values will be stored a string and converted later.
    char            *space_total;
    char            *space_used;
};

mfuser_t* user_alloc(void)
{
    mfuser_t    *user;

    user = (mfuser_t *) calloc(1, sizeof(mfuser_t));

    return user;
}

void user_free(mfuser_t *user)
{
    if (user == NULL) {
        fprintf(stderr, "user cannot be NULL\n");
        return;
    }

    if(user->first_name != NULL)
        free(user->first_name);

    if(user->last_name != NULL)
        free(user->last_name);

    if(user->space_total != NULL)
        free(user->space_total);

    if(user->space_used != NULL)
        free(user->space_used);

    return;
}

int user_set_first_name(mfuser_t *user, const char *first_name)
{
    if(user == NULL || first_name == NULL) return -1;

    if(user->first_name != NULL) {
 
       free(user->first_name);
        user->first_name = NULL;
    }

    user->first_name = strdup(first_name);

    return 0;
}

int user_get_first_name(mfuser_t *user, char *buf, int buf_sz)
{
    if(user == NULL || buf == NULL) return -1;

    if(buf_sz < 2) return -1;

    if(user->first_name == NULL) return 0;

    memset(buf,0,buf_sz);

    strncpy(buf,user->first_name,buf_sz - 1);

    return strlen(buf);
}

int user_set_last_name(mfuser_t *user, const char *last_name)
{
    if(user == NULL || last_name == NULL) return -1;

    if(user->last_name != NULL) {

        free(user->last_name);
        user->last_name = NULL;
    }

    user->last_name = strdup(last_name);

    return 0;
}

int user_get_last_name(mfuser_t *user, char *buf, int buf_sz)
{
    if(user == NULL || buf == NULL) return -1;

    if(buf_sz < 2) return -1;

    if(user->last_name == NULL) return 0;

    memset(buf,0,buf_sz);

    strncpy(buf,user->last_name,buf_sz - 1);

    return strlen(buf);
}

int user_get_space_total(mfuser_t *user, char *buf, int buf_sz)
{
    if(user == NULL || buf == NULL) return -1;

    if(buf_sz < 2) return -1;

    if(user->space_total == NULL) return 0;

    memset(buf,0,buf_sz);

    strncpy(buf,user->space_total,buf_sz - 1);

    return strlen(buf);
}

int user_set_space_total(mfuser_t *user, const char *bytes_total)
{
    if(user == NULL || bytes_total == NULL) return -1;

    if(user->space_total != NULL) {

        free(user->space_total);
        user->space_total = NULL;
    }

    user->space_total = strdup(bytes_total);

    return 0;
}

int user_get_space_used(mfuser_t *user, char *buf, int buf_sz)
{
    if(user == NULL || buf == NULL) return -1;

    if(buf_sz < 2) return -1;

    if(user->space_used == NULL) return 0;

    memset(buf,0,buf_sz);

    strncpy(buf,user->space_used,buf_sz - 1);

    return strlen(buf);
}

int user_set_space_used(mfuser_t *user, const char *bytes_used)
{
    if(user == NULL || bytes_used == NULL) return -1;

    if(user->space_used != NULL) {

        free(user->space_used);
        user->space_used = NULL;
    }

    user->space_used = strdup(bytes_used);

    return 0;
}
