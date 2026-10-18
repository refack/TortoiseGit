cmake_minimum_required(VERSION 4.4)
project(SharedMFC LANGUAGES CXX)

find_package(MFC REQUIRED)

add_library(SharedMFC STATIC)

target_precompile_headers(SharedMFC PRIVATE stdafx.h)

target_sources(SharedMFC
    PRIVATE
        ACEdir/ACEdit.cpp
        ACEdir/ACListWnd.cpp
        ColumnManager.cpp
        ResText/codecvt.cpp
        ResText/POFile.cpp
        ResText/ResModule.cpp
        ResText/ResText.cpp
        ResText/Utils.cpp
        Utils/FileTextLines.cpp
        Utils/SysImageList.cpp
        Utils/Theme.cpp

)

set_target_properties(SharedMFC PROPERTIES
    CMAKE_MFC_FLAG 2
)

target_compile_features(SharedMFC
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(SharedMFC
    PRIVATE
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/libgit2/include"   # sanity.h
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(SharedMFC
    PRIVATE
        GIT_DEPRECATE_HARD
        ENABLE_INTSAFE_SIGNED_FUNCTIONS

        UNICODE
        _UNICODE

        NOMINMAX        # Make Windows.h sane

        _WIN64
        WIN64
        _WINDOWS
        WINDOWS
        WINVER=_WIN32_WINNT_WIN10
        _WIN32_WINNT=_WIN32_WINNT_WIN10

        _AFXDLL     # use the shared Microsoft Foundation Class (MFC) DLLs instead of statically linking the MFC libraries

        "$<$<CONFIG:Debug>:DEBUG>"
        "$<$<CONFIG:Release>:NDEBUG>"
        "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},x64>:_AMD64_>"
        "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},ARM64>:_ARM64_>"
)

target_link_directories(SharedMFC
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(SharedMFC
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

        gitdll
        TGitLibgit2Mfc
)

target_compile_options(SharedMFC
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

target_sources(SharedMFC
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        BrowseFolder.ixx
        MessageBox.ixx
)