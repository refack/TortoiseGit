add_executable(GitWCRev)

tgit_app_manifests(GitWCRev)	# the .xml listed here was never embedded (not a .manifest)

target_sources(GitWCRev
    PRIVATE
        GitWCRev.cpp
        GitWCRev.h
        GitWCRev.rc
        Register.cpp
        Register.h
        resource.h
        resourcecom.h
        status.cpp
        status.h
        stdafx.cpp
        stdafx.h
)

target_include_directories(GitWCRev
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(GitWCRev
    PRIVATE
        shlwapi
        winhttp
        rpcrt4
        gdi32
        crypt32
        tgitlib
)
