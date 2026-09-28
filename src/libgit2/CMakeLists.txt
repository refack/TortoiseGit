cmake_minimum_required(VERSION 4.3)

# before project() — the gate is read when the CXX toolchain is discovered
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
#set(CMAKE_CXX_MODULE_STD ON)          # or per-target: set_target_properties(TGitLibgit2 PROPERTIES CXX_MODULE_STD ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)

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

find_package(MFC REQUIRED)

add_library(TGitLibgit2 SHARED)

target_sources(TGitLibgit2
    PRIVATE
        patches/filter-filter.c
        patches/ssh-wintunnel.c
        patches/system-call.c
        Utils/accHelper.cpp
        Utils/CmdLineParser.cpp
        Utils/Debug.cpp
        Utils/DragDropImpl.cpp
        Utils/FileTextLines.cpp
        Utils/PathWatcher.cpp
        Utils/ReaderWriterLock.cpp
#        Utils/resource.rc
        Utils/SysImageList.cpp
        Utils/Theme.cpp
        Utils/UnicodeUtils.cpp
)

set_target_properties(TGitLibgit2 PROPERTIES
    CMAKE_MFC_FLAG 2
)

target_compile_features(TGitLibgit2
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(TGitLibgit2
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src/gitdll"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(TGitLibgit2
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
        "$<$<CONFIG:Debug>:DEBUG>"
        "$<$<CONFIG:Release>:NDEBUG>"
        "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},x64>:_AMD64_>"
        "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},ARM64>:_ARM64_>"
)

target_link_directories(TGitLibgit2
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(TGitLibgit2
    PUBLIC
        gitdll
    PRIVATE
        winhttp
        rpcrt4
        gdi32
        crypt32
)

target_compile_options(TGitLibgit2
    PRIVATE
        /W3
        /external:W0
        /external:anglebrackets
)

set_target_properties(TGitLibgit2 PROPERTIES CXX_MODULE_STD ON)

target_sources(TGitLibgit2
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/modules"
    FILES
        modules/shims/gsl.ixx
        modules/shims/wil.ixx
        modules/BugTraqAssociations.ixx
        modules/DarkModeHelper.ixx
        modules/DebugOutput.ixx
        modules/DirFileEnum.ixx
        modules/FormatMessageWrapper.ixx
        modules/GitAdminDir.ixx
        modules/GitDataObject.ixx
        modules/GitHash.ixx
        modules/GitMailmap.ixx
        modules/GitRev.ixx
        modules/GitRevLogList.ixx
        modules/Graphviz.ixx
        modules/Libraries.ixx
        modules/PathUtils.ixx
        modules/PersonalDictionary.ixx
        modules/RIAA.ixx
        modules/Registry.ixx
        modules/ShellUpdater.ixx
        modules/SmartLibgit2Ref.ixx
        modules/StringUtils.ixx
        modules/TGitPath.ixx
        modules/TaskbarUUID.ixx
        modules/TempFile.ixx
        modules/gitindex.ixx
        modules/tgittypes.ixx
        modules/invarients.ixx
        modules/wstr.ixx
        modules/lanes.ixx
        modules/LangDll.ixx
)
