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

#ifndef _MFSHELL_HTTP_H_
#define _MFSHELL_HTTP_H_

#include <jansson.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <curl/curl.h>

#define HTTP_CONN_LAZY_SSL  (1U << 0)

typedef struct mfhttp       mfhttp;

typedef int (*DataHandler)  (mfhttp * conn,void * data);


mfhttp         *http_create(void);

void            http_destroy(mfhttp * conn);

void            http_set_connect_flags(mfhttp * conn,unsigned int flags);

void            http_set_data_handler(mfhttp * conn,
                                        DataHandler data_handler,
                                        void * cb_data);

int             http_get_buf(mfhttp * conn, const char *url);

int             http_post_buf(mfhttp * conn, const char *url,
                              const char *post_args);

int             http_get_file(mfhttp * conn, const char *url,
                              const char *path);

json_t         *http_parse_buf_json(mfhttp * conn, size_t flags,
                                    json_error_t * error);

int             http_post_file(mfhttp * conn, const char *url, FILE * fh,
                               struct curl_slist **custom_headers,
                               uint64_t filesize);

char           *urlencode(const char *input);

#endif
