add_library(AsyncFramework STATIC)

target_sources(AsyncFramework
    PRIVATE
        AsyncCall.h
        CriticalSection.cpp
        CriticalSection.h
        Future.h
        IJob.h
        JobBase.cpp
        JobBase.h
        JobScheduler.cpp
        JobScheduler.h
        SchedulerSuspension.h
        stdafx.cpp
        stdafx.h
        Thread.cpp
        Thread.h
        WaitableEvent.cpp
        WaitableEvent.h
)

target_include_directories(AsyncFramework
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(AsyncFramework
    PRIVATE
        winhttp
        rpcrt4
        gdi32
        crypt32
)
