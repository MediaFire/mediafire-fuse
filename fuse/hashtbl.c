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

#define _POSIX_C_SOURCE 200809L // for strdup and struct timespec (in fuse.h)
#define _XOPEN_SOURCE 700       // for S_IFDIR and S_IFREG (on linux,
                                // posix_c_source is enough but this is needed
                                // on freebsd)

#define FUSE_USE_VERSION 30

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fuse/fuse.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <openssl/sha.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <libgen.h>

#include "hashtbl.h"
#include "filecache.h"
#include "../mfapi/mfconn.h"
#include "../mfapi/file.h"
#include "../mfapi/folder.h"
#include "../mfapi/apicalls.h"
#include "../utils/strings.h"
#include "../utils/hash.h"

/*
 * we build a hashtable using the first three characters of the file or folder
 * key. Since the folder key is encoded in base 36 (10 digits and 26 letters),
 * this means that the resulting hashtable has to have 36^3=46656 buckets.
 */
#define NUM_BUCKETS 46656

struct h_entry {
    /*
     * keys are either 13 (folders) or 15 (files) long since the structure
     * members are most likely 8-byte aligned anyways, it does not make sense
     * to differentiate between them */
    char            key[MFAPI_MAX_LEN_KEY + 1];
    /* a filename is maximum 255 characters long */
    char            name[MFAPI_MAX_LEN_NAME + 1];
    /* we do not add the parent here because it is not used anywhere */
    /* char parent[20]; */
    /* local revision */
    uint64_t        remote_revision;
    /* the revision of the local version. For folders, this is the last
     * revision for which the folder contents have been retrieved. For files
     * this is the last revision for which the file contents have been
     * retrieved */
    uint64_t        local_revision;
    /* creation time */
    uint64_t        ctime;
    /* the containing folder */
    union {
        /* during runtime this is a pointer to the containing h_entry struct */
        struct h_entry *entry;
        /* when storing on disk, this is the offset of the stored h_entry
         * struct */
        uint64_t        offs;
    } parent;

    /********************
     * only for folders *
     ********************/
    /* number of children (number of files plus number of folders). Set to
     * zero when storing on disk */
    uint64_t        num_children;
    /*
     * Array of pointers to its children. Set to zero when storing on disk.
     *
     * This member could also be an array of keys which would not require
     * lookups on updating but we expect more reads than writes so we
     * sacrifice slower updates for faster lookups */
    struct h_entry **children;

    /******************
     * only for files *
     ******************/
    /* SHA256 is 256 bits = 32 bytes */
    unsigned char   hash[SHA256_DIGEST_LENGTH];
    /*
     * last access time to remove old locally cached files
     * atime is also never zero for files
     * a file that has never been accessed has an atime of 1 */
    uint64_t        atime;
    /* file size */
    uint64_t        fsize;
};

/*
 * Each bucket is an array of pointers instead of an array of h_entry structs
 * so that the array can be changed without the memory location of the h_entry
 * struct changing because the children of each h_entry struct point to those
 * locations
 *
 * This also allows us to implement each bucket as a sorted list in the
 * future. Queries could then be done using bisection (O(log(n))) instead of
 * having to traverse all elements in the bucket (O(n)). But with 46656
 * buckets this should not make a performance difference often.
 */

struct folder_tree {
    uint64_t        revision;
    char           *filecache;
    uint64_t        bucket_lens[NUM_BUCKETS];
    struct h_entry **buckets[NUM_BUCKETS];
    struct h_entry  root;
};

/* static functions local to this file */

/* functions without remote access */
static void     folder_tree_free_entries(folder_tree * tree);
static struct h_entry *folder_tree_lookup_key(folder_tree * tree,
                                              const char *key);
static bool     folder_tree_is_root(struct h_entry *entry);
static struct h_entry *folder_tree_allocate_entry(folder_tree * tree,
                                                  const char *key,
                                                  struct h_entry *new_parent);
static struct h_entry *folder_tree_add_file(folder_tree * tree, mffile * file,
                                            struct h_entry *new_parent);
static struct h_entry *folder_tree_add_folder(folder_tree * tree,
                                              mffolder * folder,
                                              struct h_entry *new_parent);
static void     folder_tree_remove(folder_tree * tree, const char *key);
static bool     folder_tree_is_parent_of(struct h_entry *parent,
                                         struct h_entry *child);
static bool     is_valid_cache_filename(const char *name, char key[],
                                        uint64_t * revision);
static int      atime_compare(const void *a, const void *b);

/* functions with remote access */
static struct h_entry *folder_tree_lookup_path(folder_tree * tree,
                                               mfconn * conn,
                                               const char *path);
static int      folder_tree_rebuild_helper(folder_tree * tree, mfconn * conn,
                                           struct h_entry *curr_entry);
static int      folder_tree_update_file_info(folder_tree * tree, mfconn * conn,
                                             const char *key);
static int      folder_tree_update_folder_info(folder_tree * tree,
                                               mfconn * conn, const char *key);

/* persistant storage file layout:
 *
 * byte 0: 0x4D -> ASCII M
 * byte 1: 0x46 -> ASCII F
 * byte 2: 0x53 -> ASCII S  --> MFS == MediaFire Storage
 * byte 3: 0x00 -> version information
 * bytes 4-11   -> last seen device revision
 * bytes 12-19  -> number of h_entry structs including root (num_hts)
 * bytes 20...  -> h_entry structs, the first one being root
 *
 * the children pointer member of the h_entry struct is useless when stored,
 * should be set to zero and not used when reading the file
 */

int folder_tree_store(folder_tree * tree, FILE * stream)
{

    /* to allow a quick mapping from keys to their offsets in the array that
     * will be stored, we create a hashtable of the same structure as the
     * folder_tree but instead of storing pointers to h_entries in the buckets
     * we store their integer offset. This way, when one known in which bucket
     * and in which position in a bucket a h_entry struct is, one can retrieve
     * the associated integer offset. */

    uint64_t      **integer_buckets;
    uint64_t        i,
                    j,
                    k,
                    num_hts;
    size_t          ret;
    struct h_entry *tmp_parent;
    int             bucket_id;
    bool            found;

    integer_buckets = (uint64_t **) malloc(NUM_BUCKETS * sizeof(uint64_t *));
    if (integer_buckets == NULL) {
        fprintf(stderr, "cannot malloc");
        return -1;
    }

    /* start counting with one because the root is also stored */
    num_hts = 1;
    for (i = 0; i < NUM_BUCKETS; i++) {
        if (tree->bucket_lens[i] == 0)
            continue;

        integer_buckets[i] =
            (uint64_t *) malloc(tree->bucket_lens[i] * sizeof(uint64_t));
        for (j = 0; j < tree->bucket_lens[i]; j++) {
            integer_buckets[i][j] = num_hts;
            num_hts++;
        }
    }

    /* write four header bytes */
    ret = fwrite("MFS\0", 1, 4, stream);
    if (ret != 4) {
        fprintf(stderr, "cannot fwrite\n");
        return -1;
    }

    /* write revision */
    ret = fwrite(&(tree->revision), sizeof(tree->revision), 1, stream);
    if (ret != 1) {
        fprintf(stderr, "cannot fwrite\n");
        return -1;
    }

    /* write number of h_entries */
    ret = fwrite(&num_hts, sizeof(num_hts), 1, stream);
    if (ret != 1) {
        fprintf(stderr, "cannot fwrite\n");
        return -1;
    }

    /* write the root */
    ret = fwrite(&(tree->root), sizeof(struct h_entry), 1, stream);
    if (ret != 1) {
        fprintf(stderr, "cannot fwrite\n");
        return -1;
    }

    for (i = 0; i < NUM_BUCKETS; i++) {
        if (tree->bucket_lens[i] == 0)
            continue;

        for (j = 0; j < tree->bucket_lens[i]; j++) {
            /* save the old value of the parent to restore it later */
            tmp_parent = tree->buckets[i][j]->parent.entry;
            if (tmp_parent == &(tree->root)) {
                tree->buckets[i][j]->parent.offs = 0;
            } else {
                bucket_id = base36_decode_triplet(tmp_parent->key);
                found = false;
                for (k = 0; k < tree->bucket_lens[bucket_id]; k++) {
                    if (tree->buckets[bucket_id][k] == tmp_parent) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    fprintf(stderr, "parent of %s was not found!\n",
                            tree->buckets[i][j]->key);
                    return -1;
                }
                tree->buckets[i][j]->parent.offs =
                    integer_buckets[bucket_id][k];
            }

            /* write out modified record */
            ret =
                fwrite(tree->buckets[i][j], sizeof(struct h_entry), 1, stream);
            if (ret != 1) {
                fprintf(stderr, "cannot fwrite\n");
                return -1;
            }

            /* restore original value for parent */
            tree->buckets[i][j]->parent.entry = tmp_parent;
        }
    }

    for (i = 0; i < NUM_BUCKETS; i++) {
        if (tree->bucket_lens[i] > 0) {
            free(integer_buckets[i]);
        }
    }
    free(integer_buckets);

    return 0;
}

folder_tree    *folder_tree_load(FILE * stream, const char *filecache)
{
    folder_tree    *tree;
    unsigned char   tmp_buffer[4];
    size_t          ret;
    uint64_t        num_hts;
    uint64_t        i;
    struct h_entry **ordered_entries;
    struct h_entry *tmp_entry;
    struct h_entry *parent;
    int             bucket_id;

    /* read and check the first four bytes */
    ret = fread(tmp_buffer, 1, 4, stream);
    if (ret != 4) {
        fprintf(stderr, "cannot fread\n");
        return NULL;
    }

    if (tmp_buffer[0] != 'M' || tmp_buffer[1] != 'F'
        || tmp_buffer[2] != 'S' || tmp_buffer[3] != 0) {
        fprintf(stderr, "invalid magic\n");
        return NULL;
    }

    tree = (folder_tree *) calloc(1, sizeof(folder_tree));

    /* read revision */
    ret = fread(&(tree->revision), sizeof(tree->revision), 1, stream);
    if (ret != 1) {
        fprintf(stderr, "cannot fread\n");
        return NULL;
    }

    /* read number of h_entries to read */
    ret = fread(&num_hts, sizeof(num_hts), 1, stream);
    if (ret != 1) {
        fprintf(stderr, "cannot fread\n");
        return NULL;
    }

    /* read root */
    ret = fread(&(tree->root), sizeof(tree->root), 1, stream);
    if (ret != 1) {
        fprintf(stderr, "cannot fread\n");
        return NULL;
    }
    /* zero out its array of children just in case */
    tree->root.num_children = 0;
    tree->root.children = NULL;

    /* to effectively map integer offsets to addresses we load the file into
     * an array of pointers to h_entry structs and free that array after we're
     * done with setting up the hashtable */

    /* populate the array of children */
    ordered_entries =
        (struct h_entry **)malloc(num_hts * sizeof(struct h_entry *));

    /* the first entry in this array points to the memory allocated for the
     * root */
    ordered_entries[0] = &(tree->root);

    /* read the remaining entries one by one */
    for (i = 1; i < num_hts; i++) {
        tmp_entry = (struct h_entry *)malloc(sizeof(struct h_entry));
        ret = fread(tmp_entry, sizeof(struct h_entry), 1, stream);
        if (ret != 1) {
            fprintf(stderr, "cannot fread\n");
            return NULL;
        }
        /* zero out the array of children */
        tmp_entry->num_children = 0;
        tmp_entry->children = NULL;
        /* store pointer to it in the array */
        ordered_entries[i] = tmp_entry;
    }

    /* turn the parent offset value into a pointer to the memory we allocated
     * earlier, populate the array of children for each entry and sort them
     * into the hashtable */
    for (i = 1; i < num_hts; i++) {
        /* the parent of this entry is at the given offset in the array */
        parent = ordered_entries[ordered_entries[i]->parent.offs];
        ordered_entries[i]->parent.entry = parent;

        /* use the parent information to populate the array of children */
        parent->num_children++;
        parent->children =
            (struct h_entry **)realloc(parent->children,
                                       parent->num_children *
                                       sizeof(struct h_entry *));
        if (parent->children == NULL) {
            fprintf(stderr, "realloc failed\n");
            return NULL;
        }
        parent->children[parent->num_children - 1] = ordered_entries[i];

        /* put the entry into the hashtable */
        bucket_id = base36_decode_triplet(ordered_entries[i]->key);
        tree->bucket_lens[bucket_id]++;
        tree->buckets[bucket_id] =
            (struct h_entry **)realloc(tree->buckets[bucket_id],
                                       tree->bucket_lens[bucket_id] *
                                       sizeof(struct h_entry *));
        if (tree->buckets[bucket_id] == NULL) {
            fprintf(stderr, "realloc failed\n");
            return NULL;
        }
        tree->buckets[bucket_id][tree->bucket_lens[bucket_id] - 1] =
            ordered_entries[i];
    }

    free(ordered_entries);

    tree->filecache = strdup(filecache);

    return tree;
}

folder_tree    *folder_tree_create(const char *filecache)
{
    folder_tree    *tree;

    tree = (folder_tree *) calloc(1, sizeof(folder_tree));

    tree->filecache = strdup(filecache);

    return tree;
}

static void folder_tree_free_entries(folder_tree * tree)
{
    uint64_t        i,
                    j;

    for (i = 0; i < NUM_BUCKETS; i++) {
        for (j = 0; j < tree->bucket_lens[i]; j++) {
            free(tree->buckets[i][j]->children);
            free(tree->buckets[i][j]);
        }
        free(tree->buckets[i]);
        tree->buckets[i] = NULL;
        tree->bucket_lens[i] = 0;
    }
    free(tree->root.children);
    tree->root.children = NULL;
    tree->root.num_children = 0;
}

void folder_tree_destroy(folder_tree * tree)
{
    folder_tree_free_entries(tree);
    free(tree->filecache);
    free(tree);
}

/*
 * given a folderkey, lookup the h_entry struct of it in the hashtable
 *
 * if key is NULL, then a pointer to the root is returned
 *
 * if no matching h_entry struct is found, NULL is returned
 */
static struct h_entry *folder_tree_lookup_key(folder_tree * tree,
                                              const char *key)
{
    int             bucket_id;
    uint64_t        i;

    if (key == NULL || key[0] == '\0') {
        return &(tree->root);
    }
    /* retrieve the right bucket for this key */
    bucket_id = base36_decode_triplet(key);

    for (i = 0; i < tree->bucket_lens[bucket_id]; i++) {
        if (strcmp(tree->buckets[bucket_id][i]->key, key) == 0) {
            return tree->buckets[bucket_id][i];
        }
    }

    fprintf(stderr, "cannot find h_entry struct for key %s\n", key);
    return NULL;
}

/*
 * given a path, return the h_entry struct of the last component
 *
 * the path must start with a slash
 */
static struct h_entry *folder_tree_lookup_path(folder_tree * tree,
                                               mfconn * conn, const char *path)
{
    char           *tmp_path;
    char           *new_path;
    char           *slash_pos;
    struct h_entry *curr_dir;
    struct h_entry *result;
    uint64_t        i;
    bool            success;

    if (path[0] != '/') {
        fprintf(stderr, "Path must start with a slash\n");
        return NULL;
    }

    curr_dir = &(tree->root);

    // if the root is requested, return directly
    if (strcmp(path, "/") == 0) {
        return curr_dir;
    }
    // strip off the leading slash
    new_path = strdup(path + 1);
    tmp_path = new_path;
    result = NULL;

    for (;;) {
        // make sure that curr_dir is up to date
        if (curr_dir->atime == 0
            && curr_dir->local_revision != curr_dir->remote_revision) {
            folder_tree_rebuild_helper(tree, conn, curr_dir);
        }
        // path with a trailing slash, so the remainder is of zero length
        if (tmp_path[0] == '\0') {
            // return curr_dir
            result = curr_dir;
            break;
        }
        slash_pos = strchr(tmp_path, '/');
        if (slash_pos == NULL) {
            // no slash found in the remaining path:
            // find entry in current directory and return it
            for (i = 0; i < curr_dir->num_children; i++) {
                if (strcmp(curr_dir->children[i]->name, tmp_path) == 0) {
                    // return this directory
                    result = curr_dir->children[i];

                    // make sure that result is up to date
                    if (result->atime == 0
                        && result->local_revision != result->remote_revision) {
                        folder_tree_rebuild_helper(tree, conn, result);
                    }
                    break;
                }
            }

            // no matter whether the last part was found or not, iteration
            // stops here
            break;
        }

        *slash_pos = '\0';

        // a slash was found, so recurse into the directory of that name or
        // abort if the name matches a file
        success = false;
        for (i = 0; i < curr_dir->num_children; i++) {
            if (strcmp(curr_dir->children[i]->name, tmp_path) == 0) {
                // test if a file matched
                if (curr_dir->children[i]->atime != 0) {
                    fprintf(stderr,
                            "A file can only be at the end of a path\n");
                    break;
                }
                // a directory matched, break out of this loop and recurse
                // deeper in the next iteration
                curr_dir = curr_dir->children[i];
                success = true;
                break;
            }
        }

        // either a file was part of a path or a folder of matching name was
        // not found, so we break out of this loop too
        if (!success) {
            break;
        }
        // point tmp_path to the character after the last found slash
        tmp_path = slash_pos + 1;
    }

    free(new_path);

    return result;
}

uint64_t folder_tree_path_get_num_children(folder_tree * tree,
                                           mfconn * conn, const char *path)
{
    struct h_entry *result;

    result = folder_tree_lookup_path(tree, conn, path);

    if (result != NULL) {
        return result->num_children;
    } else {
        return -1;
    }
}

bool folder_tree_path_is_root(folder_tree * tree, mfconn * conn,
                              const char *path)
{
    struct h_entry *result;

    result = folder_tree_lookup_path(tree, conn, path);

    if (result != NULL) {
        return result == &(tree->root);
    } else {
        return false;
    }
}

bool folder_tree_path_is_file(folder_tree * tree, mfconn * conn,
                              const char *path)
{
    struct h_entry *result;

    result = folder_tree_lookup_path(tree, conn, path);

    if (result != NULL) {
        return result->atime != 0;
    } else {
        return false;
    }
}

bool folder_tree_path_is_directory(folder_tree * tree, mfconn * conn,
                                   const char *path)
{
    struct h_entry *result;

    result = folder_tree_lookup_path(tree, conn, path);

    if (result != NULL) {
        return result->atime == 0;
    } else {
        return false;
    }
}

const char     *folder_tree_path_get_key(folder_tree * tree, mfconn * conn,
                                         const char *path)
{
    struct h_entry *result;

    result = folder_tree_lookup_path(tree, conn, path);

    if (result != NULL) {
        return result->key;
    } else {
        return NULL;
    }
}

/*
 * given a path, check if it exists in the hashtable
 */
bool folder_tree_path_exists(folder_tree * tree, mfconn * conn,
                             const char *path)
{
    struct h_entry *result;

    result = folder_tree_lookup_path(tree, conn, path);

    return result != NULL;
}

int folder_tree_getattr(folder_tree * tree, mfconn * conn, const char *path,
                        struct stat *stbuf)
{
    struct h_entry *entry;

    entry = folder_tree_lookup_path(tree, conn, path);

    if (entry == NULL) {
        return -ENOENT;
    }

    stbuf->st_uid = geteuid();
    stbuf->st_gid = getegid();
    stbuf->st_ctime = entry->ctime;
    stbuf->st_mtime = entry->ctime;
    if (entry->atime == 0) {
        /* folder */
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = entry->num_children + 2;
        stbuf->st_atime = entry->ctime;
        stbuf->st_size = 1024;
        stbuf->st_blksize = 4096;
        stbuf->st_blocks = 1;   // assume everything fits into a single block
    } else {
        /* file */
        stbuf->st_mode = S_IFREG | 0666;
        stbuf->st_nlink = 1;
        stbuf->st_atime = entry->atime;
        stbuf->st_size = entry->fsize;
        stbuf->st_blksize = 4096;
        stbuf->st_blocks = (entry->fsize) / 4096 + 1;
    }

    return 0;
}

int folder_tree_readdir(folder_tree * tree, mfconn * conn, const char *path,
                        void *buf, fuse_fill_dir_t filldir)
{
    struct h_entry *entry;
    uint64_t        i;

    entry = folder_tree_lookup_path(tree, conn, path);

    /* either directory not found or found entry is not a directory */
    if (entry == NULL || entry->atime != 0) {
        return -ENOENT;
    }

    filldir(buf, ".", NULL, 0);
    filldir(buf, "..", NULL, 0);

    for (i = 0; i < entry->num_children; i++) {
        filldir(buf, entry->children[i]->name, NULL, 0);
    }

    return 0;
}

int folder_tree_tmp_open(folder_tree * tree)
{
    char           *tmpfilename;
    int             fd;

    tmpfilename = strdup_printf("%s/tmp_XXXXXX", tree->filecache);

    fd = mkstemp(tmpfilename);

    // this will cause the file to be removed immediately after it is closed
    unlink(tmpfilename);

    if (fd < 0) {
        fprintf(stderr, "mkstemp failed\n");
        return -1;
    }

    free(tmpfilename);

    return fd;
}

int folder_tree_upload_patch(folder_tree * tree, mfconn * conn,
                             const char *path)
{
    struct h_entry *entry;
    int             retval;
    char           *filename;
    char           *dir_name;
    char           *temp1;
    char           *temp2;
    const char     *folder_key;

    entry = folder_tree_lookup_path(tree, conn, path);
    /* either file not found or found entry is not a file */
    if (entry == NULL || entry->atime == 0) {
        return -ENOENT;
    }


    temp1 = strdup(path);
    filename = basename(temp1);
    temp2 = strdup(path);
    dir_name = dirname(temp2);

    folder_key = folder_tree_path_get_key(tree, conn, dir_name);

    retval = filecache_upload_patch(entry->key, entry->local_revision,
                                    tree->filecache, conn, filename, folder_key);
    free(temp1);
    free(temp2);

    if (retval != 0) {
        fprintf(stderr, "filecache_upload_patch failed\n");
        return -1;
    }

    return 0;
}

int folder_tree_truncate_file(folder_tree * tree, mfconn * conn,
			      const char *path)
{
    struct h_entry *entry;
    int             retval;
    bool            is_file = 0;
    const char     *key = NULL;

    key = folder_tree_path_get_key(tree, conn, path);
    if (key == NULL) {
	fprintf(stderr, "key is NULL\n");
	return -1;
    }

    is_file = folder_tree_path_is_file(tree, conn, path);
    if (!is_file) {
	fprintf(stderr, "Truncate is only defined for files, not folders\n");
	return -1;
    }

    entry = folder_tree_lookup_path(tree, conn, path);
    if (entry == NULL || entry->atime == 0) {
	return -ENOENT;
    }
    retval = filecache_truncate_file(entry->key, key, entry->local_revision,
				     entry->remote_revision, tree->filecache,
				     conn);
    if (retval < 0) {
	fprintf(stderr, "filecache truncate file failed\n");
	return -1;
    }
    entry->local_revision = entry->remote_revision;
      
    return 0;
}

int folder_tree_open_file(folder_tree * tree, mfconn * conn, const char *path,
                          mode_t mode, bool update)
{
    struct h_entry *entry;
    int             retval;

    entry = folder_tree_lookup_path(tree, conn, path);

    /* either file not found or found entry is not a file */
    if (entry == NULL || entry->atime == 0) {
        return -ENOENT;
    }
    fprintf(stderr, "opening %s with local %" PRIu64 " and remote %" PRIu64
            "\n", entry->key, entry->local_revision, entry->remote_revision);

    retval = filecache_open_file(entry->key, entry->local_revision,
                                 entry->remote_revision, entry->fsize,
                                 entry->hash, tree->filecache, conn, mode,
                                 update);
    if (retval == -1) {
        fprintf(stderr, "filecache_open_file failed\n");
        return -1;
    }

    if (update) {
        /* make sure that the local_revision is equal to the remote revision
         * because filecache_open_file took care of doing any updating if it
         * was necessary */
        entry->local_revision = entry->remote_revision;
    }
    // however the file was opened, its access time has to be updated
    entry->atime = time(NULL);

    return retval;
}

static bool folder_tree_is_root(struct h_entry *entry)
{
    if (entry == NULL) {
        fprintf(stderr, "entry is NULL\n");
        return false;
    }

    return (entry->name[0] == '\0'
            || (strncmp(entry->name, "myfiles", sizeof(entry->name)) == 0))
        && entry->key[0] == '\0';
}

/*
 * given a key and the new parent, this function makes sure to allocate new
 * memory if necessary and adjust the children arrays of the former and new
 * parent to accommodate for the change
 */
static struct h_entry *folder_tree_allocate_entry(folder_tree * tree,
                                                  const char *key,
                                                  struct h_entry *new_parent)
{
    struct h_entry *entry;
    int             bucket_id;
    struct h_entry *old_parent;
    uint64_t        i;
    bool            found;

    if (tree == NULL) {
        fprintf(stderr, "tree cannot be NULL\n");
        return NULL;
    }

    if (new_parent == NULL) {
        fprintf(stderr, "new parent cannot be NULL\n");
        return NULL;
    }

    entry = folder_tree_lookup_key(tree, key);

    if (entry == NULL) {
        fprintf(stderr,
                "key is NULL but this is fine, we just create it now\n");
        /* entry was not found, so append it to the end of the bucket */
        entry = (struct h_entry *)calloc(1, sizeof(struct h_entry));
        bucket_id = base36_decode_triplet(key);
        tree->bucket_lens[bucket_id]++;
        tree->buckets[bucket_id] =
            realloc(tree->buckets[bucket_id],
                    sizeof(struct h_entry *) * tree->bucket_lens[bucket_id]);
        if (tree->buckets[bucket_id] == NULL) {
            fprintf(stderr, "realloc failed\n");
            return NULL;
        }
        tree->buckets[bucket_id][tree->bucket_lens[bucket_id] - 1] = entry;

        /* since this entry is new, just add it to the children of its parent
         *
         * since the key of this file or folder did not exist in the
         * hashtable, we do not have to check whether the parent already has
         * it as a child but can just append to its list of children
         */
        new_parent->num_children++;
        new_parent->children =
            (struct h_entry **)realloc(new_parent->children,
                                       new_parent->num_children *
                                       sizeof(struct h_entry *));
        if (new_parent->children == NULL) {
            fprintf(stderr, "realloc failed\n");
            return NULL;
        }
        new_parent->children[new_parent->num_children - 1] = entry;

        return entry;
    }

    /* Entry was found, so remove the entry from the children of the old
     * parent and add it to the children of the new parent */

    old_parent = entry->parent.entry;

    /* check whether entry does not have a parent (this is the case for the
     * root node) */
    if (old_parent != NULL) {
        /* remove the file or folder from the old parent */
        for (i = 0; i < old_parent->num_children; i++) {
            if (old_parent->children[i] == entry) {
                /* move the entries on the right one place to the left */
                memmove(old_parent->children + i, old_parent->children + i + 1,
                        sizeof(struct h_entry *) * (old_parent->num_children -
                                                    i - 1));
                old_parent->num_children--;
                /* change the children size */
                if (old_parent->num_children == 0) {
                    free(old_parent->children);
                    old_parent->children = NULL;
                } else {
                    old_parent->children =
                        (struct h_entry **)realloc(old_parent->children,
                                                   old_parent->num_children *
                                                   sizeof(struct h_entry *));
                    if (old_parent->children == NULL) {
                        fprintf(stderr, "realloc failed\n");
                        return NULL;
                    }
                }
            }
        }
    } else {
        /* sanity check: if the parent was NULL then this entry must be the
         * root */
        if (!folder_tree_is_root(entry)) {
            fprintf(stderr,
                    "the parent was NULL so this node should be root but is not\n");
            fprintf(stderr, "name: %s, key: %s\n", entry->name, entry->key);
            return NULL;
        }
    }

    /* and add it to the new */
    /* since the entry already existed, it can be that the new parent
     * already contains the child */
    found = false;

    for (i = 0; i < new_parent->num_children; i++) {
        if (new_parent->children[i] == entry) {
            found = true;
            break;
        }
    }

    if (!found) {
        new_parent->num_children++;
        new_parent->children =
            (struct h_entry **)realloc(new_parent->children,
                                       new_parent->num_children *
                                       sizeof(struct h_entry *));
        if (new_parent->children == 0) {
            fprintf(stderr, "realloc failed\n");
            return NULL;
        }
        new_parent->children[new_parent->num_children - 1] = entry;
    }

    return entry;
}

/*
 * When adding an existing key, the old key is overwritten.
 * Return the inserted or updated key
 */
static struct h_entry *folder_tree_add_file(folder_tree * tree, mffile * file,
                                            struct h_entry *new_parent)
{
    struct h_entry *old_entry;
    struct h_entry *new_entry;
    uint64_t        old_revision;
    const char     *key;

    if (tree == NULL) {
        fprintf(stderr, "tree cannot be NULL\n");
        return NULL;
    }

    if (file == NULL) {
        fprintf(stderr, "file cannot be NULL\n");
        return NULL;
    }

    if (new_parent == NULL) {
        fprintf(stderr, "new parent cannot be NULL\n");
        return NULL;
    }

    key = file_get_key(file);

    /* if the file already existed in the hashtable, store its old revision
     * so that we can schedule an update of its content at the end of this
     * function */
    old_entry = folder_tree_lookup_key(tree, key);
    if (old_entry != NULL) {
        old_revision = old_entry->local_revision;
    }

    new_entry = folder_tree_allocate_entry(tree, key, new_parent);

    strncpy(new_entry->key, key, sizeof(new_entry->key));
    strncpy(new_entry->name, file_get_name(file), sizeof(new_entry->name));
    new_entry->parent.entry = new_parent;
    new_entry->remote_revision = file_get_revision(file);
    new_entry->ctime = file_get_created(file);
    new_entry->fsize = file_get_size(file);
    if (old_entry != NULL) {
        new_entry->local_revision = old_revision;
    } else {
        new_entry->local_revision = 0;
    }

    /* convert the hex string into its binary representation */
    hex2binary(file_get_hash(file), new_entry->hash);

    /* mark this h_entry struct as a file if its atime is not set yet */
    if (new_entry->atime == 0)
        new_entry->atime = 1;

    return new_entry;
}

/* given an mffolder, add its information to a new h_entry struct, or update an
 * existing h_entry struct in the hashtable
 *
 * if the revision of the existing entry was found to be less than the new
 * entry, also update its contents
 *
 * returns a pointer to the added or updated h_entry struct
 */
static struct h_entry *folder_tree_add_folder(folder_tree * tree,
                                              mffolder * folder,
                                              struct h_entry *new_parent)
{
    struct h_entry *new_entry;
    const char     *key;
    const char     *name;
    uint64_t        old_revision;
    struct h_entry *old_entry;

    if (tree == NULL) {
        fprintf(stderr, "tree cannot be NULL\n");
        return NULL;
    }

    if (folder == NULL) {
        fprintf(stderr, "folder cannot be NULL\n");
        return NULL;
    }

    if (new_parent == NULL) {
        fprintf(stderr, "new parent cannot be NULL\n");
        return NULL;
    }

    key = folder_get_key(folder);

    /* if the folder already existed in the hashtable, store its old revision
     * so that we can schedule an update of its content at the end of this
     * function */
    old_entry = folder_tree_lookup_key(tree, key);
    if (old_entry != NULL) {
        old_revision = old_entry->local_revision;
    }

    new_entry = folder_tree_allocate_entry(tree, key, new_parent);

    /* can be NULL for root */
    if (key != NULL)
        strncpy(new_entry->key, key, sizeof(new_entry->key));
    /* can be NULL for root */
    name = folder_get_name(folder);
    if (name != NULL)
        strncpy(new_entry->name, name, sizeof(new_entry->name));
    new_entry->remote_revision = folder_get_revision(folder);
    new_entry->ctime = folder_get_created(folder);
    new_entry->parent.entry = new_parent;
    if (old_entry != NULL) {
        new_entry->local_revision = old_revision;
    } else {
        new_entry->local_revision = 0;
    }

    return new_entry;
}

/*
 * given a h_entry struct of a folder, this function gets the remote content
 * of that folder and fills its children
 */
static int folder_tree_rebuild_helper(folder_tree * tree, mfconn * conn,
                                      struct h_entry *curr_entry)
{
    int             retval;
    mffolder      **folder_result;
    mffile        **file_result;
    int             i;
    const char     *key;

    /*
     * free the old children array of this folder to make sure that any
     * entries that do not exist on the remote are removed locally
     *
     * we don't free the children it references because they might be
     * referenced by someone else
     *
     * this action will leave all those entries dangling (with a reference to
     * this folder as their parent) which have been completely removed remotely
     * (including from the trash) and thus did not show up in a
     * device/get_changes call. All these entries will be cleaned up by the
     * housekeeping function
     */
    free(curr_entry->children);
    curr_entry->children = NULL;
    curr_entry->num_children = 0;

    /* first folders */
    folder_result = NULL;
    retval =
        mfconn_api_folder_get_content(conn, 0, curr_entry->key, &folder_result,
                                      NULL);
    if (retval != 0) {
        fprintf(stderr, "folder/get_content failed\n");
        if (folder_result != NULL) {
            for (i = 0; folder_result[i] != NULL; i++) {
                free(folder_result[i]);
            }
            free(folder_result);
        }
        return -1;
    }

    for (i = 0; folder_result[i] != NULL; i++) {
        key = folder_get_key(folder_result[i]);
        if (key == NULL) {
            fprintf(stderr, "folder_get_key returned NULL\n");
            folder_free(folder_result[i]);
            continue;
        }
        folder_tree_add_folder(tree, folder_result[i], curr_entry);
        folder_free(folder_result[i]);
    }
    free(folder_result);

    /* then files */
    file_result = NULL;
    retval =
        mfconn_api_folder_get_content(conn, 1, curr_entry->key, NULL,
                                      &file_result);
    if (retval != 0) {
        fprintf(stderr, "folder/get_content failed\n");
        if (file_result != NULL) {
            for (i = 0; file_result[i] != NULL; i++) {
                file_free(file_result[i]);
            }
            free(file_result);
        }
        return -1;
    }

    for (i = 0; file_result[i] != NULL; i++) {
        key = file_get_key(file_result[i]);
        if (key == NULL) {
            fprintf(stderr, "file_get_key returned NULL\n");
            file_free(file_result[i]);
            continue;
        }
        folder_tree_add_file(tree, file_result[i], curr_entry);
        file_free(file_result[i]);
    }
    free(file_result);

    /* since the children have been updated, no update is needed anymore */
    curr_entry->local_revision = curr_entry->remote_revision;

    return 0;
}

/* When trying to delete a non-existing key, nothing happens */
static void folder_tree_remove(folder_tree * tree, const char *key)
{
    int             bucket_id;
    int             found;
    uint64_t        i;
    struct h_entry *entry;
    struct h_entry *parent;

    if (key == NULL) {
        fprintf(stderr, "cannot remove root\n");
        return;
    }

    bucket_id = base36_decode_triplet(key);

    /* check if the key exists */
    found = 0;
    for (i = 0; i < tree->bucket_lens[bucket_id]; i++) {
        if (strcmp(tree->buckets[bucket_id][i]->key, key) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        fprintf(stderr, "key was not found, removing nothing\n");
        return;
    }

    /* if found, use the last value of i to adjust the bucket */
    entry = tree->buckets[bucket_id][i];
    /* move the items on the right one place to the left */
    memmove(tree->buckets[bucket_id] + i, tree->buckets[bucket_id] + i + 1,
            sizeof(struct h_entry *) * (tree->bucket_lens[bucket_id] - i - 1));
    /* change bucket size */
    tree->bucket_lens[bucket_id]--;
    if (tree->bucket_lens[bucket_id] == 0) {
        free(tree->buckets[bucket_id]);
        tree->buckets[bucket_id] = NULL;
    } else {
        tree->buckets[bucket_id] =
            realloc(tree->buckets[bucket_id],
                    sizeof(struct h_entry) * tree->bucket_lens[bucket_id]);
        if (tree->buckets[bucket_id] == NULL) {
            fprintf(stderr, "realloc failed\n");
            return;
        }
    }

    /* if it is a folder, then we have to recurse into its children which
     * reference this folder as their parent because otherwise their parent
     * pointers will reference unallocated memory */
    if (entry->num_children > 0) {
        for (i = 0; i < entry->num_children; i++) {
            if (entry->children[i]->parent.entry == entry) {
                folder_tree_remove(tree, entry->children[i]->key);
            }
        }
    }

    /* remove the entry from its parent */
    parent = entry->parent.entry;
    for (i = 0; i < parent->num_children; i++) {
        if (entry->parent.entry->children[i] == entry) {
            /* move the entries on the right one place to the left */
            memmove(parent->children + i, parent->children + i + 1,
                    sizeof(struct h_entry *) * (parent->num_children - i - 1));
            parent->num_children--;
            /* change the children size */
            if (parent->num_children == 0) {
                free(parent->children);
                parent->children = NULL;
            } else {
                parent->children =
                    (struct h_entry **)realloc(parent->children,
                                               parent->num_children *
                                               sizeof(struct h_entry *));
                if (parent->children == NULL) {
                    fprintf(stderr, "realloc failed\n");
                    return;
                }
            }
        }
    }

    /* remove its possible children */
    free(entry->children);
    /* remove entry */
    free(entry);
}

/*
 * check if a h_entry struct is the parent of another
 *
 * this checks only pointer equivalence and does not compare the key for
 * better performance
 *
 * thus, this function relies on the fact that only one h_entry struct per key
 * exists
 *
 * This function does not use the parent member of the child. If you want to
 * rely on that, then use it directly.
 */
static bool folder_tree_is_parent_of(struct h_entry *parent,
                                     struct h_entry *child)
{
    uint64_t        i;
    bool            found;

    found = false;
    for (i = 0; i < parent->num_children; i++) {
        if (parent->children[i] == child) {
            found = true;
            break;
        }
    }

    return found;
}

/*
 * update the fields of a file
 */
static int folder_tree_update_file_info(folder_tree * tree, mfconn * conn,
                                        const char *key)
{
    mffile         *file;
    int             retval;
    struct h_entry *parent;
    struct h_entry *new_entry;

    file = file_alloc();

    retval = mfconn_api_file_get_info(conn, file, key);
    if (retval != 0) {
        fprintf(stderr, "api call unsuccessful\n");
        /* maybe there is a different reason but for now just assume that an
         * unsuccessful call to file/get_info means that the remote file
         * vanished. Thus we remove the object locally */
        folder_tree_remove(tree, key);
        file_free(file);
        return 0;
    }

    parent = folder_tree_lookup_key(tree, file_get_parent(file));
    if (parent == NULL) {
        fprintf(stderr, "the parent of %s does not exist yet - retrieve it\n",
                key);
        folder_tree_update_folder_info(tree, conn, file_get_parent(file));
    }
    /* parent should exist now, so look it up again */
    parent = folder_tree_lookup_key(tree, file_get_parent(file));

    /* store the updated entry in the hashtable */
    new_entry = folder_tree_add_file(tree, file, parent);

    if (new_entry == NULL) {
        fprintf(stderr, "folder_tree_add_file failed\n");
        file_free(file);
        return -1;
    }

    file_free(file);

    return 0;
}

/*
 * update the fields of a folder through a call to folder/get_info
 *
 * we identify the folder to update by its key instead of its h_entry struct
 * pointer because this function is to fill the h_entry struct in the first
 * place
 *
 * if the folder key does not exist remote, remove it from the hashtable
 *
 * if the folder already existed locally but the remote version was found to
 * be newer, also update its content
 */
static int folder_tree_update_folder_info(folder_tree * tree, mfconn * conn,
                                          const char *key)
{
    mffolder       *folder;
    int             retval;
    struct h_entry *parent;
    struct h_entry *new_entry;

    if (key != NULL && strcmp(key, "trash") == 0) {
        fprintf(stderr, "cannot get folder info of trash\n");
        return -1;
    }

    folder = folder_alloc();

    retval = mfconn_api_folder_get_info(conn, folder, key);
    if (retval != 0) {
        fprintf(stderr, "api call unsuccessful\n");
        /* maybe there is a different reason but for now just assume that an
         * unsuccessful call to file/get_info means that the remote file
         * vanished. Thus we remove the object locally */
        folder_tree_remove(tree, key);
        folder_free(folder);
        return 0;
    }

    /*
     * folder_tree_update_folder_info might have been called during an
     * device/get_changes call in which case, the parent of that folder might
     * not exist yet. We recurse until we reach the root to not have a
     * dangling folder in our hashtable.
     */
    parent = folder_tree_lookup_key(tree, folder_get_parent(folder));
    if (parent == NULL) {
        fprintf(stderr, "the parent of %s does not exist yet - retrieve it\n",
                key);
        folder_tree_update_folder_info(tree, conn, folder_get_parent(folder));
    }
    /* parent should exist now, so look it up again */
    parent = folder_tree_lookup_key(tree, folder_get_parent(folder));

    /* store the updated entry in the hashtable */
    new_entry = folder_tree_add_folder(tree, folder, parent);

    if (new_entry == NULL) {
        fprintf(stderr, "folder_tree_add_folder failed\n");
        folder_free(folder);
        return -1;
    }

    folder_free(folder);

    return 0;
}

/*
 * ask the remote if there are changes after the locally stored revision
 *
 * if yes, integrate those changes
 *
 * the expect_changes parameter allows to skip the call to device/get_status
 * because sometimes one knows that there should be a remote change, so it is
 * useless to waste time on the additional call
 */
void folder_tree_update(folder_tree * tree, mfconn * conn, bool expect_changes)
{
    uint64_t        revision_remote;
    uint64_t        i;
    struct mfconn_device_change *changes;
    int             retval;
    struct h_entry *tmp_entry;
    const char     *key;
    uint64_t        revision;

    if (!expect_changes) {
        retval = mfconn_api_device_get_status(conn, &revision_remote);
        if (retval != 0) {
            fprintf(stderr, "device/get_status failed\n");
            return;
        }

        if (tree->revision == revision_remote) {
            fprintf(stderr, "Request to update but nothing to do\n");
            return;
        }
    }

    /*
     * we maintain the information of each entries parent but that does not
     * mean that we can rely on it when fetching updates via
     * device/get_changes. If a remote object has been permanently removed
     * (trash was emptied) then it will not show up in the results of
     * device/get_changes and thus a remote file will vanish without that file
     * showing up in the device/get_changes output. The only way to clean up
     * those removed files is to use folder/get_content for all folders that
     * changed.
     */

    /*
     * changes have to be applied in the right order but the result of
     * mfconn_api_device_get_changes is already sorted by revision
     */

    changes = NULL;
    retval = mfconn_api_device_get_changes(conn, tree->revision, &changes);
    if (retval != 0) {
        fprintf(stderr, "device/get_changes() failed\n");
        free(changes);
        return;
    }

    for (i = 0; changes[i].change != MFCONN_DEVICE_CHANGE_END; i++) {
        key = changes[i].key;
        revision = changes[i].revision;
        switch (changes[i].change) {
            case MFCONN_DEVICE_CHANGE_DELETED_FOLDER:
            case MFCONN_DEVICE_CHANGE_DELETED_FILE:
                folder_tree_remove(tree, changes[i].key);
                break;
            case MFCONN_DEVICE_CHANGE_UPDATED_FOLDER:
                /* ignore updates of the folder key "trash" or folders with
                 * the parent folder key "trash" */
                if (strcmp(changes[i].key, "trash") == 0)
                    continue;
                if (strcmp(changes[i].parent, "trash") == 0)
                    continue;
                /* only do anything if the revision of the change is greater
                 * than the revision of the locally stored entry */
                tmp_entry = folder_tree_lookup_key(tree, key);
                if (tmp_entry != NULL
                    && tmp_entry->remote_revision >= revision) {
                    break;
                }

                /* if a folder has been updated then its name or location
                 * might have changed... 
                 *
                 * folder_tree_update_folder_info will check whether the
                 * new remote revision is higher than the local revision and
                 * will also fetch the content if this is the case
                 * */
                folder_tree_update_folder_info(tree, conn, changes[i].key);
                break;
            case MFCONN_DEVICE_CHANGE_UPDATED_FILE:
                /* ignore files updated in trash */
                if (strcmp(changes[i].parent, "trash") == 0)
                    continue;
                /* only do anything if the revision of the change is greater
                 * than the revision of the locally stored entry */
                tmp_entry = folder_tree_lookup_key(tree, key);
                if (tmp_entry != NULL
                    && tmp_entry->remote_revision >= revision) {
                    break;
                }
                /* if a file changed, update its info */
                folder_tree_update_file_info(tree, conn, key);
                break;
            case MFCONN_DEVICE_CHANGE_END:
                break;
        }
    }

    /*
     * we have to manually check the root because it never shows up in the
     * results from device_get_changes
     *
     * we don't need to be recursive here because we rely on
     * device/get_changes reporting all changes to its children
     *
     * we update the root AFTER evaluating the results of device/get_changes
     * so that we only have to pull in the remaining changes
     *
     * some recursion will be done if the helper detects that some of the
     * children it updated have a newer revision than the existing ones. This
     * is necessary because device/get_changes does not report changes to
     * items which were even removed from the trash
     */

    folder_tree_rebuild_helper(tree, conn, &(tree->root));

    /* the new revision of the tree is the revision of the terminating change
     * */
    tree->revision = changes[i].revision;

    /*
     * it can happen that another change happened remotely while we were
     * trying to integrate the changes reported by the last device/get_changes
     * results. In that case, the file and folder information we retrieve will
     * have a revision greater than the local device revision we store. This
     * can also lead to lost changes. But this will only be temporary as the
     * situation should be rectified once the next device/get_changes is done.
     *
     * Just remember that due to this it can happen that the revision of the
     * local tree is less than the highest revision of a h_entry struct it
     * stores.
     */

    /* now fix up any possible errors */

    /* clean the resulting folder_tree of any dangling objects */
    fprintf(stderr, "tree before cleaning:\n");
    folder_tree_debug(tree);
    folder_tree_housekeep(tree, conn);
    fprintf(stderr, "tree after cleaning:\n");
    folder_tree_debug(tree);

    /* free allocated memory */
    free(changes);
}

/*
 * rebuild the folder_tree by a walk of the remote filesystem
 *
 * is called to initialize the folder_tree on first use
 *
 * might also be called when local and remote version get out of sync
 */
int folder_tree_rebuild(folder_tree * tree, mfconn * conn)
{
    uint64_t        revision_before;
    int             ret;

    /* free local folder_tree */
    folder_tree_free_entries(tree);

    /* get remote device revision before walking the tree */
    ret = mfconn_api_device_get_status(conn, &revision_before);
    if (ret != 0) {
        fprintf(stderr, "device/get_status call unsuccessful\n");
        return -1;
    }
    tree->revision = revision_before;

    /* walk the remote tree to build the folder_tree */

    /* populate the root */
    ret = folder_tree_update_folder_info(tree, conn, NULL);
    if (ret != 0) {
        fprintf(stderr, "folder_tree_update_folder_info unsuccessful\n");
        return -1;
    }

    folder_tree_rebuild_helper(tree, conn, &(tree->root));

    /*
     * call device/get_changes to get possible remote changes while we walked
     * the tree.
     */
    folder_tree_update(tree, conn, false);

    return 0;
}

/*
 * clean up files and folders that are never referenced
 *
 * first find all folders that have children that do not reference their
 * parent. If a discrepancy is found, ask the remote for the true list of
 * children of that folder.
 *
 * then find all files and folders that have a parent that does not reference
 * them. If a discrepancy is found, ask the remote for the true parent of that
 * file.
 */

void folder_tree_housekeep(folder_tree * tree, mfconn * conn)
{
    uint64_t        i,
                    j,
                    k;
    bool            found;

    /*
     * find objects with children who claim to have a different parent
     *
     * this should actually never happen
     */

    /* first check the root as a special case */

    found = false;
    for (k = 0; k < tree->root.num_children; k++) {
        /* only compare pointers and not keys. This relies on keys
         * being unique */
        if (tree->root.children[k]->parent.entry != &(tree->root)) {
            fprintf(stderr,
                    "root claims that %s is its child but %s doesn't think so\n",
                    tree->root.children[k]->key, tree->root.children[k]->key);
            found = true;
            break;
        }
    }
    if (found) {

        /*
         * some recursion will be done if the helper detects that some of the
         * children it updated have a newer revision than the existing ones.
         * This is necessary because device/get_changes does not report
         * changes to items which were even removed from the trash
         */

        folder_tree_rebuild_helper(tree, conn, &(tree->root));
    }

    /* then check the hashtable */
    for (i = 0; i < NUM_BUCKETS; i++) {
        for (j = 0; j < tree->bucket_lens[i]; j++) {
            found = false;
            for (k = 0; k < tree->buckets[i][j]->num_children; k++) {
                /* only compare pointers and not keys. This relies on keys
                 * being unique */
                if (tree->buckets[i][j]->children[k]->parent.entry !=
                    tree->buckets[i][j]) {
                    fprintf(stderr,
                            "%s claims that %s is its child but %s doesn't think so\n",
                            tree->buckets[i][j]->key,
                            tree->buckets[i][j]->children[k]->key,
                            tree->buckets[i][j]->children[k]->key);
                    found = true;
                    break;
                }
            }
            if (found) {

                /* an entry was found that claims to have a different parent,
                 * so ask the remote to retrieve the real list of children
                 *
                 * some recursion will be done if the helper detects that some
                 * of the children it updated have a newer revision than the
                 * existing ones. This is necessary because device/get_changes
                 * does not report changes to items which were even removed
                 * from the trash
                 */

                folder_tree_rebuild_helper(tree, conn, tree->buckets[i][j]);
            }
        }
    }

    /* find objects whose parents do not match their actual parents
     *
     * this can happen when entries in the local hashtable do not exist
     * anymore at the remote but have not been removed locally because they
     * have not been part of any device/get_changes results. This can happen
     * if the remote entries have been removed completely (including from the
     * trash)
     * */
    for (i = 0; i < NUM_BUCKETS; i++) {
        for (j = 0; j < tree->bucket_lens[i]; j++) {
            if (!folder_tree_is_parent_of
                (tree->buckets[i][j]->parent.entry, tree->buckets[i][j])) {
                fprintf(stderr,
                        "%s claims that %s is its parent but it is not\n",
                        tree->buckets[i][j]->key,
                        tree->buckets[i][j]->parent.entry->key);
                if (tree->buckets[i][j]->atime == 0) {
                    /* folder */
                    folder_tree_update_folder_info(tree, conn,
                                                   tree->buckets[i][j]->key);
                } else {
                    /* file */
                    folder_tree_update_file_info(tree, conn,
                                                 tree->buckets[i][j]->key);
                }
            }
        }
    }

    /* TODO: should this routine call folder_tree_cleanup_filecache to remove
     * unreferenced or outdated files in the cache? */
}

void folder_tree_debug_helper(folder_tree * tree, struct h_entry *ent,
                              int depth)
{
    uint64_t        i;

    if (ent == NULL) {
        ent = &(tree->root);
    }

    for (i = 0; i < ent->num_children; i++) {
        if (ent->children[i]->atime == 0) {
            /* folder */
            fprintf(stderr, "%*s d:%s k:%s p:%s\n", depth + 1, " ",
                    ent->children[i]->name, ent->children[i]->key,
                    ent->children[i]->parent.entry->key);
            folder_tree_debug_helper(tree, ent->children[i], depth + 1);
        } else {
            /* file */
            fprintf(stderr, "%*s f:%s k:%s p:%s\n", depth + 1, " ",
                    ent->children[i]->name, ent->children[i]->key,
                    ent->children[i]->parent.entry->key);
        }
    }
}

void folder_tree_debug(folder_tree * tree)
{
    folder_tree_debug_helper(tree, NULL, 0);
}

static int atime_compare(const void *a, const void *b)
{
    return ((struct h_entry *)a)->atime - ((struct h_entry *)b)->atime;
}

/*
 * to be a valid cache file, the first 15 bytes have to be letters
 * from a-z and numbers from 0-9, the 16th has to be an underscore,
 * the 17th has to be a number from 1-9 and the remaining characters
 * (if any) be a number from 0-9
 */
static bool is_valid_cache_filename(const char *name, char key[],
                                    uint64_t * revision)
{
    int             i;

    for (i = 0; i < 15; i++) {
        if (!islower(name[i]) && !isdigit(name[i]))
            return false;
    }
    if (name[i] != '_')
        return false;
    i++;
    if (name[i] < 49 || name[i] > 57)
        return false;
    for (; name[i] != '\0'; i++) {
        if (!isdigit(name[i]))
            return false;
    }

    // now copy the first 15 bytes from the name to the key
    memcpy(key, name, 15);
    key[15] = '\0';

    *revision = atoll(name + 16);

    return true;
}

/* go through all files in the filecache and check:
 *
 *  - does the filename match the known pattern?
 *      (do not act on other files to avoid accidentally touching user
 *      files)
 *  - is the quickkey known by the hashtable?
 *      - if no, delete
 *  - check if its revision is equal the remote revision
 *      - if no, delete
 *  - check if its size and hash verifies
 *      - if no, delete
 *  - once all files in the cache have been processed this way, check if
 *    the sum of their sizes is greater than X and delete the oldest
 */
void folder_tree_cleanup_filecache(folder_tree * tree, uint64_t allowed_size)
{
    struct dirent  *endp;
    struct dirent  *entryp;
    DIR            *dirp;
    int             retval;
    long            name_max;
    char           *filepath;
    char            key[MFAPI_MAX_LEN_KEY + 1];
    uint64_t        revision;
    struct h_entry *entry;
    size_t          num_cachefiles;
    size_t          i;
    uint64_t        sum_size;
    struct h_entry **cachefiles;

    // from the readdir_r man page
    name_max = pathconf(tree->filecache, _PC_NAME_MAX);
    if (name_max == -1)         /* Limit not defined, or error */
        name_max = 255;         /* Take a guess */
    entryp = malloc(offsetof(struct dirent, d_name) + name_max + 1);

    dirp = opendir(tree->filecache);
    if (dirp == NULL) {
        fprintf(stderr, "cannot open filecache\n");
        return;
    }

    num_cachefiles = 0;
    cachefiles = NULL;

    for (;;) {
        endp = NULL;
        errno = 0;
        endp = readdir(dirp);
        if (!endp && errno) {
            fprintf(stderr, "readdir_r failed\n");
            free(entryp);
            closedir(dirp);
            if (cachefiles != NULL)
                free(cachefiles);
            return;
        }
        if (endp == NULL) {
            break;
        }
        if (strcmp(entryp->d_name, ".") == 0 ||
            strcmp(entryp->d_name, "..") == 0)
            continue;

        if (!is_valid_cache_filename(entryp->d_name, key, &revision)) {
            fprintf(stderr, "not a valid cachefile: %s (ignoring)\n",
                    entryp->d_name);
            continue;
        }

        filepath = strdup_printf("%s/%s", tree->filecache, entryp->d_name);

        entry = folder_tree_lookup_key(tree, key);
        if (entry == NULL) {
            fprintf(stderr, "delete file not in hashtable: %s\n",
                    entryp->d_name);
            retval = unlink(filepath);
            if (retval != 0) {
                fprintf(stderr, "unlink failed\n");
            }
            free(filepath);
            continue;
        }

        if (revision != entry->remote_revision) {
            fprintf(stderr, "delete file with revision %" PRIu64
                    " different from remote %" PRIu64 ": %s\n", revision,
                    entry->remote_revision, entryp->d_name);
            retval = unlink(filepath);
            if (retval != 0) {
                fprintf(stderr, "unlink failed\n");
            }
            entry->local_revision = 0;
            free(filepath);
            continue;
        }

        if (revision != entry->local_revision) {
            fprintf(stderr, "delete file with revision %" PRIu64
                    " different from local %" PRIu64 ": %s\n", revision,
                    entry->local_revision, entryp->d_name);
            retval = unlink(filepath);
            if (retval != 0) {
                fprintf(stderr, "unlink failed\n");
            }
            entry->local_revision = 0;
            free(filepath);
            continue;
        }

        retval = file_check_integrity(filepath, entry->fsize, entry->hash);
        if (retval != 0) {
            fprintf(stderr, "delete file with invalid content: %s\n",
                    entryp->d_name);
            retval = unlink(filepath);
            if (retval != 0) {
                fprintf(stderr, "unlink failed\n");
            }
            entry->local_revision = 0;
            free(filepath);
            continue;
        }
        free(filepath);

        // everything is okay with this one, so append it to the list of files
        // in the cache
        num_cachefiles++;
        cachefiles =
            (struct h_entry **)realloc(cachefiles,
                                       num_cachefiles *
                                       sizeof(struct h_entry *));
        if (cachefiles == NULL) {
            fprintf(stderr, "realloc failed\n");
            free(entryp);
            closedir(dirp);
            return;
        }
        cachefiles[num_cachefiles - 1] = entry;
    }

    free(entryp);
    closedir(dirp);

    // return if there are no files in the cache
    if (num_cachefiles == 0)
        return;

    // now calculate the sum of valid files in the cache and check whether it
    // is larger than allowed
    sum_size = 0;
    for (i = 0; i < num_cachefiles; i++) {
        sum_size += cachefiles[i]->fsize;
    }

    // if the summed size is below the allowed, return
    if (sum_size <= allowed_size) {
        free(cachefiles);
        return;
    }
    // sort the files in the cache by their access time
    qsort(cachefiles, num_cachefiles, sizeof(struct h_entry *), atime_compare);

    // delete the oldest file until sum is below allowed size
    for (i = 0; i < num_cachefiles && sum_size > allowed_size; i++) {
        entry = cachefiles[i];
        fprintf(stderr, "delete file to free space: %s_%" PRIu64 "\n",
                entry->key, entry->remote_revision);
        filepath = strdup_printf("%s/%s_%" PRIu64, tree->filecache, entry->key,
                                 entry->remote_revision);
        retval = unlink(filepath);
        if (retval != 0) {
            fprintf(stderr, "unlink failed\n");
        }
        entry->local_revision = 0;
        free(filepath);
        sum_size -= entry->fsize;
    }

    free(cachefiles);
}
