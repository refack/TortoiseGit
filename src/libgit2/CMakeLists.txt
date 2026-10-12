cmake_minimum_required(VERSION 4.3)

# before project() — the gate is read when the CXX toolchain is discovered
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
#set(CMAKE_CXX_MODULE_STD ON)          # or per-target: set_target_properties(TGitLibgit2 PROPERTIES CXX_MODULE_STD ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)

set(CMAKE_EXPORT_SARIF ON)

# The triplet is a project decision, not a per-machine one: static libraries
# over the dynamic CRT, which is what UseOfMfc=Dynamic and every target's
# MSVC_RUNTIME_LIBRARY MultiThreadedDLL already assume. It has to be set before
# project(), because that is when the vcpkg toolchain reads it and runs the
# manifest install. The toolchain FORCE-lifts this value into the cache, so a
# build directory configured under another triplet corrects itself on the next
# configure - and a -DVCPKG_TARGET_TRIPLET on the command line does not win;
# change this line instead.
#
# Left alone, the toolchain derives the triplet from the compiler's platform
# and lands on plain x64-windows - the *dynamic* triplet, under which every
# vcpkg library becomes a DLL. VCPKG_DEFAULT_TRIPLET in the environment does
# not change that; the toolchain never reads it.
set(VCPKG_HOST_TRIPLET "x64-windows-static-md" CACHE STRING "" FORCE)
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "" FORCE)

project(TGitLibgit2 LANGUAGES C CXX)

find_package(libgit2 CONFIG REQUIRED)



# The core library, once per CString flavor (include/tgit_cstring.h says why):
#   TGitLibgit2     ATL - UnitTests, and later TortoiseShell / TGitCache
#   TGitLibgit2Mfc  MFC - TortoiseProc
# Both STATIC: the TGitCore.lib of CLAUDE.md's Phase B. As a DLL it exported none
# of our symbols - only libgit2's C API, which a static git2.lib marks dllexport -
# so nothing could link against the module code. gitdll stays the one DLL, and
# that boundary is what keeps libgit.lib off this library's consumers.
function(tgit_core_library name flavor)
    add_library(${name} STATIC)

    target_sources(${name}
        PRIVATE
            patches/filter-filter.c
            patches/ssh-wintunnel.c
            patches/system-call.c
            Utils/Theme.cpp
            modules/lanes.cpp   # the lanes module's implementation unit
    )
    if(flavor STREQUAL "MFC")
        # MFC-only sources: CStdioFile, CImageList. They used to be compiled into
        # the ATL library too, an MFC object inside an ATL-flavor library.
        target_sources(${name}
            PRIVATE
                Utils/FileTextLines.cpp
                Utils/SysImageList.cpp
        )
        # PRIVATE: only the str_utils shim tests it, and the shim is compiled once,
        # here. A consumer that wanted to test it would be forking outside the shim.
        target_compile_definitions(${name} PRIVATE TGIT_MFC)
    endif()

    target_compile_features(${name}
        PRIVATE
            cxx_std_23
            c_std_23
    )

    target_include_directories(${name}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/src/gitdll"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/src"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
    )

    target_compile_definitions(${name}
        PRIVATE
            _WIN64
            WIN64
            UNICODE
            _UNICODE
            GIT_DEPRECATE_HARD
            ENABLE_INTSAFE_SIGNED_FUNCTIONS
            _WIN32_WINNT=0x0A00
            _WINDOWS
            WINDOWS
            NOMINMAX
            _AFXDLL
            # Compiled in for every consumer. With headers, TGitPath.cpp was built per
            # project and only TortoiseProc defined this - so CTGitPath's layout
            # differed between binaries. A module is compiled once: one answer.
            TGIT_LFS
            "$<$<CONFIG:Debug>:DEBUG>"
            "$<$<CONFIG:Release>:NDEBUG>"
            "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},x64>:_AMD64_>"
            "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},ARM64>:_ARM64_>"
    )

    target_link_directories(${name}
        PRIVATE
            "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
    )

    target_link_libraries(${name}
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

    target_compile_options(${name}
        PRIVATE
            /W3
            /external:W0
            /external:anglebrackets
    )

    set_target_properties(${name} PROPERTIES CXX_MODULE_STD ON)

    target_sources(${name}
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
            modules/SmartLibgit2Ref.ixx
            modules/TGitPath.ixx
            modules/TaskbarUUID.ixx
            modules/TempFile.ixx
            modules/gitindex.ixx
            modules/tgittypes.ixx
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
endfunction()

tgit_core_library(TGitLibgit2 ATL)
tgit_core_library(TGitLibgit2Mfc MFC)