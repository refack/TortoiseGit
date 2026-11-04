add_library(ResizableLib STATIC)

target_sources(ResizableLib
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS
            .

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

target_include_directories(ResizableLib
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ResizableLib
    PRIVATE
        winhttp
        rpcrt4
        gdi32
        crypt32
)
