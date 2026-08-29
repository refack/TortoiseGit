cmake_minimum_required(VERSION 4.0)
project(TortoiseStub LANGUAGES CXX)

find_package(MFC REQUIRED)

add_library(TortoiseStub SHARED)

target_sources(TortoiseStub
    PRIVATE
        TortoiseStub.cpp
        TortoiseStub.rc
        TortoiseStub.def
)

set_target_properties(TortoiseStub PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CMAKE_MFC_FLAG 2
)

target_compile_features(TortoiseStub
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(TortoiseStub
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../Utils"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(TortoiseStub
    PRIVATE
        TORTOISESTUB_EXPORTS
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

target_link_directories(TortoiseStub
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(TortoiseStub
    PRIVATE
        winhttp
        rpcrt4
        gdi32
        crypt32
)

target_compile_options(TortoiseStub
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

