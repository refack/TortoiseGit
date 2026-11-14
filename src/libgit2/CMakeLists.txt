add_library(tgitlib STATIC)

target_sources("tgitlib"
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS
            .
        FILES
            tgitlib/sanity.h
            Resources/LoglistCommonResource.h
    PRIVATE
        patches/filter-filter.c
        patches/ssh-wintunnel.c
        patches/system-call.c
)

target_include_directories(tgitlib
    PRIVATE
        "${CMAKE_SOURCE_DIR}/ext/gitdll"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Pure-Win32 window bases: TortoiseUDiff derives from them, TortoiseProc's
# MiscUI widgets may again; no MFC, so they live below the CString wall.
target_sources(tgitlib
    PRIVATE
        tgitlib/BaseDialog.cpp
        tgitlib/BaseDialog.h
        tgitlib/BaseWindow.cpp
        tgitlib/BaseWindow.h
)

target_link_libraries(tgitlib
    PUBLIC
        gitdll
    PRIVATE
        # static, and it carries its private symbols (git__calloc, git_str_*) the
        # patches/*.c link against - which a libgit2 DLL would not export
        libgit2::libgit2package
        detours     # DarkModeHelper hooks OpenNcThemeData
        version     # PathUtils::GetVersionFromFile
        winhttp
        rpcrt4
        gdi32
        crypt32
)

if(CMAKE_CXX_MODULE_STD EQUAL OFF)
    target_link_libraries(tgitlib
        PUBLIC
            std_module_shim
    )
endif ()

target_sources(tgitlib
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/modules"
    FILES
        modules/shims/gsl.ixx
        modules/shims/wil.ixx
        modules/shims/str_utils.ixx
        modules/shims/libgit2.ixx
        modules/shims/json.ixx
        modules/Git.ixx
        modules/GitCall.ixx
        modules/BugTraqAssociations.ixx
        modules/DarkModeHelper.ixx
        modules/DebugOutput.ixx
        modules/DirFileEnum.ixx
        modules/GitDataObject.ixx
        modules/GitHash.ixx
        modules/GitMailmap.ixx
        modules/GitRev.ixx
        modules/GitRevLoglistCore.ixx	# MFC-free; SharedMFC's GitRevLoglist re-exports it, the unit tests import it
        modules/Graphviz.ixx
        modules/Libraries.ixx
        modules/PathUtils.ixx
        modules/PersonalDictionary.ixx
        modules/RIAA.ixx
        modules/RegistryDep.ixx
        modules/RollingFile.ixx
        modules/CacheInterface.ixx
        modules/ShellUpdater.ixx
        modules/TGitPath.ixx
        modules/TempFiles.ixx
        modules/gitindex.ixx
        modules/invarients.ixx
        modules/lanes.ixx
        modules/LangDll.ixx
        modules/GitStatus.ixx
        modules/CmdLineParser.ixx
        modules/DragDropImpl.ixx
        modules/utils/ReaderWriterLock.ixx
        modules/utils/PathWatcher.ixx
        modules/SysInfo.ixx
        modules/Theme.ixx
        modules/ShellEnvironment.ixx
        modules/TGitConstants.ixx
        modules/DPIAware.ixx
        modules/TaskbarUUID.ixx	# MFC-free; TortoiseUDiff imports it too
)
