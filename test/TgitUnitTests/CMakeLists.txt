add_executable(TGitUnitTests)

# Textual includes only: a PCH is never module-scanned, so an `import` in it is C2230.
target_precompile_headers(TGitUnitTests PRIVATE stdafx_pch.h)

target_sources(TGitUnitTests
    PRIVATE
        CrashProbe.cpp   # TEMPORARY: static-init crash diagnosis
        CmdLineParserTest.cpp
        GitByteArrayTest.cpp
        GitHashTest.cpp
        GitIndexTest.cpp
        GitLinesTest.cpp
        GitRevLoglistTest.cpp
        GitRevTest.cpp
        GitTest.cpp
        libgit2Test.cpp
        libgitTest.cpp
        LruCacheTest.cpp
        PathUtilsTest.cpp
        PersonalDictionaryTest.cpp
        StringUtilsTest.cpp
        TempFileTest.cpp
        TGitPathTest.cpp
        UnicodeUtilsTest.cpp
        UniqueQueueTests.cpp
        WideStringTest.cpp
        UnitTests.cpp

        # TortoiseProc's strings, and its VERSIONINFO, which CPathUtils.GetCopyrightForSelf
        # reads back from this exe. UnitTests.rc2 is not compiled: CMake hands only .rc to RC.
        "${CMAKE_SOURCE_DIR}/src/TortoiseProc/TortoiseProcENG.rc"
        UnitTests.rc2
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
    GitAdminDirTest.cpp             # GitAdminDir now lives in TGitPath; still includes the retired StringUtils.h
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

target_include_directories(TGitUnitTests
    PRIVATE
        gitdll
        tgitlib
        SharedMFC
        # bare icon/avi names in TortoiseProcENG.rc, cf. src/TortoiseProc/CMakeLists.txt
        "$<$<COMPILE_LANGUAGE:RC>:${CMAKE_SOURCE_DIR}/src/SharedMFC/Resources>"
)

target_compile_definitions(TGitUnitTests
    PRIVATE
        TGIT_TESTS_ONLY
        "TGIT_TEST_RESOURCES_DIR=L\"${CMAKE_CURRENT_SOURCE_DIR}/resources\""
)

target_link_libraries(TGitUnitTests
    PRIVATE
        winhttp
        rpcrt4
        gdi32
        crypt32

        GTest::gtest
        GTest::gmock

        gitdll
        tgitlib
        SharedMFC
)

# gitdll is a DLL (and must stay one), so it has to sit next to the exe. Copied
# rather than sharing a bin\ output directory: RepositoryFixtures' GetResourcesDir()
# finds test\UnitTests\resources relative to where UnitTests.exe lives.
add_custom_command(TARGET TGitUnitTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:TGitUnitTests> $<TARGET_FILE_DIR:TGitUnitTests>
    COMMAND_EXPAND_LISTS
)


target_sources(TGitUnitTests
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        AutoTempDir.ixx
        RepositoryFixtures.ixx
)