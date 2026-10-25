// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2015-2018, 2020 - TortoiseGit
// Copyright (C) 2003-2008, 2013-2014 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

#include "stdafx.h"
import PathUtils;
namespace fs = std::filesystem;

TEST(PathUtilsTest, GetFileNameFromPath)
{
	std::wstring test(L"d:\\test\\filename.ext");
	EXPECT_STREQ(L"filename.ext", PathUtils::GetFileNameFromPath(test).c_str());
	test = L"filename.ext";
	EXPECT_STREQ(L"filename.ext", PathUtils::GetFileNameFromPath(test).c_str());
	test = L"d:/test/filename";
	EXPECT_STREQ(L"filename", PathUtils::GetFileNameFromPath(test).c_str());
	test = L"d:\\test\\filename";
	EXPECT_STREQ(L"filename", PathUtils::GetFileNameFromPath(test).c_str());
	test = L"filename";
	EXPECT_STREQ(L"filename", PathUtils::GetFileNameFromPath(test).c_str());
	test.clear();
	EXPECT_STREQ(L"", PathUtils::GetFileNameFromPath(test).c_str());

	// Mixed separators, which nothing above covers and which is exactly what
	// distinguishes the two implementations: this used to normalize every slash
	// to a backslash and then take everything after the last one, and now looks
	// for the last of either. Both answer "filename" - assert that they do.
	EXPECT_STREQ(L"filename", PathUtils::GetFileNameFromPath(L"d:\\test/filename").c_str());
	EXPECT_STREQ(L"filename", PathUtils::GetFileNameFromPath(L"d:/test\\filename").c_str());
	EXPECT_STREQ(L"", PathUtils::GetFileNameFromPath(L"d:\\test\\").c_str());
}

TEST(CPathUtils, ExtTest)
{
	std::wstring test(L"d:\\test\\filename.ext");
	EXPECT_STREQ(L".ext", PathUtils::GetFileExtFromPath(test).c_str());
	test = L"filename.ext";
	EXPECT_STREQ(L".ext", PathUtils::GetFileExtFromPath(test).c_str());
	test = L"d:\\test\\filename";
	EXPECT_STREQ(L"", PathUtils::GetFileExtFromPath(test).c_str());
	test = L"filename";
	EXPECT_STREQ(L"", PathUtils::GetFileExtFromPath(test).c_str());
	test.clear();
	EXPECT_STREQ(L"", PathUtils::GetFileExtFromPath(test).c_str());

	// A dot in a directory name is not an extension of the file below it.
	EXPECT_STREQ(L"", PathUtils::GetFileExtFromPath(L"d:\\te.st\\filename").c_str());
}

TEST(CPathUtils, ParseTests)
{
	std::wstring test(L"test 'd:\\testpath with spaces' test");
	EXPECT_STREQ(L"d:\\testpath with spaces", PathUtils::ParsePathInString(test).c_str());
	test = L"d:\\testpath with spaces";
	EXPECT_STREQ(L"d:\\testpath with spaces", PathUtils::ParsePathInString(test).c_str());
}

TEST(CPathUtils, ArePathStringsEqual)
{
	// Two empty paths are equal, and must not reach the length-taking overload
	// with the null data() a default-constructed view has.
	EXPECT_TRUE(PathUtils::ArePathStringsEqual(std::wstring_view(), std::wstring_view()));
	EXPECT_TRUE(PathUtils::ArePathStringsEqualWithCase(std::wstring_view(), std::wstring_view()));

	EXPECT_TRUE(PathUtils::ArePathStringsEqual(L"C:\\My\\Path", L"c:\\my\\path"));
	EXPECT_FALSE(PathUtils::ArePathStringsEqualWithCase(L"C:\\My\\Path", L"c:\\my\\path"));
	EXPECT_TRUE(PathUtils::ArePathStringsEqualWithCase(L"C:\\My\\Path", L"C:\\My\\Path"));

	// Differing lengths are rejected before any comparison; a prefix is not a match.
	EXPECT_FALSE(PathUtils::ArePathStringsEqual(L"C:\\my", L"C:\\my\\path"));

	// ...unless the caller asks for exactly that, which is what the length-taking
	// overload is for (CTGitPath::IsAncestorOf).
	EXPECT_TRUE(PathUtils::ArePathStringsEqual(L"C:\\MY\\path", L"c:\\my", 5));
}

TEST(CPathUtils, MakeSureDirectoryPathExists)
{
	CAutoTempDir tmpDir;
	EXPECT_TRUE(fs::is_directory(tmpDir.GetTempDir()));
	EXPECT_FALSE(fs::exists(tmpDir.GetTempDir() / L"sub"));
	EXPECT_FALSE(fs::is_directory(tmpDir.GetTempDir() / L"sub"));

	EXPECT_TRUE(PathUtils::MakeSureDirectoryPathExists((tmpDir.GetTempDir() / L"sub\\sub\\dir").c_str()));
	EXPECT_TRUE(fs::is_directory(tmpDir.GetTempDir() / L"sub"));
	EXPECT_TRUE(fs::is_directory(tmpDir.GetTempDir() / L"sub\\sub"));
	EXPECT_TRUE(fs::is_directory(tmpDir.GetTempDir() / L"sub\\sub\\dir"));

	EXPECT_TRUE(PathUtils::MakeSureDirectoryPathExists((tmpDir.GetTempDir() / L"sub/asub/adir").c_str()));
	EXPECT_TRUE(fs::is_directory(tmpDir.GetTempDir() / L"sub"));
	EXPECT_TRUE(fs::is_directory(tmpDir.GetTempDir() / L"sub\\asub"));
	EXPECT_TRUE(fs::is_directory(tmpDir.GetTempDir() / L"sub\\asub\\adir"));
}

TEST(CPathUtils, EnsureTrailingPathDelimiter)
{
	std::wstring tPath;
	PathUtils::EnsureTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"");

	tPath = L"C:";
	PathUtils::EnsureTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"C:\\");

	tPath = L"C:\\";
	PathUtils::EnsureTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"C:\\");

	tPath = L"C:\\my\\path";
	PathUtils::EnsureTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"C:\\my\\path\\");

	tPath = L"C:\\my\\path\\";
	PathUtils::EnsureTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"C:\\my\\path\\");
}

TEST(CPathUtils, BuildPathWithPathDelimiter)
{
	EXPECT_STREQ(PathUtils::BuildPathWithPathDelimiter(L"").c_str(), L"");
	EXPECT_STREQ(PathUtils::BuildPathWithPathDelimiter(L"C:").c_str(), L"C:\\");
	EXPECT_STREQ(PathUtils::BuildPathWithPathDelimiter(L"C:\\").c_str(), L"C:\\");
	EXPECT_STREQ(PathUtils::BuildPathWithPathDelimiter(L"C:\\my\\path").c_str(), L"C:\\my\\path\\");
	EXPECT_STREQ(PathUtils::BuildPathWithPathDelimiter(L"C:\\my\\path\\").c_str(), L"C:\\my\\path\\");
}

TEST(CPathUtils, TrimTrailingPathDelimiter)
{
	std::wstring tPath;
	PathUtils::TrimTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"");

	tPath = L"C:";
	PathUtils::TrimTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"C:");

	tPath = L"C:\\";
	PathUtils::TrimTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"C:");

	tPath = L"C:\\my\\path";
	PathUtils::TrimTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"C:\\my\\path");

	tPath = L"C:\\my\\path\\";
	PathUtils::TrimTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"C:\\my\\path");

	tPath = L"C:\\my\\path\\\\";
	PathUtils::TrimTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"C:\\my\\path");

	// All-delimiters trims to nothing rather than leaving one behind.
	tPath = L"\\\\";
	PathUtils::TrimTrailingPathDelimiter(tPath);
	EXPECT_STREQ(tPath.c_str(), L"");
}

TEST(CPathUtils, ExpandFileName)
{
	EXPECT_STREQ(PathUtils::ExpandFileName(L"C:\\my\\path\\da\\da\\da").c_str(), L"C:\\my\\path\\da\\da\\da");
	EXPECT_STREQ(PathUtils::ExpandFileName(L"C:\\my\\path\\da\\da\\da\\").c_str(), L"C:\\my\\path\\da\\da\\da\\");

	EXPECT_STREQ(PathUtils::ExpandFileName(L"C:\\my\\path\\\\da\\da\\da").c_str(), L"C:\\my\\path\\da\\da\\da");
	EXPECT_STREQ(PathUtils::ExpandFileName(L"C:\\my\\path\\.\\da\\da\\da").c_str(), L"C:\\my\\path\\da\\da\\da");
	EXPECT_STREQ(PathUtils::ExpandFileName(L"C:\\my\\path\\.\\da\\da\\da\\").c_str(), L"C:\\my\\path\\da\\da\\da\\");

	EXPECT_STREQ(PathUtils::ExpandFileName(L"C:\\my\\path\\..\\da\\da\\da").c_str(), L"C:\\my\\da\\da\\da");
	EXPECT_STREQ(PathUtils::ExpandFileName(L"C:\\my\\path\\..\\da\\da\\da\\").c_str(), L"C:\\my\\da\\da\\da\\");

	// "\\.\\C:\\"
	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\.\\C:\\my\\path\\da\\da\\da").c_str(), L"\\\\.\\C:\\my\\path\\da\\da\\da");
	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\.\\C:\\my\\path\\da\\da\\da\\").c_str(), L"\\\\.\\C:\\my\\path\\da\\da\\da\\");

	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\.\\C:\\my\\path\\\\da\\da\\da").c_str(), L"\\\\.\\C:\\my\\path\\da\\da\\da");
	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\.\\C:\\my\\path\\.\\da\\da\\da").c_str(), L"\\\\.\\C:\\my\\path\\da\\da\\da");
	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\.\\C:\\my\\path\\.\\da\\da\\da\\").c_str(), L"\\\\.\\C:\\my\\path\\da\\da\\da\\");

	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\.\\C:\\my\\path\\..\\da\\da\\da").c_str(), L"\\\\.\\C:\\my\\da\\da\\da");
	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\.\\C:\\my\\path\\..\\da\\da\\da\\").c_str(), L"\\\\.\\C:\\my\\da\\da\\da\\");

	// UNC paths
	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\DACOMPUTER\\my\\path\\.\\da\\da\\da").c_str(), L"\\\\DACOMPUTER\\my\\path\\da\\da\\da");
	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\DACOMPUTER\\my\\path\\.\\da\\da\\da\\").c_str(), L"\\\\DACOMPUTER\\my\\path\\da\\da\\da\\");

	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\DACOMPUTER\\my\\path\\..\\da\\da\\da").c_str(), L"\\\\DACOMPUTER\\my\\da\\da\\da");
	EXPECT_STREQ(PathUtils::ExpandFileName(L"\\\\DACOMPUTER\\my\\path\\..\\da\\da\\da\\").c_str(), L"\\\\DACOMPUTER\\my\\da\\da\\da\\");
}

TEST(CPathUtils, IsSamePath)
{
	EXPECT_TRUE(PathUtils::IsSamePath(L"", L""));

	EXPECT_TRUE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\my\\path\\da\\da\\da"));
	EXPECT_TRUE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"c:\\my\\pAth\\DA\\da\\da"));
	EXPECT_TRUE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\my\\path\\da\\da\\da\\"));
	EXPECT_TRUE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\my\\path\\da\\da\\da\\."));
	EXPECT_TRUE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\my\\path\\da\\da\\da\\.\\"));
	EXPECT_TRUE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\my\\path\\.\\.\\da\\da\\da"));
	EXPECT_TRUE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\my\\path\\..\\path\\da\\da\\da"));
	EXPECT_TRUE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\my\\path\\..\\path\\da\\da\\da\\bla\\.."));

	EXPECT_FALSE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\this\\is\\a\\new\\path"));
	EXPECT_FALSE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\my\\path\\da\\da\\da\\.."));
	EXPECT_FALSE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\my\\path\\da\\da\\da\\..\\"));
	EXPECT_FALSE(PathUtils::IsSamePath(L"C:\\my\\path\\da\\da\\da", L"C:\\my\\path\\..\\path\\da\\da\\da\\.\\da"));
}

TEST(CPathUtils, GetCopyrightForSelf)
{
	const std::wstring copyright = PathUtils::GetCopyrightForSelf();
	EXPECT_TRUE(tgit::wstr::StartsWith(copyright, L"Copyright (C) 20")) << "copyright:" << copyright;
}

TEST(CPathUtils, ConvertToSlash)
{
	// data() rather than the CString GetBuffer() this used to call: that never
	// had its matching ReleaseBuffer, so the string stayed buffer-locked for the
	// rest of the test. std::wstring's storage is contiguous and always
	// null-terminated, so there is nothing to release.
	std::wstring path = L"";
	PathUtils::ConvertToSlash(path);
	EXPECT_STREQ(path.c_str(), L"");

	path = L"\\";
	PathUtils::ConvertToSlash(path);
	EXPECT_STREQ(path.c_str(), L"/");

	path = L"test";
	PathUtils::ConvertToSlash(path);
	EXPECT_STREQ(path.c_str(), L"test");

	path = L"test\\def";
	PathUtils::ConvertToSlash(path);
	EXPECT_STREQ(path.c_str(), L"test/def");

	path = L"test/def";
	PathUtils::ConvertToSlash(path);
	EXPECT_STREQ(path.c_str(), L"test/def");

	path = L"te\\st/def";
	PathUtils::ConvertToSlash(path);
	EXPECT_STREQ(path.c_str(), L"te/st/def");
}

TEST(CPathUtils, ConvertToBackslash)
{
	std::wstring out;

	out = L"";
	PathUtils::ConvertToBackslash(out);
	EXPECT_STREQ(out.c_str(), L"");

	out = L"/";
	PathUtils::ConvertToBackslash(out);
	EXPECT_STREQ(out.c_str(), L"\\");

	out = L"test";
	PathUtils::ConvertToBackslash(out);
	EXPECT_STREQ(out.c_str(), L"test");

	out = L"test/def";
	PathUtils::ConvertToBackslash(out);
	EXPECT_STREQ(out.c_str(), L"test\\def");

	out = L"test\\def";
	PathUtils::ConvertToBackslash(out);
	EXPECT_STREQ(out.c_str(), L"test\\def");

	out = L"te\\st/def";
	PathUtils::ConvertToBackslash(out);
	EXPECT_STREQ(out.c_str(), L"te\\st\\def");
}
