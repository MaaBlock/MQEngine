# Patch 1: Remove -Werror and /WX in CesiumCompilerFlags.cmake
set(FLAGS_FILE "${CESIUM_SRC_DIR}/cmake/CesiumCompilerFlags.cmake")
if(EXISTS "${FLAGS_FILE}")
    file(READ "${FLAGS_FILE}" CONTENT)
    string(FIND "${CONTENT}" "/WX" FOUND_WX)
    if(NOT FOUND_WX EQUAL -1)
        string(REPLACE "-Werror" "" CONTENT "${CONTENT}")
        string(REPLACE "/WX" "" CONTENT "${CONTENT}")
        file(WRITE "${FLAGS_FILE}" "${CONTENT}")
    endif()
endif()

# Patch 2: Patch extern/CMakeLists.txt to use system/vcpkg packages instead of ezvcpkg
set(EXTERN_FILE "${CESIUM_SRC_DIR}/extern/CMakeLists.txt")
if(EXISTS "${EXTERN_FILE}")
    file(READ "${EXTERN_FILE}" CONTENT)
    string(FIND "${CONTENT}" "unofficial-sqlite3" FOUND_SQLITE_PATCH)
    if(FOUND_SQLITE_PATCH EQUAL -1)
        string(REPLACE "git submodule update --init" "git submodule update --init --depth 1" CONTENT "${CONTENT}")
        string(REGEX REPLACE "set\\(EZVCPKG_PACKAGES[^\\)]*\\)" "set(EZVCPKG_PACKAGES \"\")\nset(CESIUM_USE_EZVCPKG OFF)" CONTENT "${CONTENT}")
        string(REPLACE "find_package(SQLite3 REQUIRED)"
"find_package(unofficial-sqlite3 CONFIG REQUIRED)
if(NOT TARGET SQLite::SQLite3)
    add_library(SQLite::SQLite3 ALIAS unofficial::sqlite3::sqlite3)
endif()" CONTENT "${CONTENT}")
        file(WRITE "${EXTERN_FILE}" "${CONTENT}")
    endif()
endif()

# Patch 2b: Patch SqliteCache.cpp sqlite3 declarations directly in CMake
set(SQLITE_CACHE_FILE "${CESIUM_SRC_DIR}/CesiumAsync/src/SqliteCache.cpp")
if(EXISTS "${SQLITE_CACHE_FILE}")
    file(READ "${SQLITE_CACHE_FILE}" CONTENT)
    set(SQLITE_MODIFIED FALSE)
    set(SQLITE_DECLS "extern \"C\" {
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
typedef long long sqlite_int64;
#define SQLITE_OK 0
#define SQLITE_BUSY 5
#define SQLITE_CORRUPT 11
#define SQLITE_DONE 101
#define SQLITE_ROW 100
#define SQLITE_STATIC ((void(*)(void *))0)
#define SQLITE_TRANSIENT ((void(*)(void *))-1)
#define SQLITE_SERIALIZE_NOCOPY 0x001
#define SQLITE_DESERIALIZE_FREEONCLOSE 1
#define SQLITE_DESERIALIZE_RESIZEABLE 2
int sqlite3_open(const char *filename, sqlite3 **ppDb);
int sqlite3_close_v2(sqlite3*);
int sqlite3_exec(sqlite3*, const char *sql, int (*callback)(void*,int,char**,char**), void *, char **errmsg);
void sqlite3_free(void*);
const char *sqlite3_errmsg(sqlite3*);
const char *sqlite3_errstr(int);
int sqlite3_changes(sqlite3*);
int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
int sqlite3_finalize(sqlite3_stmt *pStmt);
int sqlite3_reset(sqlite3_stmt *pStmt);
int sqlite3_clear_bindings(sqlite3_stmt*);
int sqlite3_step(sqlite3_stmt*);
int sqlite3_bind_blob(sqlite3_stmt*, int, const void*, int n, void(*)(void*));
int sqlite3_bind_int(sqlite3_stmt*, int, int);
int sqlite3_bind_int64(sqlite3_stmt*, int, sqlite_int64);
int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int, void(*)(void*));
const void *sqlite3_column_blob(sqlite3_stmt*, int iCol);
int sqlite3_column_bytes(sqlite3_stmt*, int iCol);
int sqlite3_column_int(sqlite3_stmt*, int iCol);
sqlite_int64 sqlite3_column_int64(sqlite3_stmt*, int iCol);
const unsigned char *sqlite3_column_text(sqlite3_stmt*, int iCol);
int sqlite3_threadsafe(void);
}")
    string(FIND "${CONTENT}" "#include <sqlite3.h>" FOUND_INC)
    if(NOT FOUND_INC EQUAL -1)
        string(REPLACE "#include <sqlite3.h>" "${SQLITE_DECLS}" CONTENT "${CONTENT}")
        set(SQLITE_MODIFIED TRUE)
    endif()
    string(FIND "${CONTENT}" "#define SQLITE_CORRUPT" FOUND_CORRUPT)
    if(FOUND_CORRUPT EQUAL -1)
        string(REPLACE "#define SQLITE_BUSY 5" "#define SQLITE_BUSY 5\n#define SQLITE_CORRUPT 11" CONTENT "${CONTENT}")
        set(SQLITE_MODIFIED TRUE)
    endif()
    string(FIND "${CONTENT}" "int sqlite3_changes(sqlite3*);" FOUND_CHANGES)
    if(FOUND_CHANGES EQUAL -1)
        string(REPLACE "const char *sqlite3_errstr(int);" "const char *sqlite3_errstr(int);\nint sqlite3_changes(sqlite3*);" CONTENT "${CONTENT}")
        set(SQLITE_MODIFIED TRUE)
    endif()
    if(SQLITE_MODIFIED)
        file(WRITE "${SQLITE_CACHE_FILE}" "${CONTENT}")
    endif()
endif()

# Patch 3: Add #include <cstdint> to AccessorUtility.h for C++20 compatibility
set(ACCESSOR_UTIL_FILE "${CESIUM_SRC_DIR}/CesiumGltf/include/CesiumGltf/AccessorUtility.h")
if(EXISTS "${ACCESSOR_UTIL_FILE}")
    file(READ "${ACCESSOR_UTIL_FILE}" CONTENT)
    string(FIND "${CONTENT}" "#include <cstdint>" FOUND_POS)
    if(FOUND_POS EQUAL -1)
        string(REPLACE "#pragma once" "#pragma once\n#include <cstdint>" CONTENT "${CONTENT}")
        file(WRITE "${ACCESSOR_UTIL_FILE}" "${CONTENT}")
    endif()
endif()

# Patch 4: Update cmake_minimum_required in extern/libjpeg-turbo/CMakeLists.txt for CMake 4.x compatibility
set(LIBJPEG_CMAKE_FILE "${CESIUM_SRC_DIR}/extern/libjpeg-turbo/CMakeLists.txt")
if(EXISTS "${LIBJPEG_CMAKE_FILE}")
    file(READ "${LIBJPEG_CMAKE_FILE}" CONTENT)
    string(FIND "${CONTENT}" "cmake_minimum_required(VERSION 2.8.12)" FOUND_JPEG_VER)
    if(NOT FOUND_JPEG_VER EQUAL -1)
        string(REPLACE "cmake_minimum_required(VERSION 2.8.12)" "cmake_minimum_required(VERSION 3.5)" CONTENT "${CONTENT}")
        file(WRITE "${LIBJPEG_CMAKE_FILE}" "${CONTENT}")
    endif()
endif()

# Patch 5: Generate KTX-Software lib/version.h directly in CMake without invoking bash/wsl on Windows
set(KTX_VERSION_H "${CESIUM_SRC_DIR}/extern/KTX-Software/lib/version.h")
if(NOT EXISTS "${KTX_VERSION_H}")
    file(WRITE "${KTX_VERSION_H}" "#define LIBKTX_VERSION v4.0\n#define LIBKTX_DEFAULT_VERSION v4.0.__default__\n")
endif()
set(KTX_VERSION_CMAKE "${CESIUM_SRC_DIR}/extern/KTX-Software/cmake/version.cmake")
if(EXISTS "${KTX_VERSION_CMAKE}")
    file(READ "${KTX_VERSION_CMAKE}" CONTENT)
    string(FIND "${CONTENT}" "mkversion" FOUND_MKVER)
    if(NOT FOUND_MKVER EQUAL -1)
        string(REPLACE "COMMAND \${BASH_EXECUTABLE} -c \"\\\"./mkversion\\\" \\\"-o\\\" \\\"version.h\\\" \\\"\${dest_path}\\\"\""
                       "COMMAND \${CMAKE_COMMAND} -E touch \"\${version_h_output}\"" CONTENT "${CONTENT}")
        file(WRITE "${KTX_VERSION_CMAKE}" "${CONTENT}")
    endif()
endif()

# Patch 6: Remove include(CPack) from KTX-Software to prevent packaging failures in nested subproject
set(KTX_CMAKE_FILE "${CESIUM_SRC_DIR}/extern/KTX-Software/CMakeLists.txt")
if(EXISTS "${KTX_CMAKE_FILE}")
    file(READ "${KTX_CMAKE_FILE}" CONTENT)
    string(FIND "${CONTENT}" "include(CPack)" FOUND_CPACK)
    if(NOT FOUND_CPACK EQUAL -1)
        string(REPLACE "include(CPack)" "# include(CPack) -- disabled for MQEngine subbuild" CONTENT "${CONTENT}")
        file(WRITE "${KTX_CMAKE_FILE}" "${CONTENT}")
    endif()
endif()

# Patch 7: Inject X-Cesium-Cache-Key deterministic bounding-volume hash into Cesium3DTilesSelection loaders
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/patch_cesium_cache.py")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/patch_cesium_cache.py" "${CESIUM_SRC_DIR}"
        RESULT_VARIABLE PATCH_CACHE_RESULT
    )
endif()


