cmake_minimum_required(VERSION 4.0)
project(TortoiseUDiff LANGUAGES CXX)

find_package(MFC REQUIRED)

add_executable(TortoiseUDiff)

target_sources(TortoiseUDiff
    PRIVATE
        FindBar.cpp
        FindBar.h
        MainWindow.cpp
        MainWindow.h
        resource.h
        stdafx.cpp
        stdafx.h
        TortoiseUDiff.cpp
        TortoiseUDiff.h
        TortoiseUDiff.rc
        UDiffColors.h
)

set_target_properties(TortoiseUDiff PROPERTIES
    CMAKE_MFC_FLAG 2
)

target_compile_features(TortoiseUDiff
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(TortoiseUDiff
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(TortoiseUDiff
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

target_link_directories(TortoiseUDiff
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(TortoiseUDiff
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

target_compile_options(TortoiseUDiff
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

