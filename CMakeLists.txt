cmake_minimum_required(VERSION 4.4)

# before project() — the gate is read when the CXX toolchain is discovered
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_MFC_FLAG 2)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

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


#set(CMAKE_C_COMPILER_LAUNCHER   "${CMAKE_CURRENT_LIST_DIR}/cl-shim.cmd")
#set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_CURRENT_LIST_DIR}/cl-shim.cmd")



project(TortoiseGit)

# One meta-application: everything below applies to every target. The
# per-directory CMakeLists.txt files were mechanically translated from the
# vcxproj files and each repeated all of it; now they only describe their target.
find_package(MFC REQUIRED)
find_package(ZLIB REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(ogdf CONFIG REQUIRED)

add_compile_options(
    -utf-8
    -W3
    -external:anglebrackets
    -external:W0
    $<$<COMPILE_LANGUAGE:CXX>:-wd4102>  # decorative goto labels
    $<$<COMPILE_LANGUAGE:CXX>:-wd4996>  # depracation TODO: later
)

# `import std;` for every target. CMake finds MSVC's std.ixx by itself; for
# clang-cl it only knows libc++ and libstdc++, so a clang-cl tree is told where
# the MSVC STL modules are - the file an MSVC tree has already generated:
#   -DCMAKE_CXX_STDLIB_MODULES_JSON=<msvc tree>/CMakeFiles/<cmake version>/msvcstl.modules.json
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_MODULE_STD ON)
#    add_compile_options("-experimental:log<OBJECT>.sarif")     # broken
#    string(APPEND CMAKE_CXX_COMPILE_OBJECT " -experimental:log<OBJECT>.sarif")     # obsolete by shim
endif()



add_compile_definitions(
    _WIN64
    WIN64
    UNICODE
    _UNICODE
    _WINDOWS
    WINDOWS
    _WIN32_WINNT=0x0A00
    NOMINMAX            # make Windows.h sane
    _AFXDLL             # the shared MFC DLLs, matching the triplet's dynamic CRT
    GIT_DEPRECATE_HARD
    ENABLE_INTSAFE_SIGNED_FUNCTIONS

    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},x64>:_AMD64_>
    $<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},ARM64>:_ARM64_>
)

# The vcpkg toolchain adds neither. Its include directory only reaches a target
# through an imported target's interface (libgit2::libgit2package carries it), so
# targets that link none of those - GitWCRev, SharedMFC, test/Cache - would lose
# git2.h and gsl/. SYSTEM makes it -external:I, which is what -external:W0 is for.
# The import libraries are needed because targets link vcpkg libraries by bare
# name (detours, libgit, OGDF, Scintilla ...).
include_directories(SYSTEM "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
link_directories(
    "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
    "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)


# The application's executables and DLLs share one directory, as the MSBuild
# layout's bin\ did: TortoiseGitProc loads gitdll.dll and starts TGitCache.exe,
# SshAskPass.exe and its siblings from its own directory. Tests keep their own
# output directories (they copy $<TARGET_RUNTIME_DLLS> next to themselves).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# The manifests the vcxproj layer gave every application: TortoiseGit.common.props
# merged src\compatibility.manifest.xml (supportedOS Windows 10/11) into all of
# them, and each project's EnableDpiAwareness=true added a *system*-aware
# <dpiAware>true</dpiAware> (TortoiseShell alone was per-monitor). CMake embeds a
# source only when its extension is .manifest, so the shared .xml is copied to one
# - listing the .xml itself is silently ignored. Per-monitor-v2 is a later,
# separate step: MFC and ResizableLib lay out in pixels and do not handle
# WM_DPICHANGED, so declaring it renders larger, not sharper.
configure_file(src/compatibility.manifest.xml "${CMAKE_BINARY_DIR}/manifests/compatibility.manifest" COPYONLY)
function(tgit_app_manifests target)
    target_sources(${target} PRIVATE "${CMAKE_BINARY_DIR}/manifests/compatibility.manifest" ${ARGN})
endfunction()
set(TGIT_SYSTEM_DPI_MANIFEST "${CMAKE_SOURCE_DIR}/src/systemDpiAware.manifest")


add_subdirectory(ext/gitdll)
add_subdirectory(ext/AsyncFramework)

add_subdirectory(src/IBugTraqProvider)
add_subdirectory(src/libgit2)
add_subdirectory(src/GitWCRev)
add_subdirectory(src/GitWCRevCOM)

add_subdirectory(ext/ResizableLib)
add_subdirectory(src/SharedMFC)
add_subdirectory(src/SshAskPass)
add_subdirectory(src/TGitCache)
add_subdirectory(src/TortoiseProc)
add_subdirectory(src/TortoiseStub)

#add_subdirectory(src/TortoiseGitBlame)
#add_subdirectory(src/TortoiseShell)
#add_subdirectory(src/TortoiseUDiff)


string(APPEND CMAKE_RUNTIME_OUTPUT_DIRECTORY "/tests")
add_subdirectory(test/Cache)
add_subdirectory(test/ShellBench)
add_subdirectory(test/TgitUnitTests)


string(TIMESTAMP CURRENT_TIME "%Y-%m-%d %H:%M:%S")
message(STATUS "configure timestamp: ${CURRENT_TIME}")
