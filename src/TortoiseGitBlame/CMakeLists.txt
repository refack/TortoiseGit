add_executable(TortoiseGitBlame WIN32)

target_precompile_headers(TortoiseProc PRIVATE stdafx.h)

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

target_include_directories(TortoiseGitBlame
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${VCPKG_INSTALLED_DIR}/include/scintilla"
        "${VCPKG_INSTALLED_DIR}/include/lexilla"
        "${CMAKE_SOURCE_DIR}/src/TortoiseProc"
        "${CMAKE_SOURCE_DIR}/src/TortoiseProc/ProtoModules"
)

target_link_libraries(TortoiseGitBlame
    PRIVATE
        gitdll
        tgitlib
        ResizableLib
        SharedMFC

        detours
        Scintilla
        Lexilla

        gdiplus
        comctl32
        shlwapi
        UxTheme
        wininet
        imm32
        msimg32
        ole32
        oleaut32
        winhttp
        rpcrt4
        gdi32
        crypt32

)

target_link_options(TortoiseGitBlame
    PRIVATE
        /ignore:4099
)
