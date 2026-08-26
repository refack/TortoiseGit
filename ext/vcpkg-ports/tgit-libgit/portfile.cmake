# git's own C sources, built as the static library gitdll links.
#
# This replaces ext\build\libgit.vcxproj, which enumerated 284 source files by
# hand. git ships contrib/buildsystems/CMakeLists.txt - upstream-maintained,
# MSVC-supported - which derives its source list from the Makefile and generates
# the headers that a hand-maintained list kept forgetting (hook-list.h,
# command-list.h, config-list.h). Deriving beats enumerating: every miss in the
# vcxproj was found by a link error rather than by the build noticing.
#
# The base is git-for-windows/git, NOT git/git: compat/win32/fscache.c and the
# rest of the Windows compat layer are not upstream and are compiled here.
#
# TortoiseGit's own changes are the six patches under patches/. Read
# TORTOISEGIT-PATCHES.md before adding a seventh - each one is grouped by what
# would have to be true for it to be deleted, and two of them are close.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO git-for-windows/git
    REF f661476ee4698cf7bfa734b91330048a54d7ee11
    SHA512 910bcb46359dd330cb8b480cdc29f20f2acfcb597c620ada5ea803a4ba1b5c2f4ecf917bc13fcf92880bcbcdd794112e39097ce657ca3d742d5e9b7eee011c33
    HEAD_REF main
    PATCHES
        patches/0001-We-are-a-DLL-not-a-process.patch
        patches/0002-One-process-many-repositories.patch
        patches/0003-Finding-the-user-s-git-installation.patch
        patches/0004-MSVC-build-enablement.patch
        patches/0005-Behaviour-we-deliberately-differ-on.patch
        patches/0006-Upstreamable.patch
)

# The CMakeLists shells out to GIT-VERSION-GEN and the header generators, so it
# needs a real POSIX sh. vcpkg scrubs PATH for the configure step, and its own
# find_program() only looks in the two default Git for Windows install paths, so
# any other install (scoop, winget, portable) fails with "shell interpreter was
# not found".
#
# Derive it from git itself rather than guessing at install layouts: --exec-path
# lands in <root>/mingw64/libexec/git-core, and sh lives at <root>/bin or
# <root>/usr/bin. Resolve a shim to its target first - a shim would be found on
# PATH but is not a shell and mangles the arguments these scripts are called
# with.
vcpkg_find_acquire_program(GIT)
execute_process(
    COMMAND "${GIT}" --exec-path
    OUTPUT_VARIABLE git_exec_path
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE git_exec_path_result
)
if(NOT git_exec_path_result EQUAL 0)
    message(FATAL_ERROR "Could not ask '${GIT}' for its exec-path.")
endif()

set(sh_hints "")
foreach(up IN ITEMS "../.." "../../.." "../../../..")
    get_filename_component(root "${git_exec_path}/${up}" ABSOLUTE)
    list(APPEND sh_hints "${root}/bin" "${root}/usr/bin")
endforeach()

find_program(TGIT_SH_EXE
    NAMES sh
    PATHS ${sh_hints} "C:/Program Files/Git/bin" "$ENV{LOCALAPPDATA}/Programs/Git/bin"
    NO_DEFAULT_PATH
)
if(NOT TGIT_SH_EXE)
    message(FATAL_ERROR
        "No POSIX sh found near '${GIT}'. git's CMake build needs one to run "
        "GIT-VERSION-GEN and the header generators. Looked in: ${sh_hints}")
endif()
message(STATUS "Using shell interpreter: ${TGIT_SH_EXE}")

# USE_VCPKG defaults ON and bootstraps a *second* vcpkg into
# compat/vcbuild/vcpkg. We are already inside one, so the dependencies come from
# the manifest and the toolchain redirects find_package() at them.
#
# The CMakeLists overrides CMAKE_SOURCE_DIR to the git root itself, so the
# configure directory is contrib/buildsystems rather than the tree root.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/contrib/buildsystems"
    OPTIONS
        -DUSE_VCPKG=OFF
        "-DSH_EXE=${TGIT_SH_EXE}"
        # SKIP_DASHED_BUILT_INS is deliberately NOT set. It looks like the right
        # switch for a library-only build, but combined with no CURL it leaves
        # both git_links and git_http_links empty, and CMakeLists.txt:846 then
        # calls add_custom_command(OUTPUT) with nothing to output: "Wrong
        # syntax. A TARGET or OUTPUT must be specified." That is an upstream
        # bug, not a misuse. Leaving it off costs nothing here, because we build
        # only the libgit target and the links target never runs.
        -DPERL_TESTS=OFF
        -DPYTHON_TESTS=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_CURL=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_EXPAT=ON
    MAYBE_UNUSED_VARIABLES
        # vcpkg injects these into every configure; git's CMakeLists reads none
        # of them, and vcpkg treats an unread -D as a hard error unless listed.
        CMAKE_INSTALL_BINDIR
        CMAKE_INSTALL_LIBDIR
        FETCHCONTENT_FULLY_DISCONNECTED
        _VCPKG_ROOT_DIR
)

# Only the library. Building git.exe and the ~150 built-ins would cost minutes
# per configuration and gitdll links none of it.
vcpkg_cmake_build(TARGET libgit LOGFILE_BASE build-libgit)

# git's CMakeLists installs the executables and nothing else - there is no
# install rule for the libgit target, because upstream does not think of it as a
# consumable library. So place it by hand.
foreach(cfg IN ITEMS "rel" "dbg")
    if(cfg STREQUAL "rel")
        set(dest "${CURRENT_PACKAGES_DIR}/lib")
    else()
        set(dest "${CURRENT_PACKAGES_DIR}/debug/lib")
    endif()
    set(built "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${cfg}/libgit.lib")
    if(NOT EXISTS "${built}")
        message(FATAL_ERROR "libgit.lib was not built at ${built}; the libgit target may have been renamed.")
    endif()
    file(INSTALL "${built}" DESTINATION "${dest}")
endforeach()

# gitdll compiles against git's internal headers - it is a shim over this
# library, not a consumer of a public API, and git installs no headers at all.
# Mirror the source tree so `#include "git-compat-util.h"` and
# `#include "odb/source-files.h"` both resolve from one include root, exactly as
# they do inside git's own build.
set(include_root "${CURRENT_PACKAGES_DIR}/include/tgit-libgit")
file(GLOB_RECURSE headers RELATIVE "${SOURCE_PATH}" "${SOURCE_PATH}/*.h")
foreach(header IN LISTS headers)
    # t/ is the test suite and contrib/ is unrelated tooling; neither is ours to ship.
    if(header MATCHES "^(t|contrib|compat/vcbuild/vcpkg)/")
        continue()
    endif()
    get_filename_component(header_dir "${header}" DIRECTORY)
    file(INSTALL "${SOURCE_PATH}/${header}" DESTINATION "${include_root}/${header_dir}")
endforeach()

# The four headers CMake generates. These are the ones a hand-maintained file
# list cannot produce and kept forgetting: hook-list.h in particular is where
# hook_name_list lives, and nothing else declares it.
foreach(generated IN ITEMS "command-list.h" "config-list.h" "hook-list.h" "version-def.h")
    set(built "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/${generated}")
    if(NOT EXISTS "${built}")
        message(FATAL_ERROR "Generated header ${generated} is missing from the build tree.")
    endif()
    file(INSTALL "${built}" DESTINATION "${include_root}")
endforeach()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
