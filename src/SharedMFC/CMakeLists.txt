cmake_minimum_required(VERSION 4.0)
project(SharedMFC LANGUAGES CXX)

find_package(MFC REQUIRED)

add_library(SharedMFC)

target_sources(SharedMFC
    PRIVATE
        ACEdir/ACEdit.cpp
        ACEdir/ACListWnd.cpp
        ColumnManager.cpp
        ResText/codecvt.cpp
        ResText/POFile.cpp
        ResText/ResModule.cpp
        ResText/ResText.cpp
        ResText/stdafx.cpp
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
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/libgit2/include"   # sanity.h
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(SharedMFC
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

        TGIT_MFC

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

target_compile_options(SharedMFC
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

