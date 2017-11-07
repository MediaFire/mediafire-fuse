cmake_minimum_required(VERSION 2.8)

project(mediafire-tools)
set(mediafire-tools_MAJOR_VERSION 0)
set(mediafire-tools_MINOR_VERSION 90)
set(mediafire-tools_VERSION
    ${mediafire-tools_MAJOR_VERSION}.${mediafire-tools_MINOR_VERSION})

# creates file compile_commands.json in build directory which is used
# to feed iwyu during tests
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall")

if (("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang") OR (("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU") AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER 4.0)))
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra")
endif()

# the following is until we learn how to reorder the gcc arguments to correctly link on Ubuntu
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-as-needed")

# ensure large file support (also necessary for fuse)
add_definitions("-D_FILE_OFFSET_BITS=64")

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

find_package(Threads REQUIRED)

find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/3rdparty/cmake)

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
       find_package(FUSE 2.9 REQUIRED)
       include_directories(${FUSE_INCLUDE_DIRS})
endif()

find_package(Jansson 2.2 REQUIRED)
include_directories(${JANSSON_INCLUDE_DIRS})

add_library(mfapi STATIC
	mfapi/mfconn.c
	mfapi/file.c
	mfapi/account.c
	mfapi/folder.c
	mfapi/patch.c
	mfapi/apicalls.c
	mfapi/apicalls/file_get_info.c
	mfapi/apicalls/file_move.c
	mfapi/apicalls/file_update.c
	mfapi/apicalls/user_get_info.c
	mfapi/apicalls/file_get_links.c
	mfapi/apicalls/file_delete.c
	mfapi/apicalls/user_get_session_token.c
	mfapi/apicalls/folder_get_info.c
	mfapi/apicalls/folder_create.c
	mfapi/apicalls/folder_get_content.c
	mfapi/apicalls/folder_delete.c
	mfapi/apicalls/folder_move.c
	mfapi/apicalls/folder_update.c
	mfapi/apicalls/device_get_status.c
	mfapi/apicalls/device_get_changes.c
	mfapi/apicalls/device_get_patch.c
	mfapi/apicalls/device_get_updates.c
	mfapi/apicalls/upload_check.c
	mfapi/apicalls/upload_instant.c
	mfapi/apicalls/upload_simple.c
	mfapi/apicalls/upload_patch.c
	mfapi/apicalls/upload_poll_upload.c
	)

add_library(mfutils STATIC
	utils/http.c
	utils/strings.c
	utils/stringv.c
	utils/xdelta3.c
    utils/fsio.c
	utils/hash.c
    utils/config.c)

add_executable(mediafire-shell
	mfshell/main.c
	mfshell/mfshell.c
    mfshell/terminal.c
    mfshell/signals.c
	mfshell/commands/folder.c
	mfshell/commands/auth.c
	mfshell/commands/chdir.c
	mfshell/commands/debug.c
	mfshell/commands/file.c
	mfshell/commands/get.c
	mfshell/commands/put.c
	mfshell/commands/help.c
	mfshell/commands/host.c
	mfshell/commands/lcd.c
	mfshell/commands/links.c
	mfshell/commands/list.c
	mfshell/commands/lpwd.c
	mfshell/commands/mkdir.c
	mfshell/commands/pwd.c
	mfshell/commands/whoami.c
	mfshell/commands/rmdir.c
	mfshell/commands/rm.c
	mfshell/commands/status.c
	mfshell/commands/changes.c
	mfshell/config.c
	mfshell/options.c
	mfshell/commands/updates.c)
target_link_libraries(mediafire-shell mfapi mfutils ${CURL_LIBRARIES} ${OPENSSL_LIBRARIES} ${JANSSON_LIBRARIES})

enable_testing()

add_executable(mediafire-fuse
	fuse/main.c
	fuse/hashtbl.c
	fuse/filecache.c
	fuse/operations/access.c
    fuse/operations/chmod.c
    fuse/operations/chown.c
    fuse/operations/create.c
    fuse/operations/destroy.c
    fuse/operations/flush.c
    fuse/operations/fsync.c
    fuse/operations/fsyncdir.c
    fuse/operations/getattr.c
    fuse/operations/getxattr.c
    fuse/operations/link.c
    fuse/operations/mkdir.c
    fuse/operations/mknod.c
    fuse/operations/open.c
    fuse/operations/opendir.c
    fuse/operations/read.c
    fuse/operations/readdir.c
    fuse/operations/readlink.c
    fuse/operations/release.c
    fuse/operations/releasedir.c
    fuse/operations/removexattr.c
    fuse/operations/listxattr.c
    fuse/operations/rename.c
    fuse/operations/rmdir.c
    fuse/operations/setxattr.c
    fuse/operations/statfs.c
    fuse/operations/symlink.c
    fuse/operations/truncate.c
    fuse/operations/unlink.c
    fuse/operations/utimens.c
    fuse/operations/write.c)
target_link_libraries(mediafire-fuse mfapi mfutils ${CMAKE_THREAD_LIBS_INIT} ${CURL_LIBRARIES} ${OPENSSL_LIBRARIES} ${FUSE_LIBRARIES} ${JANSSON_LIBRARIES})

add_test(iwyu ${CMAKE_SOURCE_DIR}/tests/iwyu.py ${CMAKE_BINARY_DIR})
add_test(indent ${CMAKE_SOURCE_DIR}/tests/indent.sh ${CMAKE_SOURCE_DIR})
add_test(valgrind_fuse ${CMAKE_SOURCE_DIR}/tests/valgrind_fuse.sh ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
add_test(valgrind_shell ${CMAKE_SOURCE_DIR}/tests/valgrind_shell.sh ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})

install (TARGETS mediafire-fuse mediafire-shell DESTINATION bin)

SET(CPACK_GENERATOR "DEB")

#generator independent vars
SET(CPACK_PACKAGE_VERSION ${mediafire-tools_VERSION})
SET(CPACK_PACKAGE_DESCRIPTION "FUSE module and cli tools for MediaFire")
SET(CPACK_PACKAGE_VENDOR "MediaFire")
SET(CPACK_PACKAGE_CONTACT "Bryan Christ")
SET(CPACK_PACKAGE_FILE_NAME "mediafire-tools-${CPACK_PACKAGE_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}")

SET(CPACK_DEBIAN_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl (>= 7.22.0), libfuse (>= 2.8.6), libjansson4 (>= 2.2.0) ")


INCLUDE(CPack)
