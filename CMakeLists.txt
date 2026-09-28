cmake_minimum_required(VERSION 4.3)

# before project() — the gate is read when the CXX toolchain is discovered
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
#set(CMAKE_CXX_MODULE_STD ON)          # or per-target: set_target_properties(TGitLibgit2 PROPERTIES CXX_MODULE_STD ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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


add_subdirectory(src/gitdll)
#add_subdirectory(src/AsyncFramework)
#add_subdirectory(src/IBugTraqProvider)
add_subdirectory(src/libgit2)
#add_subdirectory(src/GitWCRev)
#add_subdirectory(src/GitWCRevCOM)
#add_subdirectory(src/ResizableLib)
#add_subdirectory(src/SshAskPass)
#add_subdirectory(src/TGitCache)
#add_subdirectory(src/TortoiseGitBlame)
#add_subdirectory(src/TortoiseProc)
#add_subdirectory(src/TortoiseShell)
#add_subdirectory(src/TortoiseStub)
#add_subdirectory(src/TortoiseUDiff)
#add_subdirectory(test/Cache)
#add_subdirectory(test/ShellBench)
#add_subdirectory(test/UnitTests)
