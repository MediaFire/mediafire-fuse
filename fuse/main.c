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
#define _DEFAULT_SOURCE         // for strdup on old systems
#define _GNU_SOURCE             // for getline on old systems

#define FUSE_USE_VERSION 30

#include <fuse/fuse.h>
#include <fuse/fuse_opt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <pwd.h>
#include <stdbool.h>
#include <pthread.h>

#include "../mfapi/mfconn.h"
#include "hashtbl.h"
#include "operations.h"
#include "../utils/strings.h"
#include "../utils/stringv.h"
#include "../utils/http.h"
#include "../utils/config.h"

enum {
    KEY_HELP,
    KEY_VERSION,
    KEY_LAZY_SSL,
};

struct mediafirefs_user_options
{
    char                *username;
    char                *password;
    char                *configfile;
    char                *server;
    int                 app_id;
    char                *api_key;

    unsigned int        http_flags;
};

static struct fuse_operations mediafirefs_oper = {
    .getattr = mediafirefs_getattr,
    .readlink = mediafirefs_readlink,
    .mknod = mediafirefs_mknod,
    .mkdir = mediafirefs_mkdir,
    .unlink = mediafirefs_unlink,
    .rmdir = mediafirefs_rmdir,
    .symlink = mediafirefs_symlink,
    .rename = mediafirefs_rename,
    .link = mediafirefs_link,
    .chmod = mediafirefs_chmod,
    .chown = mediafirefs_chown,
    .truncate = mediafirefs_truncate,
    .open = mediafirefs_open,
    .read = mediafirefs_read,
    .write = mediafirefs_write,
    .statfs = mediafirefs_statfs,
    .flush = mediafirefs_flush,
    .release = mediafirefs_release,
    .fsync = mediafirefs_fsync,
    .setxattr = mediafirefs_setxattr,
    .getxattr = mediafirefs_getxattr,
    .listxattr = mediafirefs_listxattr,
    .removexattr = mediafirefs_removexattr,
    .opendir = mediafirefs_opendir,
    .readdir = mediafirefs_readdir,
    .releasedir = mediafirefs_releasedir,
    .fsyncdir = mediafirefs_fsyncdir,
//  .init = mediafirefs_init,
    .destroy = mediafirefs_destroy,
    .access = mediafirefs_access,
    .create = mediafirefs_create,
    .utimens = mediafirefs_utimens,
};

static void usage(const char *progname)
{
    fprintf(stderr, "Usage %s [options] mountpoint\n"
            "\n"
            "general options:\n"
            "    -o opt[,opt...]        mount options\n"
            "    -h, --help             show this help\n"
            "    -V, --version          show version information\n"
            "\n"
            "MediaFire FS options:\n"
            "    -o, --username str     username\n"
            "    -p, --password str     password\n"
            "    -c, --config file      configuration file\n"
            "    --server domain        server domain\n"
            "    -i, --app-id id        App ID\n"
            "    -k, --api-key key      API Key\n"
            "    -l, --lazy-ssl         Disables SSL peer validation\n"
            "\n"
            "Notice that long options are separated from their arguments by\n"
            "a space and not an equal sign.\n" "\n", progname);
}

// this handler is for just for HELP and VERSION

static int
mediafirefs_opt_pre(void *data, const char *arg, int key,
                     struct fuse_args *outargs)
{
    (void)data;                 // unused
    (void)arg;                  // unused

    if (key == KEY_HELP) {
        usage(outargs->argv[0]);
        fuse_opt_add_arg(outargs, "-ho");
        fuse_main(outargs->argc, outargs->argv, &mediafirefs_oper, NULL);
        exit(0);
    }

    if (key == KEY_VERSION) {
        fuse_opt_add_arg(outargs, "--version");
        fuse_main(outargs->argc, outargs->argv, &mediafirefs_oper, NULL);
        exit(0);
    }

    return 1;
}

// this handler is for the all other keys that we will not abort on

static int
mediafirefs_opt_post(void *data, const char *arg, int key,
                     struct fuse_args *outargs)
{
    struct mediafirefs_user_options     *options = NULL;

    (void)arg;                  // unused
    (void)outargs;              // unused

    options = (struct mediafirefs_user_options*)data;

    if (key == KEY_LAZY_SSL) {

        options->http_flags |= HTTP_FLAG_LAZY_SSL;

        return 0;
    }

    return 1;
}

static void parse_arguments(int *argc, char ***argv,
                            struct mediafirefs_user_options *options,
                            char *configfile)
{
    FILE    *file = NULL;
    int     i;

    /* In the first pass, we only search for the help, version and config file
     * options.
     *
     * In case help or version options are found, the program will display
     * them and quit
     *
     * Otherwise, the program will read the config file (possibly supplied
     * through the option being set in the first pass), prepend its arguments
     * to argv and parse the arguments again as a second pass.
     */

    struct fuse_opt mediafirefs_opts_fst[] = {
        FUSE_OPT_KEY("-h", KEY_HELP),
        FUSE_OPT_KEY("--help", KEY_HELP),
        FUSE_OPT_KEY("-V", KEY_VERSION),
        FUSE_OPT_KEY("--version", KEY_VERSION),
        {"-c %s", offsetof(struct mediafirefs_user_options, configfile), 0},
        {"--config %s", offsetof(struct mediafirefs_user_options, configfile),
         0},
        FUSE_OPT_END
    };

    struct fuse_args args_fst = FUSE_ARGS_INIT(*argc, *argv);

    if (fuse_opt_parse
        (&args_fst, options, mediafirefs_opts_fst,
         mediafirefs_opt_pre) == -1) {
        exit(1);
    }

    *argc = args_fst.argc;
    *argv = args_fst.argv;

    if (options->configfile != NULL) {

        file = fopen(options->configfile,"r");
        config_file_read(file, argc, argv);
        fclose(file);

    } else if (configfile != NULL) {

        file = fopen(configfile,"r");
        config_file_read(file, argc, argv);
        fclose(file);
    }

    struct fuse_opt mediafirefs_opts_snd[] = {

        {"-c %s", offsetof(struct mediafirefs_user_options, configfile), 0},
        {"--config %s", offsetof(struct mediafirefs_user_options, configfile),
         0},
        {"-u %s", offsetof(struct mediafirefs_user_options, username), 0},
        {"--username %s", offsetof(struct mediafirefs_user_options, username),
         0},
        {"-p %s", offsetof(struct mediafirefs_user_options, password), 0},
        {"--password %s", offsetof(struct mediafirefs_user_options, password),
         0},
        {"--server %s", offsetof(struct mediafirefs_user_options, server), 0},
        {"-i %d", offsetof(struct mediafirefs_user_options, app_id), 0},
        {"--app-id %d", offsetof(struct mediafirefs_user_options, app_id), 0},
        {"-k %s", offsetof(struct mediafirefs_user_options, api_key), 0},
        {"--api-key %s", offsetof(struct mediafirefs_user_options, api_key),
         0},

        FUSE_OPT_KEY("-l", KEY_LAZY_SSL),
        FUSE_OPT_KEY("--lazy-ssl", KEY_LAZY_SSL),
        FUSE_OPT_END
    };

    struct fuse_args args_snd = FUSE_ARGS_INIT(*argc, *argv);

    if (fuse_opt_parse(&args_snd, options, mediafirefs_opts_snd,
        mediafirefs_opt_post) == -1) {
        exit(1);
    }

    if (*argv != args_snd.argv) {
        for (i = 0; i < *argc; i++) {
            free((*argv)[i]);
        }
        free(*argv);
    }

    *argc = args_snd.argc;
    *argv = args_snd.argv;
}

static void connect_mf(struct mediafirefs_user_options *options,
                       mfconn ** conn)
{
    if (options->app_id == -1) {
        options->app_id = 42709;
    }

    if (options->server == NULL) {
        options->server = "www.mediafire.com";
    }

    *conn = mfconn_create(options->server, options->username,
                            options->password, options->app_id,
                            options->api_key, 3, options->http_flags);

    if (*conn == NULL) {
        fprintf(stderr, "Cannot establish connection\n");
        exit(1);
    }
}

static void open_hashtbl(const char *dircache, const char *filecache,
                         mfconn * conn, folder_tree ** tree)
{
    FILE           *fp;

    fp = fopen(dircache, "r");
    if (fp != NULL) {

        // file exists
        fprintf(stderr, "loading hashtable from %s\n", dircache);

        *tree = folder_tree_load(fp, filecache);

        fclose(fp);

        if (*tree != NULL) {

            // TODO: make the maximum cache size configurable
            // size is given in bytes and current default is 1 GiB
            folder_tree_cleanup_filecache(*tree, 1073741824);

            folder_tree_update(*tree, conn, false);

            return;
        }

        fprintf(stderr, "cannot load directory hashtable - starting"
                " a new one\n");
    }
    // file doesn't exist or is corrupt
    fprintf(stderr, "creating new hashtable\n");
    *tree = folder_tree_create(filecache);

    folder_tree_rebuild(*tree, conn);

    //folder_tree_housekeep(tree);

    fprintf(stderr, "tree before starting fuse:\n");
    folder_tree_debug(*tree);
}

static void setup_cache_dir(const char *ekey, char **dircache,
                            char **filecache)
{
    const char     *homedir;
    const char     *cachedir;
    const char     *usercachedir;

    homedir = getenv("HOME");
    if (homedir == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }

    cachedir = getenv("XDG_CACHE_HOME");
    if (cachedir == NULL) {
        // $HOME/.cache/mediafire-tools
        cachedir = strdup_printf("%s/.cache", homedir);
        if (mkdir(cachedir, 0755) != 0 && errno != EEXIST) {
            perror("mkdir");
            fprintf(stderr, "cannot create %s\n", cachedir);
            exit(1);
        }
        free((void *)cachedir);
        cachedir = strdup_printf("%s/.cache/mediafire-tools", homedir);
    } else {
        // $XDG_CONFIG_HOME/mediafire-tools
        if (mkdir(cachedir, 0755) != 0 && errno != EEXIST) {
            perror("mkdir");
            fprintf(stderr, "cannot create %s\n", cachedir);
            exit(1);
        }
        cachedir = strdup_printf("%s/mediafire-tools", cachedir);
    }
    /* EEXIST is okay, so only fail if it is something else */
    if (mkdir(cachedir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
        fprintf(stderr, "cannot create %s\n", cachedir);
        exit(1);
    }
    /* now create the subdirectory for the current ekey */
    usercachedir = strdup_printf("%s/%s", cachedir, ekey);
    /* EEXIST is okay, so only fail if it is something else */
    if (mkdir(usercachedir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
        fprintf(stderr, "cannot create %s\n", usercachedir);
        exit(1);
    }

    *dircache = strdup_printf("%s/directorytree", usercachedir);

    *filecache = strdup_printf("%s/files", usercachedir);
    if (mkdir(*filecache, 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
        fprintf(stderr, "cannot create %s\n", *filecache);
        exit(1);
    }

    free((void *)cachedir);
    free((void *)usercachedir);
}

int main(int argc, char *argv[])
{
    int             ret,
                    i;
    struct mediafirefs_context_private      *ctx;

    struct mediafirefs_user_options options = {
        NULL, NULL, NULL, NULL, -1, NULL, 0,
    };

    pthread_mutexattr_t     mutex_attr;

    ctx = calloc(1, sizeof(struct mediafirefs_context_private));

    config_file_init(&(ctx->configfile));

    parse_arguments(&argc, &argv, &options, ctx->configfile);

    if (options.username == NULL) {
        printf("login: ");
        options.username = string_line_from_stdin(false);
    }
    if (options.password == NULL) {
        printf("passwd: ");
        options.password = string_line_from_stdin(true);
    }

    connect_mf(&options, &(ctx->conn));

    setup_cache_dir(mfconn_get_ekey(ctx->conn), &(ctx->dircache),
                    &(ctx->filecache));

    open_hashtbl(ctx->dircache, ctx->filecache, ctx->conn, &(ctx->tree));

    ctx->sv_writefiles = stringv_alloc();
    ctx->sv_readonlyfiles = stringv_alloc();
    ctx->last_status_check = 0;
    ctx->interval_status_check = 60;    // TODO: make this configurable

    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr,PTHREAD_MUTEX_RECURSIVE);

    pthread_mutex_init(&(ctx->mutex),
        (const pthread_mutexattr_t*)&mutex_attr);

    ret = fuse_main(argc, argv, &mediafirefs_oper, ctx);

    for (i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);

    free(ctx->configfile);
    free(ctx->dircache);
    free(ctx->filecache);
    stringv_free(ctx->sv_writefiles);
    stringv_free(ctx->sv_readonlyfiles);
    pthread_mutex_destroy(&(ctx->mutex));
    pthread_mutexattr_destroy(&mutex_attr);
    free(ctx);

    return ret;
}
