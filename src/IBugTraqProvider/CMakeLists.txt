# The bug-tracker COM interface, a static library as its vcxproj was: TortoiseProc
# links it for the MIDL-generated header; the IIDs and proxy/stub code ride along.
add_library(IBugTraqProvider STATIC)

tgit_midl(IBugTraqProvider IBugTraqProvider.idl PROXY)

target_sources(IBugTraqProvider
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/dlldata.c"
        "${CMAKE_CURRENT_BINARY_DIR}/IBugTraqProvider_i.c"
        "${CMAKE_CURRENT_BINARY_DIR}/IBugTraqProvider_p.c"
)

set_target_properties(IBugTraqProvider PROPERTIES
    MSVC_RUNTIME_LIBRARY ""
    MSVC_RUNTIME_CHECKS ""
    COMPILE_WARNING_AS_ERROR ""
    LINK_WARNING_AS_ERROR ""
)