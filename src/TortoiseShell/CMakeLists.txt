add_library(TortoiseShell SHARED)

set_target_properties(TortoiseShell PROPERTIES
    OUTPUT_NAME TortoiseGit
)

target_include_directories(TortoiseShell
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(TortoiseShell
    PRIVATE
        TORTOISESHELL
)

target_link_libraries(TortoiseShell
    PUBLIC
        gitdll
        tgitlib
        SharedMFC
    PRIVATE
        comctl32
        shlwapi
        winhttp
        rpcrt4
        gdi32
        crypt32
)

target_precompile_headers(TortoiseShell PRIVATE stdafx_pch.h)

target_sources(TortoiseShell
    PRIVATE
        AdminDirCache.h
        ContextMenu.cpp
        ExplorerCommand.cpp
        ExplorerCommand.h
        GitFolderStatus.cpp
        GitFolderStatus.h
        GITPropertyPage.cpp
        GITPropertyPage.h
        Guids.h
        IconOverlay.cpp
        RemoteCacheLink.cpp
        RemoteCacheLink.h
        resource.h
        resourceshell.rc
        ShellExt.cpp
        ShellExt.h
        ShellExtClassFactory.cpp
        ShellExtClassFactory.h
        ShellObjects.cpp
        ShellObjects.h
        ShellState.cpp
        ShellState.h
        stdafx.cpp
        stdafx.h
        TortoiseGIT.cpp
        ShellExt.def
)


target_sources(TortoiseShell
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        PreserveChdir.ixx
)
