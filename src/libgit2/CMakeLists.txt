add_library(TGitLibgit2 STATIC)

target_sources("TGitLibgit2"
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS .
        FILES
            include/sanity.h
            Resources/LoglistCommonResource.h
    PRIVATE
        patches/filter-filter.c
        patches/ssh-wintunnel.c
        patches/system-call.c
)

target_include_directories(TGitLibgit2
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/ext/gitdll"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(TGitLibgit2
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

target_sources(TGitLibgit2
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
        modules/GitRevLoglist.ixx
        modules/Graphviz.ixx
        modules/Libraries.ixx
        modules/PathUtils.ixx
        modules/PersonalDictionary.ixx
        modules/RIAA.ixx
        modules/Registry.ixx
        modules/RollingFile.ixx
        modules/ShellUpdater.ixx
        modules/TGitPath.ixx
        modules/TaskbarUUID.ixx
        modules/TempFile.ixx
        modules/gitindex.ixx
        modules/invarients.ixx
        modules/lanes.ixx
        modules/LangDll.ixx
        modules/GitStatus.ixx
        modules/CmdLineParser.ixx
        modules/DragDropImpl.ixx
        modules/utils/ReaderWriterLock.ixx
        modules/utils/PathWatcher.ixx
        modules/CacheInterface.ixx
        modules/SysInfo.ixx
        modules/ShellEnvironment.ixx
)
