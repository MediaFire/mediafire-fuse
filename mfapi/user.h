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

#ifndef __MFAPI_USER_H__
#define __MFAPI_USER_H__

typedef struct _mfuser_s mfuser_t;

mfuser_t       *user_alloc(void);

void            user_free(mfuser_t * user);

int             user_get_first_name(mfuser_t * user, char *buf, int buf_sz);

int             user_set_first_name(mfuser_t * user, const char *first_name);

int             user_get_last_name(mfuser_t * user, char *buf, int buf_sz);

int             user_set_last_name(mfuser_t * user, const char *last_name);

int             user_get_space_total(mfuser_t * user, char *buf, int buf_sz);

int             user_set_space_total(mfuser_t * user, const char *bytes_total);

int             user_get_space_used(mfuser_t * user, char *buf, int buf_sz);

int             user_set_space_used(mfuser_t * user, const char *bytes_used);

#endif
