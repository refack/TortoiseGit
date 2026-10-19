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
    COMPILE_WARNING_AS_ERROR ""
    LINK_WARNING_AS_ERROR ""
)
