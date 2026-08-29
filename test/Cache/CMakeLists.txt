cmake_minimum_required(VERSION 4.0)
project(Cache LANGUAGES CXX)

find_package(MFC REQUIRED)

add_executable(Cache)

target_sources(Cache
    PRIVATE
        Cache.cpp
        Cache.h
        Cache.rc
        CacheDlg.cpp
        CacheDlg.h
        Resource.h
        stdafx.cpp
        stdafx.h
)

set_target_properties(Cache PROPERTIES
    CMAKE_MFC_FLAG 2
)

target_compile_features(Cache
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(Cache
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/TortoiseShell"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/gitdll"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Utils"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Git"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/TortoiseProc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Utils/MiscUI"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/TGitCache"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(Cache
    PRIVATE
        _WIN64
        WIN64
        UNICODE
        _UNICODE
        GIT_DEPRECATE_HARD
        ENABLE_INTSAFE_SIGNED_FUNCTIONS
        _WIN32_WINNT=0x0A00
        _WINDOWS
        WINDOWS
        NOMINMAX
        _AFXDLL
        "$<$<CONFIG:Debug>:_DEBUG>"
        "$<$<CONFIG:Debug>:DEBUG>"
        "$<$<CONFIG:Release>:NDEBUG>"
        "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},x64>:_AMD64_>"
        "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},ARM64>:_ARM64_>"
)

target_link_directories(Cache
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(Cache
    PRIVATE
        winhttp
        rpcrt4
        gdi32
        crypt32
        gitdll
        TGitLibgit2
)

target_compile_options(Cache
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

