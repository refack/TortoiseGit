cmake_minimum_required(VERSION 4.0)
project(UnitTests LANGUAGES CXX)

find_package(GTest REQUIRED CONFIG)
find_package(MFC REQUIRED)

add_executable(UnitTests)

target_sources(UnitTests
    PRIVATE
        AutoTempDir.cpp
        AutoTempDir.h
        CmdLineParserTest.cpp
        CrashProbe.cpp   # TEMPORARY: static-init crash diagnosis
        GitByteArrayTest.cpp
        GitHashTest.cpp
        GitIndexTest.cpp
        GitRevLoglistTest.cpp
        GitRevTest.cpp
        GitTest.cpp
        libgit2Test.cpp
        libgitTest.cpp
        LruCacheTest.cpp
        PathUtilsTest.cpp
        PersonalDictionaryTest.cpp
        RepositoryFixtures.h
        stdafx.cpp
        stdafx.h
        StringUtilsTest.cpp
        TempFileTest.cpp
        TGitPathTest.cpp
        UnicodeUtilsTest.cpp
        UniqueQueueTests.cpp
        UnitTests.cpp
        UnitTests.rc2
        WideStringTest.cpp
)

# Parked, not deleted: each tests code that is not in any CMake target yet - the
# vcxproj build compiled these subjects straight out of TortoiseProc and Utils.
# They come back as their subject joins a target. Every file here is a suite
# shrink that has to be accounted for, so list the reason with it.
set(UNITTESTS_PARKED
    # Drafts, never built: TDD assertions for the 2026-09-28 conjectures. They
    # assert APIs that do not exist yet, so parking them shrinks nothing.
    ConjectureGitTest.cpp
    ConjectureShellDataTest.cpp
    ConjectureStringTest.cpp
    ConjectureTGitPathTest.cpp

    AppUtilsTest.cpp                # TortoiseProc/AppUtils
    FileTextLinesTest.cpp           # MFC (CStdioFile): belongs to the MFC binary, and this target is ATL
    GitAdminDirTest.cpp             # GitAdminDir.ixx is not in TGitLibgit2's module set
    GitCliOutputParserTest.cpp      # TortoiseProc/GitCliOutputParser
    GitRevRefBrowseTest.cpp         # TortoiseProc/GitRevRefBrowser
    GitWCRevStatusTest.cpp          # GitWCRev/status
    LogDataVectorTest.cpp           # TortoiseProc/LogDlgHelper
    LogFileTest.cpp                 # TortoiseProc/LogFile
    ProjectPropertiesTest.cpp       # TortoiseProc/ProjectProperties
    StagingTest.cpp                 # TortoiseProc/StagingOperations
    TortoiseGitBlameDataTest.cpp    # TortoiseGitBlame/TortoiseGitBlameData
    UpdateCryptoTest.cpp            # TortoiseProc/UpdateCrypto
    VersioncheckParserTest.cpp      # TortoiseProc/VersioncheckParser
    WindowsCredentialsStoreTests.cpp  # Utils/WindowsCredentialsStore
)

set_target_properties(UnitTests PROPERTIES
    CMAKE_MFC_FLAG 2
    CXX_MODULE_STD ON
)

target_compile_features(UnitTests
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(UnitTests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/gitdll"
        "${CMAKE_SOURCE_DIR}/src/libgit2/include"
        "${CMAKE_SOURCE_DIR}/src/libgit2/include/Utils"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(UnitTests
    PRIVATE
        TGIT_TESTS_ONLY
        TGIT_LFS
        "TGIT_TEST_RESOURCES_DIR=L\"${CMAKE_CURRENT_SOURCE_DIR}/resources\""
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
        "$<$<CONFIG:Debug>:_DEBUG>"
        "$<$<CONFIG:Debug>:DEBUG>"
        "$<$<CONFIG:Release>:NDEBUG>"
        "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},x64>:_AMD64_>"
        "$<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},ARM64>:_ARM64_>"
)

target_link_directories(UnitTests
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib>"
)

target_link_libraries(UnitTests
    PRIVATE
        GTest::gtest
        GTest::gmock
        winhttp
        rpcrt4
        gdi32
        crypt32
        gitdll
        TGitLibgit2
)

# gitdll is a DLL (and must stay one), so it has to sit next to the exe. Copied
# rather than sharing a bin\ output directory: RepositoryFixtures' GetResourcesDir()
# finds test\UnitTests\resources relative to where UnitTests.exe lives.
add_custom_command(TARGET UnitTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:UnitTests> $<TARGET_FILE_DIR:UnitTests>
    COMMAND_EXPAND_LISTS
)

target_compile_options(UnitTests
    PRIVATE
        /Zm110
        /W3
        /external:W0
        /external:anglebrackets
)

