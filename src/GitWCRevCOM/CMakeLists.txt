cmake_minimum_required(VERSION 4.0)
project(GitWCRevCOM LANGUAGES C CXX)

find_package(MFC REQUIRED)

add_executable(GitWCRevCOM)

target_sources(GitWCRevCOM
    PRIVATE
        GitWCRevCOM_h.h
        GitWCRevCOM_i.c
        GitWCRevCOM.cpp
        GitWCRevCOM.h
        GitWCRevCOM.rc
)

set_target_properties(GitWCRevCOM PROPERTIES
    CMAKE_MFC_FLAG 2
)

target_compile_features(GitWCRevCOM
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(GitWCRevCOM
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(GitWCRevCOM
    PRIVATE
        _WIN64
        WIN64
        UNICODE
        _UNICODE
        GIT_DEPRECATE_HARD
        ENABLE_INTSAFE_SIGNED_FUNCTIONS
        _WIN32_WINNT=0x0A00
        _WINDOWS
        WINDOWS
        NOMINMAX
        _AFXDLL
        "$<$<CONFIG:Debug>:_DEBUG>"
        "$<$<CONFIG:Debug>:DEBUG>"
        "$<$<CONFIG:Release>:NDEBUG>"
        "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},x64>:_AMD64_>"
        "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},ARM64>:_ARM64_>"
)

target_link_directories(GitWCRevCOM
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
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

target_compile_options(GitWCRevCOM
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

