cmake_minimum_required(VERSION 4.0)
project(IBugTraqProvider LANGUAGES C)

find_package(MFC REQUIRED)

add_executable(IBugTraqProvider)

target_sources(IBugTraqProvider
    PRIVATE
        dlldata.c
        IBugTraqProvider_h.h
        IBugTraqProvider_i.c
        IBugTraqProvider_p.c
)

set_target_properties(IBugTraqProvider PROPERTIES
    MSVC_RUNTIME_LIBRARY ""
    MSVC_RUNTIME_CHECKS ""
    CMAKE_MFC_FLAG ""
    COMPILE_WARNING_AS_ERROR ""
    LINK_WARNING_AS_ERROR ""
)

