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

#ifndef __FUSE_OPERATIONS_H__
#define __FUSE_OPERATIONS_H__

#include <fuse/fuse.h>
#include <stddef.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>

#include "../mfapi/mfconn.h"
#include "hashtbl.h"
#include "../utils/stringv.h"

struct fuse_conn_info;
struct fuse_file_info;
struct stat;
struct statvfs;

struct mediafirefs_context_private {
    mfconn         *conn;
    folder_tree    *tree;
    time_t          last_status_check;
    time_t          interval_status_check;
    pthread_mutex_t mutex;
    char           *configfile;
    char           *dircache;
    char           *filecache;
    /* stores:
     *  - all currently open temporary files which are to be uploaded when
     *    they are closed.
     *  - all files that are opened for writing
     */
    stringv        *sv_writefiles;
    /* stores all files that have been opened for reading only */
    stringv        *sv_readonlyfiles;
};

int             mediafirefs_getattr(const char *path, struct stat *stbuf);
int             mediafirefs_readlink(const char *path, char *buf,
                                     size_t bufsize);
int             mediafirefs_mknod(const char *path, mode_t mode, dev_t dev);
int             mediafirefs_mkdir(const char *path, mode_t mode);
int             mediafirefs_unlink(const char *path);
int             mediafirefs_rmdir(const char *path);
int             mediafirefs_symlink(const char *target, const char *linkpath);
int             mediafirefs_rename(const char *oldpath, const char *newpath);
int             mediafirefs_link(const char *target, const char *linkpath);
int             mediafirefs_chmod(const char *path, mode_t mode);
int             mediafirefs_chown(const char *path, uid_t uid, gid_t gid);
int             mediafirefs_truncate(const char *path, off_t length);
int             mediafirefs_open(const char *path,
                                 struct fuse_file_info *file_info);
int             mediafirefs_read(const char *path, char *buf, size_t size,
                                 off_t offset,
                                 struct fuse_file_info *file_info);
int             mediafirefs_write(const char *path, const char *buf,
                                  size_t size, off_t offset,
                                  struct fuse_file_info *file_info);
int             mediafirefs_statfs(const char *path, struct statvfs *buf);
int             mediafirefs_flush(const char *path,
                                  struct fuse_file_info *file_info);
int             mediafirefs_release(const char *path,
                                    struct fuse_file_info *file_info);
int             mediafirefs_fsync(const char *path, int datasync,
                                  struct fuse_file_info *file_info);
int             mediafirefs_setxattr(const char *path, const char *name,
                                     const char *value, size_t size,
                                     int flags);
int             mediafirefs_getxattr(const char *path, const char *name,
                                     char *value, size_t size);
int             mediafirefs_listxattr(const char *path, char *list,
                                      size_t size);
int             mediafirefs_removexattr(const char *path, const char *list);
int             mediafirefs_opendir(const char *path,
                                    struct fuse_file_info *file_info);
int             mediafirefs_readdir(const char *path, void *buf,
                                    fuse_fill_dir_t filldir, off_t offset,
                                    struct fuse_file_info *info);
int             mediafirefs_releasedir(const char *path,
                                       struct fuse_file_info *file_info);
int             mediafirefs_fsyncdir(const char *path,
                                     int datasync,
                                     struct fuse_file_info *file_info);
void           *mediafirefs_init(struct fuse_conn_info *conn);
void            mediafirefs_destroy(void *user_ptr);
int             mediafirefs_access(const char *path, int mode);
int             mediafirefs_create(const char *path, mode_t mode,
                                   struct fuse_file_info *file_info);
int             mediafirefs_utimens(const char *path,
                                    const struct timespec tv[2]);

#endif
