cmake_minimum_required(VERSION 4.0)
project(gitdll LANGUAGES C CXX)

find_package(ZLIB REQUIRED)
find_package(MFC REQUIRED)

add_library(gitdll STATIC)

target_sources(gitdll
    PRIVATE
        die.cpp
        gitdll.c
        gitdll.h
        gitdll.rc
        resource.h
)

set_target_properties(gitdll PROPERTIES
    CMAKE_MFC_FLAG 2
)

target_compile_features(gitdll
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(gitdll
    PRIVATE
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tgit-libgit/compat/poll"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tgit-libgit/compat/fnmatch"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tgit-libgit/compat/regex"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tgit-libgit/compat/win32"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tgit-libgit/compat/vcbuild/include"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(gitdll
    PRIVATE
        GITDLL_EXPORTS
        MICROSOFT_WINDOWS_WINBASE_H_DEFINE_INTERLOCKED_CPLUSPLUS_OVERLOADS
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

target_link_directories(gitdll
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(gitdll
    PRIVATE
        ntdll
        ws2_32
        libgit
        advapi32
        user32
        winhttp
        rpcrt4
        gdi32
        crypt32
        ZLIB::ZLIB
)

target_compile_options(gitdll
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

