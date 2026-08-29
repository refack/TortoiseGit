cmake_minimum_required(VERSION 4.0)
project(TGitCache LANGUAGES CXX)

find_package(libgit2 REQUIRED CONFIG)
find_package(MFC REQUIRED)

add_executable(TGitCache WIN32)

target_sources(TGitCache
    PRIVATE
        CachedDirectory.cpp
        CachedDirectory.h
        CacheInterface.cpp
        CacheInterface.h
        DirectoryWatcher.cpp
        DirectoryWatcher.h
        FolderCrawler.cpp
        FolderCrawler.h
        GITStatusCache.cpp
        GitStatusCache.h
        resource.h
        ShellUpdater.cpp
        ShellUpdater.h
        StatusCacheEntry.cpp
        StatusCacheEntry.h
        stdafx.cpp
        stdafx.h
        TGITCache.cpp
        TGitCache.h
        TGitCache.rc
)

set_target_properties(TGitCache PROPERTIES
    CMAKE_MFC_FLAG 2
)

target_compile_features(TGitCache
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(TGitCache
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src/TortoiseShell"
        "${CMAKE_SOURCE_DIR}/src/Git"
        "${CMAKE_SOURCE_DIR}/ext/gitdll"
        "${CMAKE_SOURCE_DIR}/src/Utils"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(TGitCache
    PRIVATE
        TGITCACHE
        _CONSOLE
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

target_link_directories(TGitCache
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(TGitCache
    PRIVATE
        libgit2::libgit2package
        winhttp
        rpcrt4
        gdi32
        crypt32
        gitdll
        TGitLibgit2
)

target_compile_options(TGitCache
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

