add_executable(SshAskPass)

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
        UxTheme
        detours
        winhttp
        rpcrt4
        gdi32
        crypt32
)
