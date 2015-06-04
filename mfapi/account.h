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

#ifndef __MFAPI_ACCOUNT_H__
#define __MFAPI_ACCOUNT_H__

#define ACCOUNT_FLAG_DIRTY_SIZE             ( 1U << 1 )
#define ACCOUNT_FLAG_ALL                    ( 0xFF )

#include <inttypes.h>

typedef struct  _account_s  account_t;

account_t*  account_alloc(void);

void        account_free(account_t *account);

int         account_set_username(account_t *account, const char *username);

int         account_get_username(account_t *account, char *buf, int buf_sz);

int         account_set_password(account_t *account, const char *password);

int         account_get_password(account_t *account, char *buf, int buf_sz);

int         account_set_ekey(account_t *account, const char *ekey);

int         account_get_ekey(account_t *account, char *buf, int buf_sz);

int         account_get_first_name(account_t * account, char *buf, int buf_sz);

int         account_set_first_name(account_t *account, const char *first_name);

int         account_get_last_name(account_t *account, char *buf, int buf_sz);

int         account_set_last_name(account_t *account, const char *last_name);

int         account_get_space_total(account_t *account, char *buf, int buf_sz);

int         account_set_space_total(account_t *account,
                                    const char *bytes_total);

int         account_get_space_used(account_t *account, char *buf, int buf_sz);

int         account_set_space_used(account_t *account, const char *bytes_used);

uint16_t    account_get_state_flags(account_t *account);

int         account_add_state_flags(account_t *account, uint16_t flags);

int         account_del_state_flags(account_t *account, uint16_t flags);

#endif
