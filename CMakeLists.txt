cmake_minimum_required(VERSION 4.3)

# before project() — the gate is read when the CXX toolchain is discovered
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
#set(CMAKE_CXX_MODULE_STD ON)          # or per-target: set_target_properties(TGitLibgit2 PROPERTIES CXX_MODULE_STD ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_SARIF ON)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/SARIF")
add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-wd4996>  # depracation TODO: later
    $<$<COMPILE_LANGUAGE:CXX>:-wd4102>  # decorative goto labels
)

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

# we use this abstration breaking hack to hide from CLion
# it needs to be after the `project` declaration
string(APPEND CMAKE_CXX_COMPILE_OBJECT " /experimental:log <OBJECT>.sarif")


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

add_subdirectory(src/gitdll)
add_subdirectory(src/AsyncFramework)
add_subdirectory(src/IBugTraqProvider)
add_subdirectory(src/libgit2)
add_subdirectory(src/GitWCRev)
add_subdirectory(src/GitWCRevCOM)
add_subdirectory(src/ResizableLib)
add_subdirectory(src/SshAskPass)
add_subdirectory(src/TGitCache)
#add_subdirectory(src/TortoiseGitBlame)
add_subdirectory(src/TortoiseProc)
#add_subdirectory(src/TortoiseShell)
add_subdirectory(src/TortoiseStub)
#add_subdirectory(src/TortoiseUDiff)

unset(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
add_subdirectory(test/Cache)
add_subdirectory(test/ShellBench)
add_subdirectory(test/TgitUnitTests)
