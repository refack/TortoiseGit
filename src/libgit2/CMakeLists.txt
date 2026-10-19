# The core library, once per CString flavor (modules/shims/str_utils.ixx says why):
#   TGitLibgit2     ATL - UnitTests, and later TortoiseShell / TGitCache
#   TGitLibgit2Mfc  MFC - TortoiseProc
# Both STATIC: the TGitCore.lib of CLAUDE.md's Phase B. As a DLL it exported none
# of our symbols - only libgit2's C API, which a static git2.lib marks dllexport -
# so nothing could link against the module code. gitdll stays the one DLL, and
# that boundary is what keeps libgit.lib off this library's consumers.
set(flavor MFC)

add_library(TGitLibgit2Mfc STATIC)

target_sources("TGitLibgit2Mfc"
    PRIVATE
        patches/filter-filter.c
        patches/ssh-wintunnel.c
        patches/system-call.c
        modules/lanes.cpp   # the lanes module's implementation unit
)

target_include_directories(TGitLibgit2Mfc
    PRIVATE
        "${CMAKE_SOURCE_DIR}/ext/gitdll"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src/SharedMFC"
)

# _AFXDLL comes from the root for every target; the ATL flavor, when it returns,
# has to take it back out (the root define is what the shim's TGIT_MFC split is for).
target_compile_definitions(TGitLibgit2Mfc
    PRIVATE
        $<$<STREQUAL:${flavor},MFC>:TGIT_MFC>
)

target_link_libraries(TGitLibgit2Mfc
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

target_sources(TGitLibgit2Mfc
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
)
