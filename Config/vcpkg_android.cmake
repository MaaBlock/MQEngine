if (VCPKG_TARGET_ANDROID)

    if (NOT DEFINED ENV{ANDROID_NDK_HOME} AND NOT DEFINED ANDROID_NDK)
        message(FATAL_ERROR "
        Please set an environment variable ANDROID_NDK_HOME
        For example:
        export ANDROID_NDK_HOME=/home/your-account/Android/Sdk/ndk-bundle
        ")
    endif()

    if (NOT DEFINED ENV{VCPKG_ROOT})
        message(FATAL_ERROR "
        Please set an environment variable VCPKG_ROOT
        For example:
        export VCPKG_ROOT=/path/to/vcpkg
        ")
    endif()

    if (ANDROID_ABI MATCHES "arm64-v8a")
        set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "" FORCE)
    elseif(ANDROID_ABI MATCHES "armeabi-v7a")
        set(VCPKG_TARGET_TRIPLET "arm-android" CACHE STRING "" FORCE)
    elseif(ANDROID_ABI MATCHES "x86_64")
        set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "" FORCE)
    elseif(ANDROID_ABI MATCHES "x86")
        set(VCPKG_TARGET_TRIPLET "x86-android" CACHE STRING "" FORCE)
    else()
        message(FATAL_ERROR "
        Please specify ANDROID_ABI
        Possible ABIs are: arm64-v8a, armeabi-v7a, x86_64, x86
        ")
    endif()
    message(STATUS "vcpkg_android.cmake: VCPKG_TARGET_TRIPLET was set to ${VCPKG_TARGET_TRIPLET}")

    if (DEFINED ENV{ANDROID_NDK_HOME})
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake")
    else()
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
    endif()
    set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "" FORCE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    message(STATUS "vcpkg_android.cmake: CMAKE_TOOLCHAIN_FILE was set to ${CMAKE_TOOLCHAIN_FILE}")
    message(STATUS "vcpkg_android.cmake: VCPKG_CHAINLOAD_TOOLCHAIN_FILE was set to ${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")

endif(VCPKG_TARGET_ANDROID)
