#!/bin/bash
gcc -o upload_filedrop curl_filedrop_upload.c -lcurl
chmod +x ./upload_filedrop
