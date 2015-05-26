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

#ifndef _MFFUSE_HASHTBL_H_
#define _MFFUSE_HASHTBL_H_

#include <fuse/fuse.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "../mfapi/mfconn.h"

typedef struct folder_tree folder_tree;

folder_tree    *folder_tree_create(const char *filecache);

void            folder_tree_destroy(folder_tree * tree);

int             folder_tree_rebuild(folder_tree * tree, mfconn * conn);

void            folder_tree_housekeep(folder_tree * tree, mfconn * conn);

void            folder_tree_debug(folder_tree * tree);

int             folder_tree_getattr(folder_tree * tree, mfconn * conn,
                                    const char *path, struct stat *stbuf);

int             folder_tree_readdir(folder_tree * tree, mfconn * conn,
                                    const char *path, void *buf,
                                    fuse_fill_dir_t filldir);

void            folder_tree_update(folder_tree * tree, mfconn * conn,
                                   bool expect_changes);

int             folder_tree_store(folder_tree * tree, FILE * stream);

folder_tree    *folder_tree_load(FILE * stream, const char *filecache);

void            folder_tree_cleanup_filecache(folder_tree * tree,
                                              uint64_t allowed_size);

bool            folder_tree_path_exists(folder_tree * tree, mfconn * conn,
                                        const char *path);

uint64_t        folder_tree_path_get_num_children(folder_tree * tree,
                                                  mfconn * conn,
                                                  const char *path);

bool            folder_tree_path_is_directory(folder_tree * tree,
                                              mfconn * conn, const char *path);

const char     *folder_tree_path_get_key(folder_tree * tree, mfconn * conn,
                                         const char *path);

bool            folder_tree_path_is_root(folder_tree * tree, mfconn * conn,
                                         const char *path);

bool            folder_tree_path_is_file(folder_tree * tree, mfconn * conn,
                                         const char *path);

int             folder_tree_open_file(folder_tree * tree, mfconn * conn,
                                      const char *path, mode_t mode,
                                      bool update);
int             folder_tree_truncate_file(folder_tree * tree, mfconn * conn,
					  const char *path);
int             folder_tree_tmp_open(folder_tree * tree);

int             folder_tree_upload_patch(folder_tree * tree, mfconn * conn,
                                         const char *path);

#endif
