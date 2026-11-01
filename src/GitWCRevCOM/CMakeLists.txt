add_executable(GitWCRevCOM)

tgit_midl(GitWCRevCOM GitWCRevCOM.idl TLB)

target_sources(GitWCRevCOM
    PRIVATE
        # GitWCRevCOM_i.c is #included by GitWCRevCOM.cpp, so it is not compiled on its own
        GitWCRevCOM.cpp
        GitWCRevCOM.h
        GitWCRevCOM.rc
)

# the .rc embeds the type library (1 TYPELIB "GitWCRevCOM.tlb"); rc's dependency
# scan does not see binary resources, so the edge is declared here
set_source_files_properties(GitWCRevCOM.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/GitWCRevCOM.tlb")

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
        tgitlib
)