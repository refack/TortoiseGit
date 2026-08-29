cmake_minimum_required(VERSION 4.0)
project(UnitTests LANGUAGES CXX)

find_package(GTest REQUIRED CONFIG)
find_package(MFC REQUIRED)

add_executable(UnitTests)

target_sources(UnitTests
    PRIVATE
        AppUtilsTest.cpp
        AutoTempDir.cpp
        AutoTempDir.h
        CmdLineParserTest.cpp
        FileTextLinesTest.cpp
        GitAdminDirTest.cpp
        GitByteArrayTest.cpp
        GitCliOutputParserTest.cpp
        GitHashTest.cpp
        GitIndexTest.cpp
        GitRevLoglistTest.cpp
        GitRevRefBrowseTest.cpp
        GitRevTest.cpp
        GitTest.cpp
        GitWCRevStatusTest.cpp
        libgit2Test.cpp
        libgitTest.cpp
        LogDataVectorTest.cpp
        LogFileTest.cpp
        LruCacheTest.cpp
        PathUtilsTest.cpp
        PersonalDictionaryTest.cpp
        ProjectPropertiesTest.cpp
        RepositoryFixtures.h
        StagingTest.cpp
        stdafx.cpp
        stdafx.h
        StringUtilsTest.cpp
        TempFileTest.cpp
        TGitPathTest.cpp
        TortoiseGitBlameDataTest.cpp
        UnicodeUtilsTest.cpp
        UniqueQueueTests.cpp
        UnitTests.cpp
        UnitTests.rc2
        UpdateCryptoTest.cpp
        VersioncheckParserTest.cpp
        WideStringTest.cpp
        WindowsCredentialsStoreTests.cpp
)

set_target_properties(UnitTests PROPERTIES
    CMAKE_MFC_FLAG 2
)

target_compile_features(UnitTests
    PRIVATE
        cxx_std_23
        c_std_23
)

target_include_directories(UnitTests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

target_compile_definitions(UnitTests
    PRIVATE
        TGIT_TESTS_ONLY
        TGIT_LFS
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

target_compile_options(UnitTests
    PRIVATE
        /Zm110
        /W3
        /external:W0
        /external:anglebrackets
)

