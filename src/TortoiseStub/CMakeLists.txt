add_library(TortoiseStub SHARED)

target_sources(TortoiseStub
    PRIVATE
        TortoiseStub.cpp
        TortoiseStub.rc
        TortoiseStub.def
)

set_target_properties(TortoiseStub PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_include_directories(TortoiseStub
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../Utils"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
)

target_compile_definitions(TortoiseStub
    PRIVATE
        TORTOISESTUB_EXPORTS
)

target_link_libraries(TortoiseStub
    PRIVATE
        winhttp
        rpcrt4
        gdi32
        crypt32
)
