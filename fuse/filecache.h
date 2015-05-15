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

#ifndef __FUSE_FILECACHE_H__
#define __FUSE_FILECACHE_H__

int             filecache_open_file(const char *quickkey,
                                    uint64_t local_revision,
                                    uint64_t remote_revision, uint64_t fsize,
                                    const unsigned char *fhash,
                                    const char *filecache, mfconn * conn,
                                    mode_t mode, bool update);

int             filecache_truncate_file(const char *quickkey, const char *key,
                                    uint64_t local_revision,
                                    uint64_t remote_revision,
                                    const char *filecache_path, mfconn * conn);

int             filecache_get_new_and_old_sizes(const char *quickkey,
						uint64_t local_revision,
						const char *filecache_path,
						off_t *newsize,
						off_t *oldsize);

int             filecache_upload_patch(const char *quickkey,
                                       uint64_t local_revision,
                                       const char *filecache, mfconn * conn);

#endif
