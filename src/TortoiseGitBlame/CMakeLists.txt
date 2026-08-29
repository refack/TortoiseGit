cmake_minimum_required(VERSION 4.0)
project(TortoiseGitBlame LANGUAGES CXX)

find_package(MFC REQUIRED)

add_executable(TortoiseGitBlame)

target_sources(TortoiseGitBlame
    PRIVATE
        BlameDetectMovedOrCopiedLines.h
        BlameIndexColors.h
        EditGotoDlg.cpp
        EditGotoDlg.h
        GitBlameLogList.h
        LogListBlameAction.cpp
        MainFrm.cpp
        MainFrm.h
        MenuEncode.h
        OutputWnd.cpp
        OutputWnd.h
        PropertiesWnd.cpp
        PropertiesWnd.h
        Resource.h
        stdafx.cpp
        stdafx.h
        TortoiseGitBlame.cpp
        TortoiseGitBlame.h
        TortoiseGitBlameData.cpp
        TortoiseGitBlameData.h
        TortoiseGitBlameDoc.cpp
        TortoiseGitBlameDoc.h
        TortoiseGitBlameView.cpp
        TortoiseGitBlameView.h
)

set_target_properties(TortoiseGitBlame PROPERTIES
    CMAKE_MFC_FLAG 2
)

target_compile_features(TortoiseGitBlame
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(TortoiseGitBlame
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${VCPKG_INSTALLED_DIR}/include/scintilla"
        "${VCPKG_INSTALLED_DIR}/include/lexilla"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/gitdll"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Git"
        "${CMAKE_CURRENT_SOURCE_DIR}/../TortoiseProc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Utils"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Utils/MiscUI"
        "${CMAKE_CURRENT_SOURCE_DIR}/../ResizableLib"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Resources"
        "${CMAKE_SOURCE_DIR}/src"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(TortoiseGitBlame
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

target_link_directories(TortoiseGitBlame
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(TortoiseGitBlame
    PRIVATE
        gdiplus
        comctl32
        shlwapi
        UxTheme
        wininet
        detours
        Scintilla
        Lexilla
        imm32
        msimg32
        ole32
        oleaut32
        winhttp
        rpcrt4
        gdi32
        crypt32
        gitdll
        TGitLibgit2
        ResizableLib
)

target_compile_options(TortoiseGitBlame
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

target_link_options(TortoiseGitBlame
    PRIVATE
        /ignore:4099
)

