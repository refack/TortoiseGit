cmake_minimum_required(VERSION 4.4)
project(ResizableLib LANGUAGES CXX)

find_package(MFC REQUIRED)

add_library(ResizableLib STATIC)

target_sources(ResizableLib
    PRIVATE
        ResizableComboBox.cpp
        ResizableComboBox.h
        ResizableComboLBox.cpp
        ResizableComboLBox.h
        ResizableDialog.cpp
        ResizableDialog.h
        ResizableFormView.cpp
        ResizableFormView.h
        ResizableFrame.cpp
        ResizableFrame.h
        ResizableGrip.cpp
        ResizableGrip.h
        ResizableLayout.cpp
        ResizableLayout.h
        ResizableMDIChild.cpp
        ResizableMDIChild.h
        ResizableMDIFrame.cpp
        ResizableMDIFrame.h
        ResizableMinMax.cpp
        ResizableMinMax.h
        ResizableMsgSupport.cpp
        ResizableMsgSupport.h
        ResizablePage.cpp
        ResizablePage.h
        ResizablePageEx.cpp
        ResizablePageEx.h
        ResizableSheet.cpp
        ResizableSheet.h
        ResizableSheetEx.cpp
        ResizableSheetEx.h
        ResizableSheetState.cpp
        ResizableSheetState.h
        ResizableSplitterWnd.cpp
        ResizableSplitterWnd.h
        ResizableState.cpp
        ResizableState.h
        ResizableVersion.cpp
        ResizableVersion.h
        ResizableWndState.cpp
        ResizableWndState.h
        StdAfx.cpp
        StdAfx.h
)

set_target_properties(ResizableLib PROPERTIES
    CMAKE_MFC_FLAG 2
)

target_compile_features(ResizableLib
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(ResizableLib
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/SharedMFC"       # Utils/DPIAware.h
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(ResizableLib
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

target_link_directories(ResizableLib
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(ResizableLib
    PRIVATE
        winhttp
        rpcrt4
        gdi32
        crypt32
)

target_compile_options(ResizableLib
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

