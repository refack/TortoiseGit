cmake_minimum_required(VERSION 4.0)
project(TortoiseShell LANGUAGES CXX)

find_package(MFC REQUIRED)

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
    CMAKE_MFC_FLAG 2
)

target_compile_features(TortoiseShell
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(TortoiseShell
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src/libgit2"
        "${CMAKE_SOURCE_DIR}/src/Utils"
        "${CMAKE_SOURCE_DIR}/src/Utils/MiscUI"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src/Git"
        "${CMAKE_SOURCE_DIR}/ext/gitdll"
        "${CMAKE_SOURCE_DIR}/src"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(TortoiseShell
    PRIVATE
        TORTOISESHELL
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

target_link_directories(TortoiseShell
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(TortoiseShell
    PUBLIC
        gitdll
        TGitLibgit2
    PRIVATE
        comctl32
        shlwapi
        winhttp
        rpcrt4
        gdi32
        crypt32
)

target_compile_options(TortoiseShell
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

