add_executable(SshAskPass WIN32)  # GUI subsystem: entry point is _tWinMain, not main

target_sources(SshAskPass
    PRIVATE
        Resource.h
        SshAskPass.cpp
        SshAskPass.rc
        stdafx.cpp
        stdafx.h
)

target_include_directories(SshAskPass
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src/Utils"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(SshAskPass
    PRIVATE
        tgitlib   # DarkModeHelper, DPIAware, RIAA, Registry; -I for <tgitlib/version.h> in the .rc2
        UxTheme
        detours
        winhttp
        rpcrt4
        gdi32
        crypt32
)
