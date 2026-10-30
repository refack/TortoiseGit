add_executable(Cache)

target_sources(Cache
    PRIVATE
        Cache.cpp
        Cache.h
        Cache.rc
        CacheDlg.cpp
        CacheDlg.h
        Resource.h
        stdafx.cpp
        stdafx.h
)

target_include_directories(Cache
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/TortoiseShell"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../ext/gitdll"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Utils"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Git"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/TortoiseProc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Utils/MiscUI"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/TGitCache"
        "${CMAKE_SOURCE_DIR}/src/libgit2/include"   # sanity.h
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(Cache
    PRIVATE
        winhttp
        rpcrt4
        gdi32
        crypt32
        gitdll
        tgitlib
)
