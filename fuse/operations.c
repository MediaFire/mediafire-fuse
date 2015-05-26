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

#define _POSIX_C_SOURCE 200809L // for strdup and struct timespec
#define _XOPEN_SOURCE 700       // for S_IFDIR and S_IFREG (on linux,
                                // posix_c_source is enough but this is needed
                                // on freebsd)

#define FUSE_USE_VERSION 30

#include <fuse/fuse.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fuse/fuse_common.h>
#include <stdint.h>
#include <libgen.h>
#include <stdbool.h>
#include <time.h>
#include <openssl/sha.h>
#include <sys/statvfs.h>

#include "../mfapi/user.h"
#include "../mfapi/mfconn.h"
#include "../mfapi/apicalls.h"
#include "../utils/stringv.h"
#include "../utils/hash.h"
#include "hashtbl.h"
#include "operations.h"

/* what you can safely assume about requests to your filesystem
 *
 * from: http://sourceforge.net/p/fuse/wiki/FuseInvariants/
 *
 * There are a number of assumptions that one can safely make when
 * implementing a filesystem using fuse. This page should be completed with a
 * set of such assumptions.
 *
 *  - All requests are absolute, i.e. all paths begin with "/" and include the
 *    complete path to a file or a directory. Symlinks, "." and ".." are
 *    already resolved.
 *
 *  - For every request you can get except for "Getattr()", "Read()" and
 *    "Write()", usually for every path argument (both source and destination
 *    for link and rename, but only the source for symlink), you will get a
 *    "Getattr()" request just before the callback.
 *
 * For example, suppose I store file names of files in a filesystem also into a
 * database. To keep data in sync, I would like, for each filesystem operation
 * that succeeds, to check if the file exists on the database. I just do this in
 * the "Getattr()" call, since all other calls will be preceded by a getattr.
 *
 *  - The value of the "st_dev" attribute in the "Getattr()" call are ignored
 *    by fuse and an appropriate anomynous device number is inserted instead.
 *
 *  - The arguments for every request are already verified as much as
 *    possible. This means that, for example "readdir()" is only called with
 *    an existing directory name, "Readlink()" is only called with an existing
 *    symlink, "Symlink()" is only called if there isn't already another
 *    object with the requested linkname, "read()" and "Write()" are only
 *    called if the file has been opened with the correct flags.
 *
 *  - The VFS also takes care of avoiding race conditions:
 *
 *  - while "Unlink()" is running on a specific file, it cannot be interrupted
 *    by a "Chmod()", "Link()" or "Open()" call from a different thread on the
 *    same file.
 *
 *  - while "Rmdir()" is running, no files can be created in the directory
 *    that "Rmdir()" is acting on.
 *
 *  - If a request returns invalid values (e.g. in the structure returned by
 *    "Getattr()" or in the link target returned by "Symlink()") or if a
 *    request appears to have failed (e.g. if a "Create()" request succeds but
 *    a subsequent "Getattr()" (that fuse calls automatically) ndicates that
 *    no regular file has been created), the syscall returns EIO to the
 *    caller.
 */

struct mediafirefs_openfile {
    // to fread and fwrite from/to the file
    int             fd;
    char           *path;
    // whether or not a patch has to be uploaded when closing
    bool            is_readonly;
    // whether or not to do a new file upload when closing
    bool            is_local;
    // is true if file has been updated since last flush
    bool            is_flushed;
};

int mediafirefs_getattr(const char *path, struct stat *stbuf)
{
    printf("FUNCTION: getattr. path: %s\n", path);
    /*
     * since getattr is called before every other call (except for getattr,
     * read and write) wee only call folder_tree_update in the getattr call
     * and not the others
     */
    struct mediafirefs_context_private *ctx;
    int             retval;
    time_t          now;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    now = time(NULL);
    if (now - ctx->last_status_check > ctx->interval_status_check) {
        folder_tree_update(ctx->tree, ctx->conn, false);
        ctx->last_status_check = now;
    }

    retval = folder_tree_getattr(ctx->tree, ctx->conn, path, stbuf);

    if (retval != 0 && stringv_mem(ctx->sv_writefiles, path)) {
        stbuf->st_uid = geteuid();
        stbuf->st_gid = getegid();
        stbuf->st_ctime = 0;
        stbuf->st_mtime = 0;
        stbuf->st_mode = S_IFREG | 0666;
        stbuf->st_nlink = 1;
        stbuf->st_atime = 0;
        stbuf->st_size = 0;
        retval = 0;
    }

    pthread_mutex_unlock(&(ctx->mutex));

    return retval;
}

int mediafirefs_readdir(const char *path, void *buf, fuse_fill_dir_t filldir,
                        off_t offset, struct fuse_file_info *info)
{
    printf("FUNCTION: readdir. path: %s\n", path);
    (void)offset;
    (void)info;
    struct mediafirefs_context_private *ctx;
    int             retval;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));
    retval = folder_tree_readdir(ctx->tree, ctx->conn, path, buf, filldir);
    pthread_mutex_unlock(&(ctx->mutex));

    return retval;
}

void mediafirefs_destroy(void *user_ptr)
{
    printf("FUNCTION: destroy\n");
    FILE           *fd;
    struct mediafirefs_context_private *ctx;

    ctx = (struct mediafirefs_context_private *)user_ptr;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "storing hashtable\n");

    fd = fopen(ctx->dircache, "w+");

    if (fd == NULL) {
        fprintf(stderr, "cannot open %s for writing\n", ctx->dircache);
        pthread_mutex_unlock(&(ctx->mutex));
        return;
    }

    folder_tree_store(ctx->tree, fd);

    fclose(fd);

    folder_tree_destroy(ctx->tree);

    mfconn_destroy(ctx->conn);

    pthread_mutex_unlock(&(ctx->mutex));
}

int mediafirefs_mkdir(const char *path, mode_t mode)
{
    printf("FUNCTION: mkdir. path: %s\n", path);

    (void)mode;

    char           *dirname;
    int             retval;
    char           *basename;
    const char     *key;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    /* we don't need to check whether the path already existed because the
     * getattr call made before this one takes care of that
     */

    /* before calling the remote function we check locally */

    dirname = strdup(path);

    /* remove possible trailing slash */
    if (dirname[strlen(dirname) - 1] == '/') {
        dirname[strlen(dirname) - 1] = '\0';
    }

    /* split into dirname and basename */

    basename = strrchr(dirname, '/');
    if (basename == NULL) {
        fprintf(stderr, "cannot find slash\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOENT;
    }

    /* replace the slash by a terminating zero */
    basename[0] = '\0';

    /* basename points to the string after the last slash */
    basename++;

    /* check if the dirname is of zero length now. If yes, then the directory
     * is to be created in the root */
    if (dirname[0] == '\0') {
        key = NULL;
    } else {
        key = folder_tree_path_get_key(ctx->tree, ctx->conn, dirname);
    }

    retval = mfconn_api_folder_create(ctx->conn, key, basename);
    if (retval != 0) {
        fprintf(stderr, "mfconn_api_folder_create unsuccessful\n");
        pthread_mutex_unlock(&(ctx->mutex));
        // FIXME: find better errno in this case
        return -EAGAIN;
    }

    free(dirname);

    folder_tree_update(ctx->tree, ctx->conn, true);

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

int mediafirefs_rmdir(const char *path)
{
    printf("FUNCTION: rmdir. path: %s\n", path);
    const char     *key;
    int             retval;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    /* no need to check
     *  - if path is directory
     *  - if directory is empty
     *  - if directory is root
     *
     * because getattr was called before and already made sure
     */

    key = folder_tree_path_get_key(ctx->tree, ctx->conn, path);
    if (key == NULL) {
        fprintf(stderr, "key is NULL\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOENT;
    }

    retval = mfconn_api_folder_delete(ctx->conn, key);
    if (retval != 0) {
        fprintf(stderr, "mfconn_api_folder_create unsuccessful\n");
        pthread_mutex_unlock(&(ctx->mutex));
        // FIXME: find better errno in this case
        return -EAGAIN;
    }

    /* retrieve remote changes to not get out of sync */
    folder_tree_update(ctx->tree, ctx->conn, true);

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

int mediafirefs_unlink(const char *path)
{
    printf("FUNCTION: unlink. path: %s\n", path);
    const char     *key;
    int             retval;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    /* no need to check
     *  - if path is directory
     *  - if directory is empty
     *  - if directory is root
     *
     * because getattr was called before and already made sure
     */

    key = folder_tree_path_get_key(ctx->tree, ctx->conn, path);
    if (key == NULL) {
        fprintf(stderr, "key is NULL\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOENT;
    }

    retval = mfconn_api_file_delete(ctx->conn, key);
    if (retval != 0) {
        fprintf(stderr, "mfconn_api_file_create unsuccessful\n");
        pthread_mutex_unlock(&(ctx->mutex));
        // FIXME: find better errno in this case
        return -EAGAIN;
    }

    /* retrieve remote changes to not get out of sync */
    folder_tree_update(ctx->tree, ctx->conn, true);

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

/*
 * the following restrictions apply:
 *  1. a file can be opened in read-only mode more than once at a time
 *  2. a file can only be be opened in write-only or read-write mode
 *     more than once at a time.
 *  3. a file that is only local and has not been uploaded yet cannot be read
 *     from
 *  4. a file that has opened in any way will not be updated to its latest
 *     remote revision until all its opened handles are closed
 *
 *  Point 3 is enforced by the lookup in the hashtable failing.
 *
 *  Point 4 is enforced by checking if the current path is in the writefiles or
 *  readonlyfiles string vector and if yes, no updating will be done.
 */
int mediafirefs_open(const char *path, struct fuse_file_info *file_info)
{
    printf("FUNCTION: open. path: %s\n", path);
    int             fd;
    struct mediafirefs_openfile *openfile;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fd = folder_tree_open_file(ctx->tree, ctx->conn, path, file_info->flags,
                               true);
    if (fd < 0) {
        fprintf(stderr, "folder_tree_file_open unsuccessful\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return fd;
    }

    openfile = malloc(sizeof(struct mediafirefs_openfile));
    openfile->fd = fd;
    openfile->is_local = false;
    openfile->path = strdup(path);
    openfile->is_flushed = true;

    if ((file_info->flags & O_ACCMODE) == O_RDONLY) {
        openfile->is_readonly = true;
        // add to readonlyfiles
        stringv_add(ctx->sv_readonlyfiles, path);
    } else {
        openfile->is_readonly = false;
        // add to writefiles
        stringv_add(ctx->sv_writefiles, path);
    }

    file_info->fh = (uintptr_t) openfile;

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

/*
 * this is called if the file does not exist yet. It will create a temporary
 * file and open it.
 * once the file gets closed, it will be uploaded.
 */
int mediafirefs_create(const char *path, mode_t mode,
                       struct fuse_file_info *file_info)
{
    printf("FUNCTION: create. path: %s\n", path);

    (void)mode;

    int             fd;
    struct mediafirefs_openfile *openfile;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fd = folder_tree_tmp_open(ctx->tree);
    if (fd < 0) {
        fprintf(stderr, "folder_tree_tmp_open failed\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return -EACCES;
    }
    
    openfile = malloc(sizeof(struct mediafirefs_openfile));
    openfile->fd = fd;
    openfile->is_local = true;
    openfile->is_readonly = false;
    openfile->path = strdup(path);
    openfile->is_flushed = false;
    file_info->fh = (uintptr_t) openfile;

    // add to writefiles
    stringv_add(ctx->sv_writefiles, path);

    pthread_mutex_unlock(&(ctx->mutex));

    mediafirefs_flush(path, file_info);

    return 0;
}

int mediafirefs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *file_info)
{
    printf("FUNCTION: read. path: %s\n", path);

    (void)path;
    ssize_t         retval;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;
    pthread_mutex_lock(&(ctx->mutex));

    retval =
        pread(((struct mediafirefs_openfile *)(uintptr_t) file_info->fh)->fd,
              buf, size, offset);

    pthread_mutex_unlock(&(ctx->mutex));

    return retval;
}

int mediafirefs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *file_info)
{
    printf("FUNCTION: write. path: %s\n", path);

    (void)path;
    ssize_t         retval;
    struct mediafirefs_context_private *ctx;
    struct mediafirefs_openfile *openfile;

    ctx = fuse_get_context()->private_data;
    pthread_mutex_lock(&(ctx->mutex));

    openfile = (struct mediafirefs_openfile *)(uintptr_t) file_info->fh;

    retval = pwrite(openfile->fd, buf, size, offset);
    openfile->is_flushed = false;

    pthread_mutex_unlock(&(ctx->mutex));

    return retval;
}

/*
 * note: the return value of release() is ignored by fuse
 *
 */
int mediafirefs_release(const char *path, struct fuse_file_info *file_info)
{
    printf("FUNCTION: release. path: %s\n", path);

    (void)path;

    struct mediafirefs_context_private *ctx;
    struct mediafirefs_openfile *openfile;
    struct mfconn_upload_check_result check_result;

    /* filesystems should not assume that flush will ever be called.
     * since we do our uploading in flush, we must make sure flush is called*/
    mediafirefs_flush(path, file_info);

    ctx = fuse_get_context()->private_data;

    // zero out check result to prevent spurious results later
    memset(&check_result,0,sizeof(check_result));
    
    pthread_mutex_lock(&(ctx->mutex));

    openfile = (struct mediafirefs_openfile *)(uintptr_t) file_info->fh;

    // if file was opened as readonly then it just has to be closed
    if (openfile->is_readonly) {
        // remove this entry from readonlyfiles
        if (stringv_del(ctx->sv_readonlyfiles, openfile->path) != 0) {
            fprintf(stderr, "FATAL: readonly entry %s not found\n",
                    openfile->path);
            exit(1);
        }

        close(openfile->fd);
        free(openfile->path);
        free(openfile);
        pthread_mutex_unlock(&(ctx->mutex));
        return 0;
    }
    // if the file is not readonly, its entry in writefiles has to be removed
    if (stringv_del(ctx->sv_writefiles, openfile->path) != 0) {
        fprintf(stderr, "FATAL: writefiles entry %s not found\n",
                openfile->path);
        exit(1);
    }

    close(openfile->fd);

    free(openfile->path);
    free(openfile);

    folder_tree_update(ctx->tree, ctx->conn, true);

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

int mediafirefs_readlink(const char *path, char *buf, size_t bufsize)
{
    printf("FUNCTION: readlink. path: %s\n", path);

    (void)path;
    (void)buf;
    (void)bufsize;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "readlink not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_mknod(const char *path, mode_t mode, dev_t dev)
{
    printf("FUNCTION: mknod. path: %s\n", path);

    (void)path;
    (void)mode;
    (void)dev;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "mknod not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_symlink(const char *target, const char *linkpath)
{
    printf("FUNCTION: symlink. target: %s, linkpath: %s\n", target, linkpath);

    (void)target;
    (void)linkpath;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "symlink not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_rename(const char *oldpath, const char *newpath)
{
    printf("FUNCTION: rename. oldpath: %s, newpath %s\n", oldpath, newpath);

    char           *temp1;
    char           *temp2;
    char           *olddir;
    char           *newdir;
    char           *oldname;
    char           *newname;
    int             retval;
    struct mediafirefs_context_private *ctx;
    bool            is_file;
    const char     *key;
    const char     *folderkey;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    is_file = folder_tree_path_is_file(ctx->tree, ctx->conn, oldpath);

    key = folder_tree_path_get_key(ctx->tree, ctx->conn, oldpath);
    if (key == NULL) {
        fprintf(stderr, "key is NULL\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOENT;
    }
    // check if the directory changed
    temp1 = strdup(oldpath);
    temp2 = strdup(newpath);
    olddir = dirname(temp1);
    newdir = dirname(temp2);

    if (strcmp(olddir, newdir) != 0) {
        folderkey = folder_tree_path_get_key(ctx->tree, ctx->conn, newdir);
        if (key == NULL) {
            fprintf(stderr, "key is NULL\n");
            free(temp1);
            free(temp2);
            pthread_mutex_unlock(&(ctx->mutex));
            return -ENOENT;
        }

        if (is_file) {
            retval = mfconn_api_file_move(ctx->conn, key, folderkey);
        } else {
            retval = mfconn_api_folder_move(ctx->conn, key, folderkey);
        }
        if (retval != 0) {
            if (is_file) {
                fprintf(stderr, "mfconn_api_file_move failed\n");
            } else {
                fprintf(stderr, "mfconn_api_folder_move failed\n");
            }
            free(temp1);
            free(temp2);
            pthread_mutex_unlock(&(ctx->mutex));
            return -ENOENT;
        }
    }

    free(temp1);
    free(temp2);

    // check if the name changed
    temp1 = strdup(oldpath);
    temp2 = strdup(newpath);
    oldname = basename(temp1);
    newname = basename(temp2);

    if (strcmp(oldname, newname) != 0) {
        if (is_file) {
            retval = mfconn_api_file_update(ctx->conn, key, newname, NULL, false);
        } else {
            retval = mfconn_api_folder_update(ctx->conn, key, newname, NULL);
        }
        if (retval != 0) {
            if (is_file) {
                fprintf(stderr, "mfconn_api_file_update failed\n");
            } else {
                fprintf(stderr, "mfconn_api_folder_update failed\n");
            }
            free(temp1);
            free(temp2);
            pthread_mutex_unlock(&(ctx->mutex));
            return -ENOENT;
        }
    }

    free(temp1);
    free(temp2);

    folder_tree_update(ctx->tree, ctx->conn, true);

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

int mediafirefs_link(const char *target, const char *linkpath)
{
    printf("FUNCTION: link. target: %s, linkpath %s\n", target, linkpath);

    (void)target;
    (void)linkpath;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "link not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_chmod(const char *path, mode_t mode)
{
    printf("FUNCTION: chmod. path: %s\n", path);
    (void)path;
    (void)mode;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "chmod not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_chown(const char *path, uid_t uid, gid_t gid)
{
    printf("FUNCTION: chown. path: %s\n", path);

    (void)path;
    (void)uid;
    (void)gid;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "chown not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_truncate(const char *path, off_t length)
{
    printf("FUNCTION: truncate. path: %s, length: %zd\n", path, length);

    int             retval;

    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    if (length != 0) {
	fprintf(stderr, "Truncate is not defined for length other than 0\n");
	pthread_mutex_unlock(&(ctx->mutex));
	return -EINVAL;
    }

    retval = folder_tree_truncate_file(ctx->tree, ctx->conn, path);
    
    if (retval == -1) {
	pthread_mutex_unlock(&(ctx->mutex));
	return -ENOENT;
    }

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

int mediafirefs_statfs(const char *path, struct statvfs *buf)
{
    printf("FUNCTION: statfs. path: %s\n", path);

    // TODO:    add mfuser to ctx and store it accross the system.
    //          instantiating it every time on a per-instance is not ideal.

    mfuser_t       *user = NULL;

    char            space_total[128];
    char            space_used[128];

    uintmax_t       bytes_total = 0;
    uintmax_t       bytes_used = 0;
    uintmax_t       bytes_free = 0;

    (void)path;
    (void)buf;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    // init buffers before we enter critical region (primitive guard)
    memset(space_total, 0, sizeof(space_total));
    memset(space_used, 0, sizeof(space_used));

    pthread_mutex_lock(&(ctx->mutex));

    user = user_alloc();
    mfconn_api_user_get_info(ctx->conn, user);

    user_get_space_total(user, space_total, 128);
    user_get_space_used(user, space_used, 128);

    if (user != NULL)
        user_free(user);

    bytes_total = atoll(space_total);
    bytes_used = atoll(space_used);

    if (bytes_total == 0) {

        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOSYS;         // returning -ENOENT might make more sense
    }

    bytes_free = bytes_total - bytes_used;

    fprintf(stderr, "account size: %ju bytes\n", bytes_total);
    fprintf(stderr, "account used: %ju bytes\n", bytes_used);
    fprintf(stderr, "account free: %ju bytes\n", bytes_free);

    // there is no block size on the remote so we will fake it with a
    // block size of 64k.  FUSE requires that block size be a multiple
    // of 4096 bytes (4k)
    buf->f_bsize = 65536;
    buf->f_frsize = 65536;
    buf->f_blocks = (bytes_total / 65535);
    buf->f_bfree = (bytes_free / 65536);
    buf->f_bavail = (bytes_free / 65536);

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

int mediafirefs_flush(const char *path, struct fuse_file_info *file_info)
{
    printf("FUNCTION: flush. path: %s\n", path);

    FILE           *fh;
    char           *file_name;
    char           *dir_name;
    const char     *folder_key;
    char           *upload_key;
    char           *temp1;
    char           *temp2;
    int             retval;
    struct mediafirefs_context_private *ctx;
    struct mediafirefs_openfile *openfile;
    struct mfconn_upload_check_result check_result;
    unsigned char   bhash[SHA256_DIGEST_LENGTH];
    char           *hash;
    uint64_t        size;

    openfile = (struct mediafirefs_openfile *)(uintptr_t) file_info->fh;

    if (openfile->is_flushed) {
	return 0;
    }

    ctx = fuse_get_context()->private_data;

    // zero out check result to prevent spurious results later
    memset(&check_result,0,sizeof(check_result));
    
    pthread_mutex_lock(&(ctx->mutex));


    if (openfile->is_readonly) {
	/* nothing to do here */
	pthread_mutex_unlock(&(ctx->mutex));
	return 0;
    }
        // if the file only exists locally, an initial upload has to be done
    if (openfile->is_local) {
        // pass a copy because dirname and basename may modify their argument
        temp1 = strdup(openfile->path);
        file_name = basename(temp1);
        temp2 = strdup(openfile->path);
        dir_name = dirname(temp2);

        fh = fdopen(openfile->fd, "r");
        rewind(fh);

        folder_key = folder_tree_path_get_key(ctx->tree, ctx->conn, dir_name);

	size = -1;
        retval = calc_sha256(fh, bhash, &size);
        rewind(fh);

        if (retval != 0) {
            fprintf(stderr, "failed to calculate hash\n");
            free(temp1);
            free(temp2);
            pthread_mutex_unlock(&(ctx->mutex));
            return -EACCES;
        }

        hash = binary2hex(bhash, SHA256_DIGEST_LENGTH);

        retval = mfconn_api_upload_check(ctx->conn, file_name, hash, size,
                                         folder_key, &check_result);

        if (retval != 0) {
            free(temp1);
            free(temp2);
            free(hash);
            fprintf(stderr, "mfconn_api_upload_check failed\n");
            fprintf(stderr, "file_name: %s\n",file_name);
            fprintf(stderr, "hash: %s\n",hash);
            fprintf(stderr, "size: %jd\n",size);
            fprintf(stderr, "folder_key: %s\n",folder_key);
            pthread_mutex_unlock(&(ctx->mutex));
            return -EACCES;
        }

        if (check_result.hash_exists) {
            // hash exists, so use upload/instant

            retval = mfconn_api_upload_instant(ctx->conn, NULL,
                                               file_name, hash, size,
                                               folder_key);

            free(temp1);
            free(temp2);
            free(hash);

            if (retval != 0) {
                fprintf(stderr, "mfconn_api_upload_instant failed\n");
                pthread_mutex_unlock(&(ctx->mutex));
                return -EACCES;
            }
        } else {
            // hash does not exist, so do full upload
            upload_key = NULL;
            retval = mfconn_api_upload_simple(ctx->conn, folder_key,
                                              fh, file_name, false, &upload_key);

            free(temp1);
            free(temp2);
            free(hash);

            if (retval != 0 || upload_key == NULL) {

                fprintf(stderr, "mfconn_api_upload_simple failed\n");
                fprintf(stderr, "file_name: %s\n",file_name);
                fprintf(stderr, "hash: %s\n",hash);
                fprintf(stderr, "size: %jd\n",size);
                fprintf(stderr, "folder_key: %s\n",folder_key);

                pthread_mutex_unlock(&(ctx->mutex));
                return -EACCES;
            }
            // poll for completion
            retval = mfconn_upload_poll_for_completion(ctx->conn, upload_key);
            free(upload_key);

            if (retval != 0) {
                fprintf(stderr, "mfconn_upload_poll_for_completion failed\n");
                pthread_mutex_unlock(&(ctx->mutex));
                return -1;
            }
        }

        folder_tree_update(ctx->tree, ctx->conn, true);
	openfile->is_flushed = true;
        pthread_mutex_unlock(&(ctx->mutex));
        return 0;
    }

    // the file was not opened readonly and also existed on the remote
    // thus, we have to check whether any changes were made and if yes, upload
    // a patch

    retval = folder_tree_upload_patch(ctx->tree, ctx->conn, openfile->path);

    if (retval != 0) {
	    fprintf(stderr, "folder_tree_upload_patch failed\n");
	    pthread_mutex_unlock(&(ctx->mutex));
	    return -EACCES;
    }


    folder_tree_update(ctx->tree, ctx->conn, true);

    openfile->is_flushed = true;
    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

int mediafirefs_fsync(const char *path, int datasync,
                      struct fuse_file_info *file_info)
{
    printf("FUNCTION: fsync. path: %s\n", path);

    (void)path;
    (void)datasync;
    (void)file_info;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "fsync not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_setxattr(const char *path, const char *name,
                         const char *value, size_t size, int flags)
{
    printf("FUNCTION: setxattr. path: %s\n", path);

    (void)path;
    (void)name;
    (void)value;
    (void)size;
    (void)flags;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "setxattr not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_getxattr(const char *path, const char *name, char *value,
                         size_t size)
{
    printf("FUNCTION: getxattr. path: %s\n", path);

    (void)path;
    (void)name;
    (void)value;
    (void)size;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "getxattr not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_listxattr(const char *path, char *list, size_t size)
{
    printf("FUNCTION: listxattr. path: %s\n", path);

    (void)path;
    (void)list;
    (void)size;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "listxattr not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_removexattr(const char *path, const char *list)
{
    printf("FUNCTION: removexattr. path: %s\n", path);

    (void)path;
    (void)list;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "removexattr not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

int mediafirefs_opendir(const char *path, struct fuse_file_info *file_info)
{
    printf("FUNCTION: opendir. path: %s\n", path);

    (void)path;
    (void)file_info;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "opendir is a no-op\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

int mediafirefs_releasedir(const char *path, struct fuse_file_info *file_info)
{
    printf("FUNCTION: releasedir. path: %s\n", path);

    (void)path;
    (void)file_info;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "releasedir is a no-op\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

int mediafirefs_fsyncdir(const char *path, int datasync,
                         struct fuse_file_info *file_info)
{
    printf("FUNCTION: fsyncdir. path: %s\n", path);

    (void)path;
    (void)datasync;
    (void)file_info;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "fsyncdir not implemented\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return -ENOSYS;
}

/*
void* mediafirefs_init(struct fuse_conn_info *conn)
{
    (void)conn;
    fprintf(stderr, "init is a no-op");
}
*/

int mediafirefs_access(const char *path, int mode)
{
    printf("FUNCTION: access. path: %s\n", path);

    (void)path;
    (void)mode;
    struct mediafirefs_context_private *ctx;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    fprintf(stderr, "access is a no-op\n");

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}

int mediafirefs_utimens(const char *path, const struct timespec tv[2])
{
    printf("FUNCTION: utimens. path: %s\n", path);

    time_t          since_epoch;
    struct tm       local_time;
    static int      b_tzset = 0;        // has tzset() been called
    char            print_time[24];     // buf for print friendly local time
    bool            is_file = 0;
    const char     *key = NULL;
    int             retval;

    struct mediafirefs_context_private *ctx;

    (void)path;

    ctx = fuse_get_context()->private_data;

    pthread_mutex_lock(&(ctx->mutex));

    is_file = folder_tree_path_is_file(ctx->tree, ctx->conn, path);

    // look up the key
    key = folder_tree_path_get_key(ctx->tree, ctx->conn, path);
    if (key == NULL) {
        fprintf(stderr, "key is NULL\n");
        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOENT;
    }
    // call tzset if needed
    if (b_tzset != 1) {
        tzset();
        b_tzset = 1;
    }
    // we only care about mtime (not atime) and seconds because the
    // could doesn't handle nanoseconds
    memcpy(&since_epoch, &tv[1].tv_sec, sizeof(time_t));

    if (localtime_r((const time_t *)&since_epoch, &local_time) == NULL) {
        fprintf(stderr, "utimens not implemented\n");
        pthread_mutex_unlock(&(ctx->mutex));

        return -ENOSYS;
    }

    memset(print_time, 0, sizeof(print_time));
    strftime(print_time, sizeof(print_time) - 1, "%F %T", &local_time);

    fprintf(stderr, "utimens file set: %s\n", print_time);

    if (is_file) {
        retval = mfconn_api_file_update(ctx->conn, key, NULL, print_time, false);
    } else {
        retval = mfconn_api_folder_update(ctx->conn, key, NULL, print_time);
    }

    if (retval == -1) {
        pthread_mutex_unlock(&(ctx->mutex));
        return -ENOENT;
    }

    pthread_mutex_unlock(&(ctx->mutex));

    return 0;
}
