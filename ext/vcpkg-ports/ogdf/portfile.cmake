# OGDF has no port in the public vcpkg registry (checked 2026-08-25; the nearest
# graph-layout ports are graphviz, which is EPL-1.0 and so cannot be linked into
# a GPL-2 binary, and igraph, which would be a rewrite of the RevisionGraph
# adapter for equivalent output). So this overlay is the whole port, not a patch
# layer over a stock one -- unlike ext/vcpkg-ports/libgit2.
#
# It replaces ext/build/ogdf.vcxproj, which hand-listed 157 .cpp files (249 of
# them COIN-OR/Clp) and carried a hand-written substitute for the header CMake
# generates. Upstream's own CMake build is used as-is apart from one line -- see
# cache-debug-postfix.diff, which is a packaging fix rather than a TortoiseGit
# behaviour change, so this port should be donatable to microsoft/vcpkg intact.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ogdf/ogdf
    # Pinned to the commit ext/OGDF was vendored at: 19 commits past the
    # foxglove-202510 tag. Retiring the submodule is the point of this port, so
    # the commit has to live here rather than in .gitmodules.
    REF 17f045b131851f5d32af184d5a7a864cec2bfc27
    SHA512 fcef316193adb1b8775bbdf1528eaef7c96fb061b809c869c83032b654f137d23a8c8a3da526e3dec32683999a8c0f35ea79bc1600733215bfb88d174c27c266
    HEAD_REF master
    PATCHES
        cache-debug-postfix.diff
)

# These mirror ext/build/ogdf/ogdf/basic/internal/config_autogen.h, which is the
# source of truth for how TortoiseGit has always built OGDF. Every one of them is
# also the upstream default -- they are pinned rather than inherited so a change
# in upstream's defaults shows up as a diff here instead of silently.
#   OGDF_MEMORY_MANAGER=POOL_TS   -> #define OGDF_MEMORY_POOL_TS
#   COIN_SOLVER=CLP               -> #define COIN_OSI_CLP (and builds src/coin/Clp)
#   OGDF_USE_ASSERT_EXCEPTIONS=OFF-> assertions abort rather than throw
#   OGDF_DEBUG_MODE=REGULAR       -> no OGDF_HEAVY_DEBUG
#   OGDF_INCLUDE_CGAL=OFF         -> src/ogdf/geometric is not built (needs CGAL+OpenMP)
#
# CMAKE_DEBUG_POSTFIX is cleared because upstream sets it to "-debug" and vcpkg
# separates the two builds by directory instead; leaving it would give consumers
# an OGDF-debug.lib that no $(VcpkgLibSuffix) convention in the tree matches.
#
# Doxygen is disabled explicitly: with it found *and* DOC_INSTALL on, doc.cmake
# adds a doc-check target to ALL, which would put documentation generation in the
# library build.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DOGDF_MEMORY_MANAGER=POOL_TS
        -DCOIN_SOLVER=CLP
        -DOGDF_USE_ASSERT_EXCEPTIONS=OFF
        -DOGDF_DEBUG_MODE=REGULAR
        -DOGDF_INCLUDE_CGAL=OFF
        -DOGDF_WARNING_ERRORS=OFF
        -DOGDF_SEPARATE_TESTS=OFF
        -DOGDF_ENABLE_CLANG_TIDY=OFF
        -DCMAKE_DEBUG_POSTFIX=
        -DCMAKE_DISABLE_FIND_PACKAGE_Doxygen=ON
        -DDOC_INSTALL=OFF
    MAYBE_UNUSED_VARIABLES
        DOC_INSTALL
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME ogdf CONFIG_PATH share/ogdf)

# config_autogen.h is generated per build type, into include/ogdf-<type>/, and
# the only line that differs between them is OGDF_DEBUG. vcpkg discards
# debug/include entirely, so shipping the release copy as-is would compile a
# Debug consumer without OGDF_DEBUG against a debug OGDF.lib that has it -- an
# ODR mismatch, silent at link time.
#
# Derive the flag from NDEBUG instead, exactly as the hand-written header this
# port replaces did, and put the result where the #include <ogdf/basic/internal/
# config_autogen.h> in 18 public headers resolves it from a plain include/ path.
# That is also what lets consumers drop the second include directory the vcxproj
# needed ($(ProjectDir)ogdf).
set(_autogen "${CURRENT_PACKAGES_DIR}/include/ogdf-release/ogdf/basic/internal/config_autogen.h")
if(NOT EXISTS "${_autogen}")
    message(FATAL_ERROR "OGDF did not generate ${_autogen}; upstream changed where create_autogen_header() writes.")
endif()

file(READ "${_autogen}" _autogen_text)
if(NOT _autogen_text MATCHES "/\\* #undef OGDF_DEBUG \\*/")
    message(FATAL_ERROR "config_autogen.h was generated with OGDF_DEBUG already set; the release build should not define it.")
endif()
string(REPLACE
    "/* #undef OGDF_DEBUG */"
    "/* Set from NDEBUG by the TortoiseGit overlay port, so one installed header\n   serves both the debug and the release OGDF library. */\n#if !defined(NDEBUG)\n\t#define OGDF_DEBUG\n#endif"
    _autogen_text "${_autogen_text}")
file(WRITE "${CURRENT_PACKAGES_DIR}/include/ogdf/basic/internal/config_autogen.h" "${_autogen_text}")

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/include/ogdf-release"
    "${CURRENT_PACKAGES_DIR}/include/ogdf-debug"
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    # doc.cmake installs doc/examples unconditionally, DOC_INSTALL or not.
    "${CURRENT_PACKAGES_DIR}/share/doc"
    "${CURRENT_PACKAGES_DIR}/debug/doc"
)

vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/LICENSE.txt"
    "${SOURCE_PATH}/LICENSE_GPL_v2.txt"
    "${SOURCE_PATH}/LICENSE_GPL_v3.txt"
)
