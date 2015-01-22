cmake_minimum_required(VERSION 2.8)

project(mediafire-tools)

# creates file compile_commands.json in build directory which is used
# to feed iwyu during tests
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall -Werror")

if (("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang") OR (("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU") AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER 4.0)))
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra")
else()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fms-extensions")
endif()

# the following is until we learn how to reorder the gcc arguments to correctly link on Ubuntu
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-as-needed")

# ensure large file support (also necessary for fuse)
add_definitions("-D_FILE_OFFSET_BITS=64")

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/3rdparty/cmake)

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
       find_package(FUSE 2.9 REQUIRED)
       include_directories(${FUSE_INCLUDE_DIRS})
endif()

find_package(Jansson 2.5 REQUIRED)
include_directories(${JANSSON_INCLUDE_DIRS})

add_library(mfapi OBJECT
	mfapi/mfconn.c
	mfapi/file.c
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

add_library(mfutils OBJECT
	utils/http.c
	utils/strings.c
	utils/stringv.c
	utils/xdelta3.c
	utils/hash.c)

add_executable(mediafire-shell
	$<TARGET_OBJECTS:mfapi>
	$<TARGET_OBJECTS:mfutils>
	mfshell/main.c
	mfshell/mfshell.c
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
target_link_libraries(mediafire-shell ${CURL_LIBRARIES} ${OPENSSL_LIBRARIES} ${JANSSON_LIBRARIES})

enable_testing()

add_executable(mediafire-fuse
	$<TARGET_OBJECTS:mfapi>
	$<TARGET_OBJECTS:mfutils>
	fuse/main.c
	fuse/hashtbl.c
	fuse/filecache.c
	fuse/operations.c)
target_link_libraries(mediafire-fuse ${CURL_LIBRARIES} ${OPENSSL_LIBRARIES} ${FUSE_LIBRARIES} ${JANSSON_LIBRARIES})

add_test(iwyu ${CMAKE_SOURCE_DIR}/tests/iwyu.py ${CMAKE_BINARY_DIR})
add_test(indent ${CMAKE_SOURCE_DIR}/tests/indent.sh ${CMAKE_SOURCE_DIR})
add_test(valgrind_fuse ${CMAKE_SOURCE_DIR}/tests/valgrind_fuse.sh ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
add_test(valgrind_shell ${CMAKE_SOURCE_DIR}/tests/valgrind_shell.sh ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})

install (TARGETS mediafire-fuse mediafire-shell DESTINATION bin)
