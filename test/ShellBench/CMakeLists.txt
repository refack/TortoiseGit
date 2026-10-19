add_executable(ShellBench)

target_sources(ShellBench
    PRIVATE
        ShellBench.cpp
)

set_target_properties(ShellBench PROPERTIES
    OUTPUT_NAME ShellTest
)

target_include_directories(ShellBench
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
)

target_compile_definitions(ShellBench
    PRIVATE
        _CONSOLE
)

target_link_libraries(ShellBench
    PRIVATE
        shlwapi
        ole32
        advapi32
        winhttp
        rpcrt4
        gdi32
        crypt32
)
