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

#define _XOPEN_SOURCE       // required for fileno()

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <inttypes.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <openssl/sha.h>
#include <openssl/md5.h>

#include "hash.h"
#include "fsio.h"

typedef struct _hasher_s
{
    unsigned char   *hash;

    int             state;

    union
    {
        MD5_CTX     md5;
        SHA256_CTX  sha256;
    };
}
hasher_t;


/*
 * we use this table to convert from a base36 char (ignoring case) to an
 * integer or from a hex string to binary (in the latter case letters g-z and
 * G-Z remain unused)
 * we "waste" these 128 bytes of memory so that we don't need branching
 * instructions when decoding
 * we only need 128 bytes because the input is a *signed* char
 */
static unsigned char base36_decoding_table[] = {
/* 0x00 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 0x10 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 0x20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 0x30 */ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
/* 0x40 */ 0, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
/* 0x50 */ 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0, 0, 0, 0, 0,
/* 0x60 */ 0, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
/* 0x70 */ 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0, 0, 0, 0, 0
};

/*
 * table to convert from a byte into the two hexadecimal digits representing
 * it
 *
 * the hex chars have to be lower case until server-side is fixed to accept
 * both cases
 */
static char     base16_encoding_table[][2] = {
    "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0a", "0b",
    "0c", "0d", "0e", "0f", "10", "11", "12", "13", "14", "15", "16", "17",
    "18", "19", "1a", "1b", "1c", "1d", "1e", "1f", "20", "21", "22", "23",
    "24", "25", "26", "27", "28", "29", "2a", "2b", "2c", "2d", "2e", "2f",
    "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "3a", "3b",
    "3c", "3d", "3e", "3f", "40", "41", "42", "43", "44", "45", "46", "47",
    "48", "49", "4a", "4b", "4c", "4d", "4e", "4f", "50", "51", "52", "53",
    "54", "55", "56", "57", "58", "59", "5a", "5b", "5c", "5d", "5e", "5f",
    "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "6a", "6b",
    "6c", "6d", "6e", "6f", "70", "71", "72", "73", "74", "75", "76", "77",
    "78", "79", "7a", "7b", "7c", "7d", "7e", "7f", "80", "81", "82", "83",
    "84", "85", "86", "87", "88", "89", "8a", "8b", "8c", "8d", "8e", "8f",
    "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "9a", "9b",
    "9c", "9d", "9e", "9f", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    "a8", "a9", "aa", "ab", "ac", "ad", "ae", "af", "b0", "b1", "b2", "b3",
    "b4", "b5", "b6", "b7", "b8", "b9", "ba", "bb", "bc", "bd", "be", "bf",
    "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8", "c9", "ca", "cb",
    "cc", "cd", "ce", "cf", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
    "d8", "d9", "da", "db", "dc", "dd", "de", "df", "e0", "e1", "e2", "e3",
    "e4", "e5", "e6", "e7", "e8", "e9", "ea", "eb", "ec", "ed", "ee", "ef",
    "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "fa", "fb",
    "fc", "fd", "fe", "ff"
};

// private callback
static int  _md5_hash_cb(fsio_t *fsio,int event,fsio_data_t *fsio_data);

static int  _sha256_hash_cb(fsio_t *fsio,int event,fsio_data_t *fsio_data);

int calc_md5(FILE * file, unsigned char *hash)
{
    fsio_t          *fsio = NULL;
    hasher_t        *hasher = NULL;
    int             fd;
    ssize_t         bytes = -1;     // read to end of file
    int             retval;

    fd = fileno(file);
    if(fd == -1) return -1;

    // make a copy
    fd = dup(fd);

    // initalize state
    hasher = (hasher_t*)calloc(1,sizeof(hasher_t));
    hasher->state = 0;
    hasher->hash = hash;

    fsio = fsio_create();
    fsio_set_source(fsio,fd);

    fsio_set_hook(fsio,FSIO_EVENT_BLOCK_READ,_md5_hash_cb);
    fsio_set_hook_data(fsio,FSIO_EVENT_BLOCK_READ,(void*)hasher);

    fsio_set_hook(fsio,FSIO_EVENT_FILE_READ,_md5_hash_cb);
    fsio_set_hook_data(fsio,FSIO_EVENT_FILE_READ,(void*)hasher);

    retval = fsio_file_read(fsio,&bytes);

    fsio_destroy(fsio,true);

    return retval;
}

/*
 * calculate the SHA256 sum and optionally (if size != NULL) count the file
 * size
 */
int calc_sha256(FILE * file, unsigned char *hash, uint64_t * size)
{
    fsio_t          *fsio = NULL;
    hasher_t        *hasher = NULL;
    int             fd;
    ssize_t         bytes = -1;     // read to end of file
    int             retval;

    fd = fileno(file);
    if(fd == -1) return -1;

    // make a copy
    fd = dup(fd);

    // initalize state
    hasher = (hasher_t*)calloc(1,sizeof(hasher_t));
    hasher->state = 0;
    hasher->hash = hash;

    fsio = fsio_create();
    fsio_set_source(fsio,fd);

    fsio_set_hook(fsio,FSIO_EVENT_BLOCK_READ,_sha256_hash_cb);
    fsio_set_hook_data(fsio,FSIO_EVENT_BLOCK_READ,(void*)hasher);

    fsio_set_hook(fsio,FSIO_EVENT_FILE_READ,_sha256_hash_cb);
    fsio_set_hook_data(fsio,FSIO_EVENT_FILE_READ,(void*)hasher);

    if(size != NULL) bytes = *size;

    retval = fsio_file_read(fsio,&bytes);

    fsio_destroy(fsio,true);
    if(hasher != NULL) free(hasher);

    return retval;
}

/* decodes a zero terminated string containing hex characters into their
 * binary representation. The length of the string must be even as pairs of
 * characters are converted to one output byte. The output buffer must be at
 * least half the length of the input string.
 */
void hex2binary(const char *hex, unsigned char *binary)
{
    unsigned char   val1,
                    val2;
    const char     *c1,
                   *c2;
    unsigned char  *b;

    for (b = binary, c1 = hex, c2 = hex + 1;
         *c1 != '\0' && *c2 != '\0'; b++, c1 += 2, c2 += 2) {
        val1 = base36_decoding_table[(int)(*c1)];
        val2 = base36_decoding_table[(int)(*c2)];
        *b = (val1 << 4) | val2;
    }
}

char           *binary2hex(const unsigned char *binary, size_t length)
{
    char           *out;
    char           *p;
    size_t          i;

    out = malloc(length * 2 + 1);
    if (out == NULL) {
        fprintf(stderr, "cannot allocate memory\n");
        return NULL;
    }
    for (i = 0; i < length; i++) {
        p = base16_encoding_table[binary[i]];
        out[i * 2] = p[0];
        out[i * 2 + 1] = p[1];
    }
    out[length * 2] = '\0';
    return out;
}

/*
 * a function to convert a char* of the key into a hash of its first three
 * characters, treating those first three characters as if they represented a
 * number in base36
 *
 * in the future this could be made more dynamic by using the ability of
 * strtoll to convert numbers of base36 and then only retrieving the desired
 * amount of high-bits for the desired size of the hashtable
 */
int base36_decode_triplet(const char *key)
{
    return base36_decoding_table[(int)(key)[0]] * 36 * 36
        + base36_decoding_table[(int)(key)[1]] * 36
        + base36_decoding_table[(int)(key)[2]];
}

int file_check_integrity(const char *path, uint64_t fsize,
                         const unsigned char *fhash)
{
    int             retval;

    retval = file_check_integrity_size(path, fsize);
    if (retval != 0) {
        fprintf(stderr, "file_check_integrity_size failed\n");
        return -1;
    }

    retval = file_check_integrity_hash(path, fhash);
    if (retval != 0) {
        fprintf(stderr, "file_check_integrity_hash failed\n");
        return -1;
    }

    return 0;
}

int file_check_integrity_size(const char *path, uint64_t fsize)
{
    struct stat     file_info;
    int             retval;
    uint64_t        bytes_read;

    /* check if the size of the downloaded file matches the expected size */
    memset(&file_info, 0, sizeof(file_info));
    retval = stat(path, &file_info);

    if (retval != 0) {
        fprintf(stderr, "stat failed\n");
        return -1;
    }

    bytes_read = file_info.st_size;

    if (bytes_read != fsize) {
        fprintf(stderr,
                "expected %" PRIu64 " bytes but got %" PRIu64 " bytes\n",
                fsize, bytes_read);
        return -1;
    }

    return 0;
}

int file_check_integrity_hash(const char *path, const unsigned char *fhash)
{
    int             retval;
    FILE           *fh;
    unsigned char   hash[SHA256_DIGEST_LENGTH];
    char           *hexhash;

    fh = fopen(path, "r");
    if (fh == NULL) {
        perror("cannot open file");
        return -1;
    }

    retval = calc_sha256(fh, hash, NULL);
    if (retval != 0) {
        fprintf(stderr, "failed to calculate hash\n");
        fclose(fh);
        return -1;
    }

    fclose(fh);

    if (memcmp(fhash, hash, SHA256_DIGEST_LENGTH) != 0) {
        fprintf(stderr, "hashes are not equal\n");
        hexhash = binary2hex(fhash, SHA256_DIGEST_LENGTH);
        fprintf(stderr, "remote:     %s\n", hexhash);
        free(hexhash);
        hexhash = binary2hex(hash, SHA256_DIGEST_LENGTH);
        fprintf(stderr, "downloaded: %s\n", hexhash);
        free(hexhash);
        return -1;
    }

    return 0;
}

// end of public funcs
static int
_md5_hash_cb(fsio_t *fsio,int event,fsio_data_t *fsio_data)
{
    hasher_t    *hasher;

    if(fsio == NULL || fsio_data == NULL) return -1;

    hasher = fsio_data->anything;

    if(event == FSIO_EVENT_BLOCK_READ)
    {
        if(hasher->state == 0)
        {
            MD5_Init(&hasher->md5);
            hasher->state = 1;
        }

        if(hasher->state == 1)
        {
            if(fsio_data->data_sz > 0)
            {
                MD5_Update(&hasher->md5,fsio_data->data,fsio_data->data_sz);
            }
        }

        return 0;
    }

    if(event == FSIO_EVENT_FILE_READ)
    {
        MD5_Final(hasher->hash,&hasher->md5);
    }

    return 0;
}


static int
_sha256_hash_cb(fsio_t *fsio,int event,fsio_data_t *fsio_data)
{
    hasher_t    *hasher = NULL;

    if(fsio == NULL || fsio_data == NULL) return -1;

    hasher = (hasher_t*)fsio_data->anything;

    if(event == FSIO_EVENT_BLOCK_READ)
    {
        if(hasher->state == 0)
        {
            fprintf(stderr,"state 0\n");
            SHA256_Init(&hasher->sha256);
            hasher->state = 1;
        }

        if(hasher->state == 1)
        {
            if(fsio_data->data_sz > 0)
            {
                SHA256_Update(&hasher->sha256,fsio_data->data,
                                fsio_data->data_sz);
            }
        }

        return 0;
    }

    if(event == FSIO_EVENT_FILE_READ)
    {
        SHA256_Final(hasher->hash,&hasher->sha256);
    }

    return 0;
}

