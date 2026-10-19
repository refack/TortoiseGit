add_executable(TortoiseUDiff)

target_sources(TortoiseUDiff
    PRIVATE
        FindBar.cpp
        FindBar.h
        MainWindow.cpp
        MainWindow.h
        resource.h
        stdafx.cpp
        stdafx.h
        TortoiseUDiff.cpp
        TortoiseUDiff.h
        TortoiseUDiff.rc
        UDiffColors.h
)

target_include_directories(TortoiseUDiff
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/libgit2/include"   # sanity.h
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(TortoiseUDiff
    PRIVATE
        uxtheme
        detours
        Scintilla
        Lexilla
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
