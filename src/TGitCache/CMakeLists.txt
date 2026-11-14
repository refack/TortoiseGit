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

target_include_directories(TGitCache
    PRIVATE
        gitdll
        tgitlib

        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(TGitCache
    PRIVATE
        TGITCACHE
        _CONSOLE
)

target_link_libraries(TGitCache
    PRIVATE
        winhttp
        rpcrt4
        gdi32
        crypt32

        gitdll
        tgitlib
)
