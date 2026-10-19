# SHARED, never STATIC: the DLL boundary is what keeps libgit.lib (real git) off
# every consumer's link line - a consumer that also links git2.lib would get two
# implementations of odb/refs/packfiles and first-match collisions. A static
# library would hand its PRIVATE libgit on to whoever links it.
add_library(gitdll SHARED)

target_sources(gitdll
    PRIVATE
        die.cpp
        gitdll.c
        gitdll.h
        gitdll.rc
        resource.h
)

# 1. Declare public headers and base include directory
target_sources(gitdll
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS .
        FILES gitdll.h
)

# 2. Install the target and its file sets
install(TARGETS gitdll
    EXPORT tgit_ext
    FILE_SET HEADERS DESTINATION .
)

target_include_directories(gitdll
    PRIVATE
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tgit-libgit/compat/poll"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tgit-libgit/compat/fnmatch"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tgit-libgit/compat/regex"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tgit-libgit/compat/win32"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tgit-libgit/compat/vcbuild/include"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(gitdll
    PRIVATE
        GITDLL_EXPORTS
        MICROSOFT_WINDOWS_WINBASE_H_DEFINE_INTERLOCKED_CPLUSPLUS_OVERLOADS
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
