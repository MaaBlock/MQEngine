#if(NOT DEFINED VCPKG_TARGET_TRIPLET)
#    if(WIN32)
#        set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "Vcpkg target triplet")
#    endif()
#endif()
if(CMAKE_HOST_WIN32 AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android" AND NOT DEFINED CMAKE_ANDROID_API AND NOT DEFINED ANDROID_PLATFORM AND DEFINED ENV{VSINSTALLDIR})
    file(TO_NATIVE_PATH "$ENV{VSINSTALLDIR}" _mq_vs_install_native)
    string(REGEX REPLACE "[\\\\/]+$" "" _mq_vs_install_native "${_mq_vs_install_native}")
    if(EXISTS "${_mq_vs_install_native}\\VC\\Auxiliary\\Build\\vcvarsall.bat")
        string(REPLACE "\\" "\\\\" _mq_vs_install_escaped "${_mq_vs_install_native}")
        set(_mq_overlay_triplet_dir "${CMAKE_BINARY_DIR}/vcpkg_overlay_triplets")
        file(MAKE_DIRECTORY "${_mq_overlay_triplet_dir}")
        file(WRITE "${_mq_overlay_triplet_dir}/x64-windows.cmake"
"set(VCPKG_TARGET_ARCHITECTURE x64)\n"
"set(VCPKG_CRT_LINKAGE dynamic)\n"
"set(VCPKG_LIBRARY_LINKAGE dynamic)\n"
"set(VCPKG_PROVIDED_FORTRAN ON)\n"
"set(VCPKG_VISUAL_STUDIO_PATH \"${_mq_vs_install_escaped}\")\n"
        )
        set(VCPKG_OVERLAY_TRIPLETS "${_mq_overlay_triplet_dir}" CACHE STRING "Overlay triplets binding vcpkg to active VSINSTALLDIR" FORCE)
    endif()
endif()

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android" OR DEFINED CMAKE_ANDROID_API OR DEFINED ANDROID_PLATFORM)
    set(VCPKG_TARGET_ANDROID true)
    set(FCT_ANDROID true)
    include("${CMAKE_CURRENT_LIST_DIR}/vcpkg_android.cmake")
    set(VCPKG_CMAKE_SYSTEM_NAME Android)
endif()
