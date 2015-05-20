#!/bin/bash
gcc -o upload_auth curl_auth_upload.c -lssl -lcurl -lcrypto -ljansson
chmod +x ./upload_auth
