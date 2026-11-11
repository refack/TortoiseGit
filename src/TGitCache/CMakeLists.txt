add_executable(TGitCache WIN32)
tgit_app_manifests(TGitCache "${TGIT_SYSTEM_DPI_MANIFEST}")

target_sources(TGitCache
    PRIVATE
        CachedDirectory.cpp
        CachedDirectory.h
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

# Shared with TortoiseShell: the cache reads the shell's registry cache (path
# filters, submodule recursion, status display options). ShellCache.cpp does not
# include a stdafx.h of its own - a quoted one would resolve next to the file, to
# TortoiseShell's - so this host force-includes TGitCache's, as TortoiseProc does.
set(TGITCACHE_SHARED_SHELL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/TortoiseShell/ShellCache.cpp"
    "${CMAKE_SOURCE_DIR}/src/TortoiseShell/ShellCache.h"
)
target_sources(TGitCache PRIVATE ${TGITCACHE_SHARED_SHELL_SOURCES})
set_source_files_properties(${TGITCACHE_SHARED_SHELL_SOURCES}
    TARGET_DIRECTORY TGitCache
    PROPERTIES COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/stdafx.h"
)

target_include_directories(TGitCache
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src/TortoiseShell"
        "${CMAKE_SOURCE_DIR}/src/Git"
        "${CMAKE_SOURCE_DIR}/ext/gitdll"
        "${CMAKE_SOURCE_DIR}/src/Utils"
        "${CMAKE_SOURCE_DIR}/src/libgit2/include"         # sanity.h
        "${CMAKE_SOURCE_DIR}/src/libgit2/include/Utils"   # CreateProcessHelper.h, LoadIconEx.h, UniqueQueue.h
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
)

target_compile_definitions(TGitCache
    PRIVATE
        TGITCACHE
        _CONSOLE
)

target_link_libraries(TGitCache
    PRIVATE
        libgit2::libgit2package
        winhttp
        rpcrt4
        gdi32
        crypt32
        gitdll
        tgitlib
)
