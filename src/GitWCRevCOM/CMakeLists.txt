add_executable(GitWCRevCOM WIN32)	# _tWinMain: the vcxproj left SubSystem to the linker, which picked it from the entry point

tgit_app_manifests(GitWCRevCOM)
tgit_midl(GitWCRevCOM GitWCRevCOM.idl TLB)

target_sources(GitWCRevCOM
    PRIVATE
        # GitWCRevCOM_i.c is #included by GitWCRevCOM.cpp, so it is not compiled on its own
        GitWCRevCOM.cpp
        GitWCRevCOM.h
        GitWCRevCOM.rc
        # shared with GitWCRev, as the vcxproj compiled them when both lived in src/GitWCRev
        ../GitWCRev/Register.cpp
        ../GitWCRev/Register.h
        ../GitWCRev/status.cpp
        ../GitWCRev/status.h
)

# the .rc embeds the type library (1 TYPELIB "GitWCRevCOM.tlb"); rc's dependency
# scan does not see binary resources, so the edge is declared here
set_source_files_properties(GitWCRevCOM.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/GitWCRevCOM.tlb")

target_include_directories(GitWCRevCOM
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src/GitWCRev"   # resourcecom.h, GitWCRev.rc2, GitWCRev.ico for the .rc
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
