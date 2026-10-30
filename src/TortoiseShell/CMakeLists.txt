add_library(TortoiseShell SHARED)

target_sources(TortoiseShell
    PRIVATE
        ContextMenu.cpp
        ExplorerCommand.cpp
        ExplorerCommand.h
        GitFolderStatus.cpp
        GitFolderStatus.h
        GITPropertyPage.cpp
        GITPropertyPage.h
        Globals.h
        Guids.h
        IconOverlay.cpp
        ItemIDList.cpp
        ItemIDList.h
        MenuInfo.cpp
        MenuInfo.h
        PIDL.cpp
        PIDL.h
        PreserveChdir.cpp
        PreserveChdir.h
        RemoteCacheLink.cpp
        RemoteCacheLink.h
        resource.h
        resourceshell.rc
        ShellCache.cpp
        ShellCache.h
        ShellExt.cpp
        ShellExt.h
        ShellExtClassFactory.cpp
        ShellExtClassFactory.h
        ShellObjects.cpp
        ShellObjects.h
        stdafx.cpp
        stdafx.h
        TortoiseGIT.cpp
        ShellExt.def
)

set_target_properties(TortoiseShell PROPERTIES
    OUTPUT_NAME TortoiseGit
)

target_include_directories(TortoiseShell
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src/libgit2"
        "${CMAKE_SOURCE_DIR}/src/libgit2/include"   # sanity.h
        "${CMAKE_SOURCE_DIR}/src/Utils"
        "${CMAKE_SOURCE_DIR}/src/Utils/MiscUI"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src/Git"
        "${CMAKE_SOURCE_DIR}/ext/gitdll"
        "${CMAKE_SOURCE_DIR}/src"
)

target_compile_definitions(TortoiseShell
    PRIVATE
        TORTOISESHELL
)

target_link_libraries(TortoiseShell
    PUBLIC
        gitdll
        tgitlib
    PRIVATE
        comctl32
        shlwapi
        winhttp
        rpcrt4
        gdi32
        crypt32
)

# MenuInfo.cpp and ShellCache.cpp are also compiled by TortoiseProc (the settings
# pages), so they do not #include "stdafx.h" themselves: each host force-includes
# its own. See src/TortoiseProc/CMakeLists.txt for the other half.
set_source_files_properties(MenuInfo.cpp ShellCache.cpp
    TARGET_DIRECTORY TortoiseShell
    PROPERTIES COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/stdafx.h"
)
