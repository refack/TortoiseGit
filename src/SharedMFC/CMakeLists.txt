add_library(SharedMFC STATIC)

target_precompile_headers(SharedMFC PRIVATE stdafx.h)

target_sources(SharedMFC
    PRIVATE
        ACEdir/ACEdit.cpp
        ACEdir/ACListWnd.cpp
        ResText/codecvt.cpp
        ResText/POFile.cpp
        ResText/ResModule.cpp
        ResText/ResText.cpp
        ResText/Utils.cpp
        Utils/FileTextLines.cpp
        Utils/SysImageList.cpp
        Utils/LogDlg.cpp
        Utils/LogDlg.h
        Utils/LogDlgFileFilter.cpp
        Utils/LogDlgFileFilter.h
)

target_sources(SharedMFC
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS .
        FILES

)

target_include_directories(SharedMFC
    PUBLIC
        gitdll
        tgitlib
        ResizableLib
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src/TortoiseProc/ProtoModules"
        "${CMAKE_SOURCE_DIR}/src/TortoiseProc/MiscUI"
        "${CMAKE_SOURCE_DIR}/src/TortoiseProc"
)

target_link_libraries(SharedMFC
    PUBLIC
        gitdll
        tgitlib
        ResizableLib

    PRIVATE
        imm32
        msimg32
        ole32
        oleaut32
        comdlg32
        winhttp
        rpcrt4
        gdi32
        crypt32
)

target_sources(SharedMFC
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        BrowseFolder.ixx
        ColumnManager.ixx
        MessageBox.ixx
        Theme.ixx
        GitRevLoglist.ixx
        StandAloneDlg.ixx
        GitLogListBase.ixx
        Tooltip.ixx
        LogDlgFilter.ixx
)
