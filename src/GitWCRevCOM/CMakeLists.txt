add_executable(GitWCRevCOM)

target_sources(GitWCRevCOM
    PRIVATE
        GitWCRevCOM_h.h
        GitWCRevCOM_i.c
        GitWCRevCOM.cpp
        GitWCRevCOM.h
        GitWCRevCOM.rc
)

target_include_directories(GitWCRevCOM
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(GitWCRevCOM
    PRIVATE
        shlwapi
        winhttp
        rpcrt4
        gdi32
        crypt32
        TGitLibgit2
)
