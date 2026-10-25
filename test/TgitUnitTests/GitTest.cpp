// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2015-2021, 2023, 2025-2026 - TortoiseGit

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
#include "RepositoryFixtures.h"
import git_h;
import GitRev;
import str_utils;
import ShellEnvironment;

// For performance reason, turn LIBGIT off by default,
INSTANTIATE_TEST_SUITE_P(CGit, CBasicGitWithEmptyRepositoryFixture, testing::Values(GIT_CLI, /*LIBGIT,*/ LIBGIT2, LIBGIT2_ALL));
INSTANTIATE_TEST_SUITE_P(CGit, CBasicGitWithEmptyBareRepositoryFixture, testing::Values(GIT_CLI, /*LIBGIT,*/ LIBGIT2, LIBGIT2_ALL));
INSTANTIATE_TEST_SUITE_P(CGit, CBasicGitWithTestRepoFixture, testing::Values(GIT_CLI, LIBGIT, LIBGIT2, LIBGIT2_ALL));
INSTANTIATE_TEST_SUITE_P(CGit, CBasicGitWithTestRepoBareFixture, testing::Values(GIT_CLI, LIBGIT, LIBGIT2, LIBGIT2_ALL));
INSTANTIATE_TEST_SUITE_P(CGit, CBasicGitWithSubmoduleRepositoryFixture, testing::Values(GIT_CLI, /*LIBGIT,*/ LIBGIT2, LIBGIT2_ALL));

TEST(CGit, RunSet)
{
	CString output;
	CGit cgit;
	ASSERT_EQ(0, GitCall::Run({L"cmd", L"/c", L"set"}, &output));
	ASSERT_FALSE(output.IsEmpty());
	ASSERT_TRUE(output.Find(L"windir")); // should be there on any MS OS ;)
}

TEST(CGit, RunGit)
{
	CString output;
	CGit cgit;
	ASSERT_EQ(0, GitCall::Run({L"git", L"--version"}, &output));
	ASSERT_FALSE(output.IsEmpty());
}

TEST(CGit, RunGit_BashPipe)
{
	auto tmpfile = GitCall::GetTempFile();
	ASSERT_STRNE(L"", tmpfile);
	tmpfile.Replace(L'\\', L'/');
	ASSERT_TRUE(StringUtils::WriteStringToTextFile(tmpfile, L"testing piping..."));
	SCOPE_EXIT{ ::DeleteFile(tmpfile); };
	CString pipefile = GitCall::GetTempFile();
	ASSERT_STRNE(L"", tmpfile);
	pipefile.Replace(L'\\', L'/');
	CString pipecmd;
	pipecmd.Format(L"cat < %s", static_cast<LPCTSTR>(tmpfile));
	ASSERT_TRUE(StringUtils::WriteStringToTextFile(pipefile, pipecmd));
	SCOPE_EXIT{ ::DeleteFile(pipefile); };
	CString output;
	CGit cgit;
	ASSERT_EQ(0, GitCall::Run({L"bash.exe", std::wstring(pipefile)}, &output));
	ASSERT_STREQ(L"testing piping...", output);
}

TEST(CGit, RunGit_Error)
{
	CAutoTempDir tempdir;
	CGit cgit;
	cgit.SetCurrentDirExact(tempdir.GetTempDir());

	CString output;
	EXPECT_NE(0, GitCall::Run({L"git-not-found.exe"}, &output)); // Git for Windows returns 2, cygwin-hack returns 127
	//EXPECT_STREQ(L"", output); with cygwin-hack we get an error message from sh.exe

	output.Empty();
	EXPECT_EQ(128, GitCall::Run({L"git.exe", L"add", L"file.txt"}, &output));
	EXPECT_TRUE(StringUtils::StartsWithI(output, L"fatal: not a git repository (or any"));
}

TEST_P(CBasicGitWithTestRepoBareFixture, RunGit_AbsolutePath)
{
	CAutoTempDir tempdir;

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"archive", L"-o", (tempdir.GetTempDir() / L"export.zip").c_str(), L"HEAD"}, &output));
	EXPECT_STREQ("", output);

	EXPECT_TRUE(fs::exists(tempdir.GetTempDir() / L"export.zip"));
}

TEST(CGit, RunLogFile)
{
	CAutoTempDir tempdir;
	auto tmpfile = tempdir.GetTempDir() / L"output.txt";
	CString error;
	CGit cgit;
	ASSERT_EQ(0, GitCall::RunLogFile(L"git --version", tmpfile.c_str(), &error));
	EXPECT_STREQ(L"", error);
	CString fileContents;
	EXPECT_EQ(true, StringUtils::ReadStringFromTextFile(tmpfile.c_str(), fileContents));
	EXPECT_TRUE(StringUtils::StartsWith(fileContents, L"git version "));
}

TEST(CGit, RunLogFile_Set)
{
	CAutoTempDir tempdir;
	auto tmpfile = tempdir.GetTempDir() / L"output.txt";
	CString error;
	CGit cgit;
	ASSERT_EQ(0, GitCall::RunLogFile(L"cmd /c set", tmpfile.c_str(), &error));
	EXPECT_STREQ(L"", error);
	CString fileContents;
	EXPECT_EQ(true, StringUtils::ReadStringFromTextFile(tmpfile.c_str(), fileContents));
	EXPECT_TRUE(fileContents.Find(L"windir")); // should be there on any MS OS ;)
}

TEST(CGit, RunLogFile_Error)
{
	CAutoTempDir tempdir;
	auto tmpfile = tempdir.GetTempDir() / L"output.txt";
	CString error;
	CGit cgit;
	cgit.SetCurrentDirExact(tempdir.GetTempDir());

	EXPECT_EQ(128, GitCall::RunLogFile(L"git.exe add file.txt", tmpfile.c_str(), &error));
	EXPECT_TRUE(StringUtils::StartsWithI(error, L"fatal: not a git repository (or any"));
	__int64 size = -1;
	EXPECT_EQ(0, TGitH::GetFileModifyTime(tmpfile, nullptr, nullptr, &size));
	EXPECT_EQ(0, size);
}

TEST(CGit, StringAppend)
{
	CString string = L"something";
	CGit::StringAppend(string, std::string_view());
	EXPECT_STREQ(L"something", string);
	CGit::StringAppend(string, std::string_view(nullptr, 0));
	EXPECT_STREQ(L"something", string);
	constexpr char somebytes[1] = { 0 };
	CGit::StringAppend(string, somebytes, 0);
	EXPECT_STREQ(L"something", string);
	CGit::StringAppend(string, somebytes);
	EXPECT_STREQ(L"something", string);
	constexpr char moreBytesUTFEight[] = { 0x68, 0x65, 0x6C, 0x6C, '\xC3', '\xB6', 0x0A, 0x00 };
	CGit::StringAppend(string, std::string_view(moreBytesUTFEight, 3));
	EXPECT_STREQ(L"somethinghel", string);
	CGit::StringAppend(string, std::string_view(moreBytesUTFEight + 3, 1), CP_ACP);
	EXPECT_STREQ(L"somethinghell", string);
	CGit::StringAppend(string, moreBytesUTFEight);
	EXPECT_STREQ(L"somethinghellhellö\n", string);
	CGit::StringAppend(string, std::string_view(moreBytesUTFEight, sizeof(moreBytesUTFEight)));
	EXPECT_STREQ(L"somethinghellhellö\nhellö\n\0", string);
	CGit::StringAppend(string, std::string_view(moreBytesUTFEight, 3));
	EXPECT_STREQ(L"somethinghellhellö\nhellö\n\0hel", string);
	CGit::StringAppend(string, "\xf0\xa0\x9c\x8e");
	EXPECT_STREQ(L"somethinghellhellö\nhellö\n\0hel\U0002070e", string);
}

TEST(CGit, GetFileModifyTime)
{
	__int64 time = -1;
	bool isDir = false;
	__int64 size = -1;
	EXPECT_EQ(-1, TGitH::GetFileModifyTime(L"does-not-exist.txt", &time, &isDir, &size));

	time = -1;
	isDir = false;
	size = -1;
	EXPECT_EQ(0, TGitH::GetFileModifyTime(L"c:\\Windows", &time, &isDir, &size));
	EXPECT_TRUE(isDir);

	time = -1;
	isDir = false;
	size = -1;
	EXPECT_EQ(0, TGitH::GetFileModifyTime(L"c:\\Windows\\", &time, &isDir, &size));
	EXPECT_TRUE(isDir);

	CAutoTempDir tempdir;
	auto testFile = tempdir.GetTempDir() / L"test.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is testing fileöäü."));

	time = -1;
	EXPECT_EQ(0, TGitH::GetFileModifyTime(testFile, &time));
	EXPECT_NE(-1, time);

	__int64 time2 = -1;
	isDir = false;
	size = -1;
	EXPECT_EQ(0, TGitH::GetFileModifyTime(testFile, &time2, &isDir, &size));
	EXPECT_EQ(time, time2);
	EXPECT_FALSE(isDir);
	EXPECT_EQ(27, size);

	Sleep(250);
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is testing fileöü."));
	__int64 time3 = -1;
	isDir = false;
	size = -1;
	EXPECT_EQ(0, TGitH::GetFileModifyTime(testFile, &time3, &isDir, &size));
	EXPECT_NE(-1, time3);
	EXPECT_FALSE(isDir);
	EXPECT_EQ(25, size);
	EXPECT_GE(time3, time);
}

TEST(CGit, LoadTextFile)
{
	CAutoTempDir tempdir;

	CString msg = L"something--";
	EXPECT_FALSE(TGitH::LoadTextFile(L"does-not-exist.txt", msg));
	EXPECT_STREQ(L"something--", msg);

	auto testFile = tempdir.GetTempDir() / L"test.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is testing fileöäü."));
	EXPECT_TRUE(TGitH::LoadTextFile(testFile.c_str(), msg));
	EXPECT_STREQ(L"something--this is testing fileöäü.\n", msg);

	msg.Empty();
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is testing\nfileöäü."));
	EXPECT_TRUE(TGitH::LoadTextFile(testFile.c_str(), msg));
	EXPECT_STREQ(L"this is testing\nfileöäü.\n", msg);

	msg.Empty();
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is\r\ntesting\nfileöäü.\r\n\r\n"));
	EXPECT_TRUE(TGitH::LoadTextFile(testFile.c_str(), msg));
	EXPECT_STREQ(L"this is\ntesting\nfileöäü.\n", msg);
}

TEST(CGit, IsBranchNameValid)
{
	CGit cgit;
	EXPECT_TRUE(cgit.IsBranchNameValid(L"master"));
	EXPECT_TRUE(cgit.IsBranchNameValid(L"def/master"));
	EXPECT_TRUE(cgit.IsBranchNameValid(L"mäin"));
	EXPECT_TRUE(cgit.IsBranchNameValid(L"branch_\u570B\u7ACB1d\u043A"));
	EXPECT_FALSE(cgit.IsBranchNameValid(L"HEAD"));
	EXPECT_FALSE(cgit.IsBranchNameValid(L"-test"));
	EXPECT_FALSE(cgit.IsBranchNameValid(L"jfjf>ff"));
	EXPECT_FALSE(cgit.IsBranchNameValid(L"jf ff"));
	EXPECT_FALSE(cgit.IsBranchNameValid(L"jf~ff"));
}

TEST(CGit, StripRefName)
{
	EXPECT_STREQ(L"abc", TGitH::StripRefName(L"abc"));
	EXPECT_STREQ(L"bcd", TGitH::StripRefName(L"refs/bcd"));
	EXPECT_STREQ(L"cde", TGitH::StripRefName(L"refs/heads/cde"));

	EXPECT_STREQ(L"notes/commits", TGitH::StripRefName(L"refs/notes/commits"));
	EXPECT_STREQ(L"remotes/origin/abc", TGitH::StripRefName(L"refs/remotes/origin/abc"));
	EXPECT_STREQ(L"tags/abc", TGitH::StripRefName(L"refs/tags/abc"));
}

TEST(CGit, CombinePath)
{
	CGit cgit;
	cgit.SetCurrentDirExact(L"c:\\something");
	EXPECT_STREQ(L"c:\\something", cgit.CombinePath(L""));
	EXPECT_STREQ(L"c:\\something\\file.txt", cgit.CombinePath(L"file.txt"));
	EXPECT_STREQ(L"c:\\something\\sub\\file.txt", cgit.CombinePath(L"sub\\file.txt"));
	EXPECT_STREQ(L"c:\\something\\subdir\\file2.txt", cgit.CombinePath(CTGitPath(L"subdir/file2.txt")));
}

TEST(CGit, GetShortName)
{
	TGitH::REF_TYPE type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"master", TGitH::GetShortName(L"refs/heads/master", &type));
	EXPECT_EQ(TGitH::LOCAL_BRANCH, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"somedir/mastr", TGitH::GetShortName(L"refs/heads/somedir/mastr", &type));
	EXPECT_EQ(TGitH::LOCAL_BRANCH, type);

	type = TGitH::BISECT_BAD; // do not use UNKNOWN here to make sure it gets set
	EXPECT_STREQ(L"svn/something", TGitH::GetShortName(L"refs/svn/something", &type));
	EXPECT_EQ(TGitH::UNKNOWN, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"origin/master", TGitH::GetShortName(L"refs/remotes/origin/master", &type));
	EXPECT_EQ(TGitH::REMOTE_BRANCH, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"origin/sub/master", TGitH::GetShortName(L"refs/remotes/origin/sub/master", &type));
	EXPECT_EQ(TGitH::REMOTE_BRANCH, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"release1", TGitH::GetShortName(L"refs/tags/release1", &type));
	EXPECT_EQ(TGitH::TAG, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"releases/v1", TGitH::GetShortName(L"refs/tags/releases/v1", &type));
	EXPECT_EQ(TGitH::TAG, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"release2", TGitH::GetShortName(L"refs/tags/release2^{}", &type));
	EXPECT_EQ(TGitH::ANNOTATED_TAG, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"releases/v2", TGitH::GetShortName(L"refs/tags/releases/v2^{}", &type));
	EXPECT_EQ(TGitH::ANNOTATED_TAG, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"stash", TGitH::GetShortName(L"refs/stash", &type));
	EXPECT_EQ(TGitH::STASH, type);

	type = TGitH::BISECT_BAD; // do not use UNKNOWN here to make sure it gets set
	EXPECT_STREQ(L"something", TGitH::GetShortName(L"refs/something", &type));
	EXPECT_EQ(TGitH::UNKNOWN, type);

	type = TGitH::BISECT_BAD; // do not use UNKNOWN here to make sure it gets set
	EXPECT_STREQ(L"sth", TGitH::GetShortName(L"sth", &type));
	EXPECT_EQ(TGitH::UNKNOWN, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"good", TGitH::GetShortName(L"refs/bisect/good", &type));
	EXPECT_EQ(TGitH::BISECT_GOOD, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"good", TGitH::GetShortName(L"refs/bisect/good-5809ac97a1115a8380b1d6bb304b62cd0b0fa9bb", &type));
	EXPECT_EQ(TGitH::BISECT_GOOD, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"bad", TGitH::GetShortName(L"refs/bisect/bad", &type));
	EXPECT_EQ(TGitH::BISECT_BAD, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"bad", TGitH::GetShortName(L"refs/bisect/bad-5809ac97a1115a8380b1d6bb304b62cd0b0fd9bb", &type));
	EXPECT_EQ(TGitH::BISECT_BAD, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"ab", TGitH::GetShortName(L"refs/notes/ab", &type));
	EXPECT_EQ(TGitH::NOTES, type);

	type = TGitH::UNKNOWN;
	EXPECT_STREQ(L"a/b", TGitH::GetShortName(L"refs/notes/a/b", &type));
	EXPECT_EQ(TGitH::NOTES, type);
}

TEST(CGit, GetRepository)
{
	CAutoTempDir tempdir;
	CGit cgit;
	cgit.SetCurrentDirExact(tempdir.GetTempDir());

	wil::unique_git_repository repo = cgit.GetGitRepository();
	EXPECT_FALSE(repo.is_valid());

	cgit.SetCurrentDirExact(tempdir.GetTempDir() / L"aöäüb");
	ASSERT_TRUE(CreateDirectory(cgit.m_CurrentDir.c_str(), nullptr));

	CString output;
	EXPECT_EQ(0, GitCall::Run({L"git.exe", L"init", L"-b", L"master"}, &output));
	EXPECT_STRNE(L"", output);

	wil::unique_git_repository repo2 = cgit.GetGitRepository(); // this tests GetGitRepository as well as m_Git.GetGitPathStringA
	EXPECT_TRUE(repo2.is_valid());

	cgit.SetCurrentDirExact(tempdir.GetTempDir() / L"aöäüb.git");
	ASSERT_TRUE(CreateDirectory(cgit.m_CurrentDir.c_str(), nullptr));

	output.Empty();
	EXPECT_EQ(0, GitCall::Run({L"git.exe", L"init", L"--bare", L"-b", L"master"}, &output));
	EXPECT_STRNE(L"", output);

	wil::unique_git_repository repo3 = cgit.GetGitRepository(); // this tests GetGitRepository as well as m_Git.GetGitPathStringA
	EXPECT_TRUE(repo3.is_valid());
}

TEST_P(CBasicGitWithEmptyRepositoryFixture, IsInitRepos_GetInitAddList)
{
	EXPECT_STREQ(L"master", m_Git.GetCurrentBranch());

	std::string output;
	auto testFile = m_Dir.GetTempDir() / L"test.txt";

	CTGitPathList addedFiles;

	EXPECT_TRUE(m_Git.IsInitRepos());
	EXPECT_EQ(0, m_Git.GetInitAddList(addedFiles));
	EXPECT_TRUE(addedFiles.IsEmpty());

	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is testing file."));
	EXPECT_EQ(0, m_Git.GetInitAddList(addedFiles));
	EXPECT_TRUE(addedFiles.IsEmpty());
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"test.txt"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_EQ(0, m_Git.GetInitAddList(addedFiles));
	ASSERT_EQ(1, addedFiles.GetCount());
	EXPECT_STREQ(L"test.txt", addedFiles[0].GetGitPathString().c_str());

	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"commit", L"-m", L"Add test.txt"}, &output));
	EXPECT_NE("", output);

	EXPECT_FALSE(m_Git.IsInitRepos());

	EXPECT_STREQ(L"master", m_Git.GetCurrentBranch());
}

TEST_P(CBasicGitWithTestRepoFixture, IsInitRepos)
{
	EXPECT_FALSE(m_Git.IsInitRepos());

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--orphan", L"orphanic"}, &output));
	EXPECT_NE("", output);

	EXPECT_TRUE(m_Git.IsInitRepos());
}

TEST_P(CBasicGitWithTestRepoFixture, HasWorkingTreeConflicts)
{
	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);

	EXPECT_EQ(FALSE, m_Git.HasWorkingTreeConflicts());

	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"merge", L"forconflict"}, &output));
	EXPECT_NE("", output);
	EXPECT_EQ(FALSE, m_Git.HasWorkingTreeConflicts());

	output.clear();
	EXPECT_EQ(1, m_Git.Run({L"git.exe", L"merge", L"simple-conflict"}, &output));
	EXPECT_NE("", output);
	EXPECT_EQ(TRUE, m_Git.HasWorkingTreeConflicts());

	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);

	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"forconflict"}, &output));
	EXPECT_NE("", output);

	output.clear();
	EXPECT_EQ(1, m_Git.Run({L"git.exe", L"merge", L"simple-conflict"}, &output));
	EXPECT_NE("", output);
	EXPECT_EQ(TRUE, m_Git.HasWorkingTreeConflicts());
}

TEST_P(CBasicGitWithTestRepoFixture, GetCurrentBranch)
{
	EXPECT_STREQ(L"master", m_Git.GetCurrentBranch());
	EXPECT_STREQ(L"master", m_Git.GetCurrentBranch(true));

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"simple-conflict"}, &output));
	EXPECT_STREQ(L"simple-conflict", m_Git.GetCurrentBranch());
	EXPECT_STREQ(L"simple-conflict", m_Git.GetCurrentBranch(true));

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"subdir/branch"}, &output));
	EXPECT_STREQ(L"subdir/branch", m_Git.GetCurrentBranch());
	EXPECT_STREQ(L"subdir/branch", m_Git.GetCurrentBranch(true));

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"560deea87853158b22d0c0fd73f60a458d47838a"}, &output));
	EXPECT_STREQ(L"(no branch)", m_Git.GetCurrentBranch());
	EXPECT_STREQ(L"560deea87853158b22d0c0fd73f60a458d47838a", m_Git.GetCurrentBranch(true));

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"-b", L"mäin", L"560deea87853158b22d0c0fd73f60a458d47838a"}, &output));
	EXPECT_STREQ(L"mäin", m_Git.GetCurrentBranch());

	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--orphan", L"orphanic"}, &output));
	EXPECT_NE("", output);

	EXPECT_STREQ(L"orphanic", m_Git.GetCurrentBranch());
	EXPECT_STREQ(L"orphanic", m_Git.GetCurrentBranch(true));
}

TEST_P(CBasicGitWithTestRepoBareFixture, GetCurrentBranch)
{
	EXPECT_STREQ(L"master", m_Git.GetCurrentBranch());
	EXPECT_STREQ(L"master", m_Git.GetCurrentBranch(true));
}

static void IsLocalBranch(CGit& m_Git)
{
	EXPECT_TRUE(m_Git.IsLocalBranch(L"master"));
	EXPECT_TRUE(m_Git.IsLocalBranch(L"subdir/branch"));

	EXPECT_FALSE(m_Git.IsLocalBranch(L"no_branch_in_repo"));

	EXPECT_FALSE(m_Git.IsLocalBranch(L"commits")); // notes/commits

	EXPECT_FALSE(m_Git.IsLocalBranch(L"stash"));

	EXPECT_FALSE(m_Git.IsLocalBranch(L"3686b9cf74f1a4ef96d6bfe736595ef9abf0fb8d"));

	// exist tags
	EXPECT_FALSE(m_Git.IsLocalBranch(L"normal-tag"));
	EXPECT_FALSE(m_Git.IsLocalBranch(L"also-signed"));
}

TEST_P(CBasicGitWithTestRepoFixture, IsLocalBranch)
{
	IsLocalBranch(m_Git);
}

TEST_P(CBasicGitWithTestRepoBareFixture, IsLocalBranch)
{
	IsLocalBranch(m_Git);
}

static void BranchTagExists_IsBranchTagNameUnique(CGit& m_Git)
{
	EXPECT_TRUE(m_Git.BranchTagExists(L"master", true));
	EXPECT_FALSE(m_Git.BranchTagExists(L"origin/master", true));
	EXPECT_FALSE(m_Git.BranchTagExists(L"normal-tag", true));
	EXPECT_FALSE(m_Git.BranchTagExists(L"also-signed", true));
	EXPECT_FALSE(m_Git.BranchTagExists(L"wuseldusel", true));

	EXPECT_FALSE(m_Git.BranchTagExists(L"master", false));
	EXPECT_TRUE(m_Git.BranchTagExists(L"normal-tag", false));
	EXPECT_TRUE(m_Git.BranchTagExists(L"also-signed", false));
	EXPECT_FALSE(m_Git.BranchTagExists(L"wuseldusel", false));

	EXPECT_TRUE(m_Git.IsBranchTagNameUnique(L"master"));
	EXPECT_TRUE(m_Git.IsBranchTagNameUnique(L"simpleconflict"));
	EXPECT_TRUE(m_Git.IsBranchTagNameUnique(L"normal-tag"));
	EXPECT_TRUE(m_Git.IsBranchTagNameUnique(L"also-signed"));
	EXPECT_TRUE(m_Git.IsBranchTagNameUnique(L"origin/master"));

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"tag", L"master", L"HEAD~2"}, &output));
	EXPECT_STREQ("", output);

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"branch", L"normal-tag", L"HEAD~2"}, &output));
	EXPECT_STREQ("", output);

	EXPECT_FALSE(m_Git.IsBranchTagNameUnique(L"master"));
	EXPECT_FALSE(m_Git.IsBranchTagNameUnique(L"normal-tag"));
	EXPECT_TRUE(m_Git.IsBranchTagNameUnique(L"also-signed"));
}

TEST_P(CBasicGitWithTestRepoFixture, BranchTagExists_IsBranchTagNameUnique)
{
	BranchTagExists_IsBranchTagNameUnique(m_Git);
}

TEST_P(CBasicGitWithTestRepoBareFixture, BranchTagExists_IsBranchTagNameUnique)
{
	BranchTagExists_IsBranchTagNameUnique(m_Git);
}

static void GetFullRefName(CGit& m_Git)
{
	EXPECT_STREQ(L"", m_Git.GetFullRefName(L"does_not_exist"s));
	EXPECT_STREQ(L"refs/heads/master", m_Git.GetFullRefName(L"master"s));
	EXPECT_STREQ(L"refs/remotes/origin/master", m_Git.GetFullRefName(L"origin/master"s));
	EXPECT_STREQ(L"refs/tags/normal-tag", m_Git.GetFullRefName(L"normal-tag"s));
	EXPECT_STREQ(L"refs/tags/also-signed", m_Git.GetFullRefName(L"also-signed"s));

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"tag", L"master", L"HEAD~2"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_STREQ(L"", m_Git.GetFullRefName(L"master"s));
	EXPECT_STREQ(L"refs/remotes/origin/master", m_Git.GetFullRefName(L"origin/master"s));

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"branch", L"normal-tag", L"HEAD~2"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_STREQ(L"", m_Git.GetFullRefName(L"normal-tag"s));

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"branch", L"origin/master", L"HEAD~2"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_STREQ(L"", m_Git.GetFullRefName(L"origin/master"s));
}

TEST_P(CBasicGitWithTestRepoFixture, GetFullRefName)
{
	GetFullRefName(m_Git);

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--orphan", L"orphanic"}, &output));
	EXPECT_NE("", output);
	EXPECT_STREQ(L"", m_Git.GetFullRefName(L"orphanic"s));
}

TEST_P(CBasicGitWithTestRepoBareFixture, GetFullRefName)
{
	GetFullRefName(m_Git);
}

TEST_P(CBasicGitWithEmptyRepositoryFixture, GetRemoteTrackedBranch)
{
	CString remote, branch;
	m_Git.GetRemoteTrackedBranchForHEAD(remote, branch);
	EXPECT_STREQ(L"", remote);
	EXPECT_STREQ(L"", branch);

	m_Git.GetRemoteTrackedBranch(L"master", remote, branch);
	EXPECT_STREQ(L"", remote);
	EXPECT_STREQ(L"", branch);

	m_Git.GetRemoteTrackedBranch(L"non-existing", remote, branch);
	EXPECT_STREQ(L"", remote);
	EXPECT_STREQ(L"", branch);
}

static void GetRemoteTrackedBranch(CGit& m_Git)
{
	CString remote, branch;
	m_Git.GetRemoteTrackedBranchForHEAD(remote, branch);
	EXPECT_STREQ(L"origin", remote);
	EXPECT_STREQ(L"master", branch);

	remote.Empty();
	branch.Empty();
	m_Git.GetRemoteTrackedBranch(L"master", remote, branch);
	EXPECT_STREQ(L"origin", remote);
	EXPECT_STREQ(L"master", branch);

	remote.Empty();
	branch.Empty();
	m_Git.GetRemoteTrackedBranch(L"non-existing", remote, branch);
	EXPECT_STREQ(L"", remote);
	EXPECT_STREQ(L"", branch);
}

TEST_P(CBasicGitWithTestRepoFixture, GetRemoteTrackedBranch)
{
	GetRemoteTrackedBranch(m_Git);
}

TEST_P(CBasicGitWithTestRepoBareFixture, GetRemoteTrackedBranch)
{
	GetRemoteTrackedBranch(m_Git);
}

TEST_P(CBasicGitWithEmptyRepositoryFixture, GetRemotePushBranch)
{
	CString remote, branch;
	m_Git.GetRemotePushBranch(L"master", remote, branch);
	EXPECT_STREQ(L"", remote);
	EXPECT_STREQ(L"", branch);

	m_Git.GetRemotePushBranch(L"non-existing", remote, branch);
	EXPECT_STREQ(L"", remote);
	EXPECT_STREQ(L"", branch);
}

static void GetRemotePushBranch(CGit& m_Git)
{
	CString remote, branch;
	m_Git.GetRemotePushBranch(L"master", remote, branch);
	EXPECT_STREQ(L"origin", remote);
	EXPECT_STREQ(L"master", branch);

	remote.Empty();
	branch.Empty();
	m_Git.GetRemotePushBranch(L"non-existing", remote, branch);
	EXPECT_STREQ(L"", remote);
	EXPECT_STREQ(L"", branch);

	const wil::unique_git_repository repo(m_Git.GetGitRepository());
	ASSERT_TRUE(repo.is_valid());
	wil::unique_git_config config;
	git_repository_config(config.put(), repo.get());
	ASSERT_TRUE(config.is_valid());

	remote.Empty();
	branch.Empty();
	EXPECT_EQ(0, git_config_set_string(config.get(), "remote.pushDefault", "originpush2"));
	m_Git.GetRemotePushBranch(L"master", remote, branch);
	EXPECT_STREQ(L"originpush2", remote);
	EXPECT_STREQ(L"master", branch);

	remote.Empty();
	branch.Empty();
	EXPECT_EQ(0, git_config_set_string(config.get(), "branch.master.pushremote", "originpush3"));
	m_Git.GetRemotePushBranch(L"master", remote, branch);
	EXPECT_STREQ(L"originpush3", remote);
	EXPECT_STREQ(L"master", branch);

	remote.Empty();
	branch.Empty();
	EXPECT_EQ(0, git_config_set_string(config.get(), "branch.master.pushbranch", "masterbranch2"));
	m_Git.GetRemotePushBranch(L"master", remote, branch);
	EXPECT_STREQ(L"originpush3", remote);
	EXPECT_STREQ(L"masterbranch2", branch);

	remote.Empty();
	branch.Empty();
	m_Git.GetRemotePushBranch(L"non-existing", remote, branch);
	EXPECT_STREQ(L"originpush2", remote);
	EXPECT_STREQ(L"", branch);
}

TEST_P(CBasicGitWithTestRepoFixture, GetRemotePushBranch)
{
	GetRemotePushBranch(m_Git);
}

TEST_P(CBasicGitWithTestRepoBareFixture, GetRemotePushBranch)
{
	GetRemotePushBranch(m_Git);
}

static void IsUnbornBranch(CGit& m_Git)
{
	EXPECT_FALSE(m_Git.IsUnbornBranch(L""));
	EXPECT_FALSE(m_Git.IsUnbornBranch(L"HEAD"));
	EXPECT_FALSE(m_Git.IsUnbornBranch(L"master"));
	EXPECT_FALSE(m_Git.IsUnbornBranch(L"refs/heads/master"));

	EXPECT_FALSE(m_Git.IsUnbornBranch(L"non-existing"));
}

TEST_P(CBasicGitWithTestRepoFixture, IsUnbornBranch)
{
	IsUnbornBranch(m_Git);

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--orphan", L"orphanic"}, &output));
	EXPECT_NE("", output);
	EXPECT_TRUE(m_Git.IsUnbornBranch(L""));
	EXPECT_TRUE(m_Git.IsUnbornBranch(L"HEAD"));
	EXPECT_TRUE(m_Git.IsUnbornBranch(L"orphanic"));
	EXPECT_TRUE(m_Git.IsUnbornBranch(L"refs/heads/orphanic"));
	EXPECT_FALSE(m_Git.IsUnbornBranch(L"master"));
}

TEST_P(CBasicGitWithTestRepoBareFixture, IsUnbornBranch)
{
	IsUnbornBranch(m_Git);
}

static void FETCHHEAD(CGit& m_Git, bool isBare)
{
	fs::path repoDir = m_Git.m_CurrentDir;
	if (!isBare)
		repoDir /= L".git";

	std::vector<std::wstring> list;
	EXPECT_EQ(0, m_Git.GetBranchList(list, nullptr));
	EXPECT_EQ(7U, list.size());
	list.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(list, nullptr, TGitH::BRANCH_LOCAL_F));
	EXPECT_EQ(7U, list.size());

	EXPECT_STREQ(L"HEAD", m_Git.FixBranchName(L"HEAD"));
	EXPECT_STREQ(L"master", m_Git.FixBranchName(L"master"));
	EXPECT_STREQ(L"non-existing", m_Git.FixBranchName(L"non-existing"));
	CString branch = L"master";
	EXPECT_STREQ(L"master", m_Git.FixBranchName_Mod(branch));
	EXPECT_STREQ(L"master", branch);
	branch = L"non-existing";
	EXPECT_STREQ(L"non-existing", m_Git.FixBranchName_Mod(branch));
	EXPECT_STREQ(L"non-existing", branch);
	CGitHash hash;
	EXPECT_NE(0, m_Git.GetHash(hash, L"FETCH_HEAD"));

	auto testFile = repoDir / L"FETCH_HEAD";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb		branch 'master' of https://code.google.com/p/tortoisegit\n737878a4e2eabfa4fab580867c2b060c70999d31	not-for-merge	branch 'extend_hooks' of https://code.google.com/p/tortoisegit\n"));

	list.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(list, nullptr));
	EXPECT_EQ(7U, list.size());
	list.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(list, nullptr, TGitH::BRANCH_LOCAL_F));
	EXPECT_EQ(8U, list.size());

	EXPECT_STREQ(L"master", m_Git.FixBranchName(L"master"));
	EXPECT_STREQ(L"non-existing", m_Git.FixBranchName(L"non-existing"));
	EXPECT_STREQ(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", m_Git.FixBranchName(L"FETCH_HEAD"));
	branch = L"HEAD";
	EXPECT_STREQ(L"HEAD", m_Git.FixBranchName_Mod(branch));
	EXPECT_STREQ(L"HEAD", branch);
	branch = L"master";
	EXPECT_STREQ(L"master", m_Git.FixBranchName_Mod(branch));
	EXPECT_STREQ(L"master", branch);
	branch = L"non-existing";
	EXPECT_STREQ(L"non-existing", m_Git.FixBranchName_Mod(branch));
	EXPECT_STREQ(L"non-existing", branch);
	branch = L"FETCH_HEAD";
	EXPECT_STREQ(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", m_Git.FixBranchName_Mod(branch));
	EXPECT_STREQ(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", branch);
	EXPECT_EQ(0, m_Git.GetHash(hash, L"FETCH_HEAD"));
	EXPECT_STREQ(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", hash.ToString());

	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"737878a4e2eabfa4fab580867c2b060c70999d31	not-for-merge	branch 'extend_hooks' of https://code.google.com/p/tortoisegit\nb9ef30183497cdad5c30b88d32dc1bed7951dfeb		branch 'master' of https://code.google.com/p/tortoisegit\n"));

	list.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(list, nullptr));
	EXPECT_EQ(7U, list.size());
	list.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(list, nullptr, TGitH::BRANCH_LOCAL_F));
	EXPECT_EQ(8U, list.size());

	EXPECT_STREQ(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", m_Git.FixBranchName(L"FETCH_HEAD"));
	branch = L"FETCH_HEAD";
	EXPECT_STREQ(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", m_Git.FixBranchName_Mod(branch));
	EXPECT_STREQ(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", branch);
	// libgit2 fails here
	//EXPECT_EQ(0, m_Git.GetHash(hash, L"FETCH_HEAD"));
	//EXPECT_STREQ(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", hash.ToString());
}

TEST_P(CBasicGitWithTestRepoFixture, FETCHHEAD)
{
	FETCHHEAD(m_Git, false);
}

TEST_P(CBasicGitWithTestRepoBareFixture, FETCHHEAD)
{
	FETCHHEAD(m_Git, true);
}

TEST_P(CBasicGitWithTestRepoFixture, IsFastForward)
{
	CGitHash commonAncestor;
	EXPECT_TRUE(m_Git.IsFastForward(L"origin/master", L"master", &commonAncestor));
	EXPECT_STREQ(L"a9d53b535cb49640a6099860ac4999f5a0857b91", commonAncestor.ToString());

	EXPECT_FALSE(m_Git.IsFastForward(L"simple-conflict", L"master", &commonAncestor));
	EXPECT_STREQ(L"b02add66f48814a73aa2f0876d6bbc8662d6a9a8", commonAncestor.ToString());
}

static void GetHash(CGit& m_Git, bool orphanedBranch)
{
	CGitHash hash;
	if (!orphanedBranch)
	{
		EXPECT_EQ(0, m_Git.GetHash(hash, L"HEAD"));
		EXPECT_STREQ(L"7c3cbfe13a929d2291a574dca45e4fd2d2ac1aa6", hash.ToString());
		EXPECT_EQ(0, m_Git.GetHash(hash, L"HEAD~1"));
		EXPECT_STREQ(L"1fc3c9688e27596d8717b54f2939dc951568f6cb", hash.ToString());
	}
	else
	{
		EXPECT_EQ(0, m_Git.GetHash(hash, L"HEAD"));
		EXPECT_TRUE(hash.IsEmpty());
		EXPECT_NE(0, m_Git.GetHash(hash, L"HEAD~1"));
	}
	EXPECT_EQ(0, m_Git.GetHash(hash, L"ff1fbef1a54a9849afd4a5e94d2ca4d80d5b96c2"));
	EXPECT_STREQ(L"ff1fbef1a54a9849afd4a5e94d2ca4d80d5b96c2", hash.ToString());
	EXPECT_EQ(0, m_Git.GetHash(hash, L"master"));
	EXPECT_STREQ(L"7c3cbfe13a929d2291a574dca45e4fd2d2ac1aa6", hash.ToString());
	EXPECT_EQ(0, m_Git.GetHash(hash, L"origin/master"));
	EXPECT_STREQ(L"a9d53b535cb49640a6099860ac4999f5a0857b91", hash.ToString());
	EXPECT_EQ(0, m_Git.GetHash(hash, L"49ecdfff36bfe2b9b499b33e5034f427e2fa54dd"));
	EXPECT_STREQ(L"49ecdfff36bfe2b9b499b33e5034f427e2fa54dd", hash.ToString());
	EXPECT_EQ(0, m_Git.GetHash(hash, L"normal-tag"));
	EXPECT_STREQ(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", hash.ToString());
	EXPECT_EQ(0, m_Git.GetHash(hash, L"all-files-signed"));
	EXPECT_STREQ(L"ab555b2776c6b700ad93848d0dd050e7d08be779", hash.ToString()); // maybe we need automatically to dereference it
	EXPECT_EQ(0, m_Git.GetHash(hash, L"all-files-signed^{}"));
	EXPECT_STREQ(L"313a41bc88a527289c87d7531802ab484715974f", hash.ToString());

	EXPECT_EQ(0, m_Git.GetHash(hash, L"fab555b2776c6b700fffffffffcccccaaaafe779")); // a hash that does not exist, we don't check for it's existance
	EXPECT_STREQ(L"fab555b2776c6b700fffffffffcccccaaaafe779", hash.ToString());

	EXPECT_NE(0, m_Git.GetHash(hash, L""));
	EXPECT_NE(0, m_Git.GetHash(hash, L"non-existing"));
	EXPECT_NE(0, m_Git.GetHash(hash, L"orphanic"));
}

TEST_P(CBasicGitWithTestRepoFixture, GetHash)
{
	GetHash(m_Git, false);
}

TEST_P(CBasicGitWithTestRepoBareFixture, GetHash)
{
	GetHash(m_Git, false);
}

TEST_P(CBasicGitWithTestRepoFixture, GetHash_OrphanBranch)
{
	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--orphan", L"orphanic"}, &output));
	EXPECT_NE("", output);

	GetHash(m_Git, true);
}

TEST_P(CBasicGitWithEmptyRepositoryFixture, GetHash_EmptyRepo)
{
	CGitHash hash;
	EXPECT_EQ(0, m_Git.GetHash(hash, L"HEAD"));
	EXPECT_TRUE(hash.IsEmpty());
}

TEST_P(CBasicGitWithEmptyRepositoryFixture, GetEmptyBranchesTagsRefs)
{
	std::vector<std::wstring> branches;
	int current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current));
	EXPECT_TRUE(branches.empty());
	EXPECT_EQ(-2, current); // not touched

	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current, TGitH::BRANCH_ALL));
	EXPECT_TRUE(branches.empty());
	EXPECT_EQ(-2, current); // not touched

	std::vector<std::wstring> tags;
	EXPECT_EQ(0, m_Git.GetTagList(tags));
	EXPECT_TRUE(tags.empty());

	std::vector<std::wstring> refs;
	EXPECT_EQ(0, m_Git.GetRefList(refs));
	EXPECT_TRUE(refs.empty());

	MAP_HASH_NAME map;
	EXPECT_EQ(0, m_Git.GetMapHashToFriendName(map));
	EXPECT_TRUE(map.empty());

	std::vector<std::wstring> remotes;
	EXPECT_EQ(0, m_Git.GetRemoteList(remotes));
	EXPECT_TRUE(remotes.empty());
}

static void GetBranchesTagsRefs(CGit& m_Git, config testConfig)
{
	std::vector<std::wstring> branches;
	int current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current));
	ASSERT_EQ(7U, branches.size());
	EXPECT_EQ(1, current);
	EXPECT_STREQ(L"forconflict", branches[0].c_str());
	EXPECT_STREQ(L"master", branches[1].c_str());
	EXPECT_STREQ(L"master2", branches[2].c_str());
	EXPECT_STREQ(L"signed-commit", branches[3].c_str());
	EXPECT_STREQ(L"simple-conflict", branches[4].c_str());
	EXPECT_STREQ(L"subdir/branch", branches[5].c_str());
	EXPECT_STREQ(L"tree-ref", branches[6].c_str());

	branches.clear();
	current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current, TGitH::BRANCH_ALL));
	ASSERT_EQ(8U, branches.size());
	EXPECT_EQ(1, current);
	EXPECT_STREQ(L"forconflict", branches[0].c_str());
	EXPECT_STREQ(L"master", branches[1].c_str());
	EXPECT_STREQ(L"master2", branches[2].c_str());
	EXPECT_STREQ(L"signed-commit", branches[3].c_str());
	EXPECT_STREQ(L"simple-conflict", branches[4].c_str());
	EXPECT_STREQ(L"subdir/branch", branches[5].c_str());
	EXPECT_STREQ(L"tree-ref", branches[6].c_str());
	EXPECT_STREQ(L"remotes/origin/master", branches[7].c_str());

	branches.clear();
	current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current, TGitH::BRANCH_ALL, true));
	ASSERT_EQ(7U, branches.size());
	EXPECT_EQ(-2, current); // not touched
	EXPECT_STREQ(L"forconflict", branches[0].c_str());
	EXPECT_STREQ(L"master2", branches[1].c_str());
	EXPECT_STREQ(L"signed-commit", branches[2].c_str());
	EXPECT_STREQ(L"simple-conflict", branches[3].c_str());
	EXPECT_STREQ(L"subdir/branch", branches[4].c_str());
	EXPECT_STREQ(L"tree-ref", branches[5].c_str());
	EXPECT_STREQ(L"remotes/origin/master", branches[6].c_str());

	branches.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(branches, nullptr, TGitH::BRANCH_ALL, true));
	ASSERT_EQ(7U, branches.size());
	EXPECT_STREQ(L"forconflict", branches[0].c_str());
	EXPECT_STREQ(L"master2", branches[1].c_str());
	EXPECT_STREQ(L"signed-commit", branches[2].c_str());
	EXPECT_STREQ(L"simple-conflict", branches[3].c_str());
	EXPECT_STREQ(L"subdir/branch", branches[4].c_str());
	EXPECT_STREQ(L"tree-ref", branches[5].c_str());
	EXPECT_STREQ(L"remotes/origin/master", branches[6].c_str());

	branches.clear();
	current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current, TGitH::BRANCH_REMOTE));
	ASSERT_EQ(1U, branches.size());
	EXPECT_EQ(-2, current); // not touched
	EXPECT_STREQ(L"remotes/origin/master", branches[0].c_str());

	std::vector<std::wstring> tags;
	EXPECT_EQ(0, m_Git.GetTagList(tags));
	ASSERT_EQ(3U, tags.size());
	EXPECT_STREQ(L"all-files-signed", tags[0].c_str());
	EXPECT_STREQ(L"also-signed", tags[1].c_str());
	EXPECT_STREQ(L"normal-tag", tags[2].c_str());

	std::vector<std::wstring> refs;
	EXPECT_EQ(0, m_Git.GetRefList(refs));
	ASSERT_EQ(13U, refs.size());
	EXPECT_STREQ(L"refs/heads/forconflict", refs[0].c_str());
	EXPECT_STREQ(L"refs/heads/master", refs[1].c_str());
	EXPECT_STREQ(L"refs/heads/master2", refs[2].c_str());
	EXPECT_STREQ(L"refs/heads/signed-commit", refs[3].c_str());
	EXPECT_STREQ(L"refs/heads/simple-conflict", refs[4].c_str());
	EXPECT_STREQ(L"refs/heads/subdir/branch", refs[5].c_str());
	EXPECT_STREQ(L"refs/heads/tree-ref", refs[6].c_str());
	EXPECT_STREQ(L"refs/notes/commits", refs[7].c_str());
	EXPECT_STREQ(L"refs/remotes/origin/master", refs[8].c_str());
	EXPECT_STREQ(L"refs/stash", refs[9].c_str());
	EXPECT_STREQ(L"refs/tags/all-files-signed", refs[10].c_str());
	EXPECT_STREQ(L"refs/tags/also-signed", refs[11].c_str());
	EXPECT_STREQ(L"refs/tags/normal-tag", refs[12].c_str());

	MAP_HASH_NAME map;
	EXPECT_EQ(0, m_Git.GetMapHashToFriendName(map));
	if (testConfig == GIT_CLI || testConfig == LIBGIT)
		ASSERT_EQ(14U, map.size()); // also contains the undereferenced tags with hashes
	else
		ASSERT_EQ(12U, map.size());

	ASSERT_EQ(1U, map[CGitHash::FromHexStr(L"7c3cbfe13a929d2291a574dca45e4fd2d2ac1aa6")].size());
	EXPECT_STREQ(L"refs/heads/master", map[CGitHash::FromHexStr(L"7c3cbfe13a929d2291a574dca45e4fd2d2ac1aa6")][0].c_str());
	ASSERT_EQ(1U, map[CGitHash::FromHexStr(L"4c5c93d2a0b368bc4570d5ec02ab03b9c4334d44")].size());
	EXPECT_STREQ(L"refs/heads/signed-commit", map[CGitHash::FromHexStr(L"4c5c93d2a0b368bc4570d5ec02ab03b9c4334d44")][0].c_str());
	ASSERT_EQ(1U, map[CGitHash::FromHexStr(L"31ff87c86e9f6d3853e438cb151043f30f09029a")].size());
	EXPECT_STREQ(L"refs/heads/subdir/branch", map[CGitHash::FromHexStr(L"31ff87c86e9f6d3853e438cb151043f30f09029a")][0].c_str());
	ASSERT_EQ(1U, map[CGitHash::FromHexStr(L"5e702e1712aa6f8cd8e0328a87be006f3a923710")].size());
	EXPECT_STREQ(L"refs/notes/commits", map[CGitHash::FromHexStr(L"5e702e1712aa6f8cd8e0328a87be006f3a923710")][0].c_str());
	ASSERT_EQ(1U, map[CGitHash::FromHexStr(L"18da7c332dcad0f37f9977d9176dce0b0c66f3eb")].size());
	EXPECT_STREQ(L"refs/stash", map[CGitHash::FromHexStr(L"18da7c332dcad0f37f9977d9176dce0b0c66f3eb")][0].c_str());
	ASSERT_EQ(1U, map[CGitHash::FromHexStr(L"c5b89de0335fd674e2e421ac4543098cb2f22cde")].size());
	EXPECT_STREQ(L"refs/heads/simple-conflict", map[CGitHash::FromHexStr(L"c5b89de0335fd674e2e421ac4543098cb2f22cde")][0].c_str());
	ASSERT_EQ(1U, map[CGitHash::FromHexStr(L"10385764a4d42d7428bbeb245015f8f338fc1e40")].size());
	EXPECT_STREQ(L"refs/heads/forconflict", map[CGitHash::FromHexStr(L"10385764a4d42d7428bbeb245015f8f338fc1e40")][0].c_str());
	ASSERT_EQ(2U, map[CGitHash::FromHexStr(L"49ecdfff36bfe2b9b499b33e5034f427e2fa54dd")].size());
	EXPECT_STREQ(L"refs/heads/master2", map[CGitHash::FromHexStr(L"49ecdfff36bfe2b9b499b33e5034f427e2fa54dd")][0].c_str());
	EXPECT_STREQ(L"refs/tags/also-signed^{}", map[CGitHash::FromHexStr(L"49ecdfff36bfe2b9b499b33e5034f427e2fa54dd")][1].c_str());
	ASSERT_EQ(1U, map[CGitHash::FromHexStr(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb")].size());//
	EXPECT_STREQ(L"refs/tags/normal-tag", map[CGitHash::FromHexStr(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb")][0].c_str());
	ASSERT_EQ(1U, map[CGitHash::FromHexStr(L"a9d53b535cb49640a6099860ac4999f5a0857b91")].size());
	EXPECT_STREQ(L"refs/remotes/origin/master", map[CGitHash::FromHexStr(L"a9d53b535cb49640a6099860ac4999f5a0857b91")][0].c_str());
	ASSERT_EQ(1U, map[CGitHash::FromHexStr(L"313a41bc88a527289c87d7531802ab484715974f")].size());
	EXPECT_STREQ(L"refs/tags/all-files-signed^{}", map[CGitHash::FromHexStr(L"313a41bc88a527289c87d7531802ab484715974f")][0].c_str());

	std::vector<std::wstring> remotes;
	EXPECT_EQ(0, m_Git.GetRemoteList(remotes));
	ASSERT_EQ(1U, remotes.size());
	EXPECT_STREQ(L"origin", remotes[0].c_str());

	EXPECT_EQ(-1, m_Git.DeleteRef(L"refs/tags/gibbednet"));
	branches.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(branches, nullptr, TGitH::BRANCH_ALL));
	EXPECT_EQ(8U, branches.size());
	tags.clear();
	EXPECT_EQ(0, m_Git.GetTagList(tags));
	EXPECT_EQ(3U, tags.size());
	refs.clear();
	EXPECT_EQ(0, m_Git.GetRefList(refs));
	EXPECT_EQ(13U, refs.size());

	EXPECT_EQ(-1, m_Git.DeleteRef(L"refs/heads/gibbednet"));
	branches.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(branches, nullptr, TGitH::BRANCH_ALL));
	EXPECT_EQ(8U, branches.size());
	tags.clear();
	EXPECT_EQ(0, m_Git.GetTagList(tags));
	EXPECT_EQ(3U, tags.size());
	refs.clear();
	EXPECT_EQ(0, m_Git.GetRefList(refs));
	EXPECT_EQ(13U, refs.size());

	EXPECT_EQ(-1, m_Git.DeleteRef(L"refs/remotes/origin/gibbednet"));
	branches.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(branches, nullptr, TGitH::BRANCH_ALL));
	EXPECT_EQ(8U, branches.size());
	tags.clear();
	EXPECT_EQ(0, m_Git.GetTagList(tags));
	EXPECT_EQ(3U, tags.size());
	refs.clear();
	EXPECT_EQ(0, m_Git.GetRefList(refs));
	EXPECT_EQ(13U, refs.size());

	EXPECT_EQ(0, m_Git.DeleteRef(L"refs/tags/normal-tag"));
	branches.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(branches, nullptr, TGitH::BRANCH_ALL));
	EXPECT_EQ(8U, branches.size());
	tags.clear();
	EXPECT_EQ(0, m_Git.GetTagList(tags));
	EXPECT_EQ(2U, tags.size());
	refs.clear();
	EXPECT_EQ(0, m_Git.GetRefList(refs));
	EXPECT_EQ(12U, refs.size());

	EXPECT_EQ(0, m_Git.DeleteRef(L"refs/tags/all-files-signed^{}"));
	branches.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(branches, nullptr, TGitH::BRANCH_ALL));
	EXPECT_EQ(8U, branches.size());
	tags.clear();
	EXPECT_EQ(0, m_Git.GetTagList(tags));
	EXPECT_EQ(1U, tags.size());
	refs.clear();
	EXPECT_EQ(0, m_Git.GetRefList(refs));
	EXPECT_EQ(11U, refs.size());

	EXPECT_EQ(0, m_Git.DeleteRef(L"refs/heads/subdir/branch"));
	branches.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(branches, nullptr, TGitH::BRANCH_ALL));
	EXPECT_EQ(7U, branches.size());
	tags.clear();
	EXPECT_EQ(0, m_Git.GetTagList(tags));
	EXPECT_EQ(1U, tags.size());
	refs.clear();
	EXPECT_EQ(0, m_Git.GetRefList(refs));
	EXPECT_EQ(10U, refs.size());

	EXPECT_EQ(0, m_Git.DeleteRef(L"refs/remotes/origin/master"));
	branches.clear();
	EXPECT_EQ(0, m_Git.GetBranchList(branches, nullptr, TGitH::BRANCH_ALL));
	EXPECT_EQ(6U, branches.size());
	tags.clear();
	EXPECT_EQ(0, m_Git.GetTagList(tags));
	EXPECT_EQ(1U, tags.size());
	refs.clear();
	EXPECT_EQ(0, m_Git.GetRefList(refs));
	EXPECT_EQ(9U, refs.size());
}

TEST_P(CBasicGitWithTestRepoFixture, GetBranchesTagsRefs)
{
	GetBranchesTagsRefs(m_Git, GetParam());
}

TEST_P(CBasicGitWithTestRepoBareFixture, GetBranchesTagsRefs)
{
	GetBranchesTagsRefs(m_Git, GetParam());
}

TEST_P(CBasicGitWithTestRepoFixture, GetBranchList_orphan)
{
	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--orphan", L"orphanic"}, &output));
	EXPECT_NE("", output);

	std::vector<std::wstring> branches;
	int current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current));
	ASSERT_EQ(7U, branches.size());
	EXPECT_EQ(-2, current);
	EXPECT_STREQ(L"forconflict", branches[0].c_str());
	EXPECT_STREQ(L"master", branches[1].c_str());
	EXPECT_STREQ(L"master2", branches[2].c_str());
	EXPECT_STREQ(L"signed-commit", branches[3].c_str());
	EXPECT_STREQ(L"simple-conflict", branches[4].c_str());
	EXPECT_STREQ(L"subdir/branch", branches[5].c_str());
	EXPECT_STREQ(L"tree-ref", branches[6].c_str());
}

TEST_P(CBasicGitWithTestRepoFixture, GetBranchList_utf8)
{
	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"-b", L"branch_u570Bu7ACB1du043A"}, &output));
	EXPECT_NE("", output);

	std::vector<std::wstring> branches;
	int current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current));
	ASSERT_EQ(8U, branches.size());
	EXPECT_EQ(0, current);
	EXPECT_STREQ(L"branch_\u570B\u7ACB1d\u043A", branches[0].c_str());
	EXPECT_STREQ(L"forconflict", branches[1].c_str());
	EXPECT_STREQ(L"master", branches[2].c_str());
	EXPECT_STREQ(L"master2", branches[3].c_str());
	EXPECT_STREQ(L"signed-commit", branches[4].c_str());
	EXPECT_STREQ(L"simple-conflict", branches[5].c_str());
	EXPECT_STREQ(L"subdir/branch", branches[6].c_str());
	EXPECT_STREQ(L"tree-ref", branches[7].c_str());
}

TEST_P(CBasicGitWithTestRepoFixture, GetBranchList_detachedhead)
{
	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"a9d53b535cb49640a6099860ac4999f5a0857b91"}, &output));
	EXPECT_NE("", output);

	std::vector<std::wstring> branches;
	int current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current));
	ASSERT_EQ(7U, branches.size());
	EXPECT_EQ(-2, current);
	EXPECT_STREQ(L"forconflict", branches[0].c_str());
	EXPECT_STREQ(L"master", branches[1].c_str());
	EXPECT_STREQ(L"master2", branches[2].c_str());
	EXPECT_STREQ(L"signed-commit", branches[3].c_str());
	EXPECT_STREQ(L"simple-conflict", branches[4].c_str());
	EXPECT_STREQ(L"subdir/branch", branches[5].c_str());
	EXPECT_STREQ(L"tree-ref", branches[6].c_str());

	branches.clear();
	current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current, TGitH::BRANCH_LOCAL, true));
	ASSERT_EQ(7U, branches.size());
	EXPECT_EQ(-2, current);
	EXPECT_STREQ(L"forconflict", branches[0].c_str());
	EXPECT_STREQ(L"master", branches[1].c_str());
	EXPECT_STREQ(L"master2", branches[2].c_str());
	EXPECT_STREQ(L"signed-commit", branches[3].c_str());
	EXPECT_STREQ(L"simple-conflict", branches[4].c_str());
	EXPECT_STREQ(L"subdir/branch", branches[5].c_str());
	EXPECT_STREQ(L"tree-ref", branches[6].c_str());

	// cygwin and msys git fail here
	if (GitCall::ms_bCygwinGit || GitCall::ms_bMsys2Git)
		return;

	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"-b", L"(HEAD", L"a9d53b535cb49640a6099860ac4999f5a0857b91"}, &output));
	EXPECT_NE("", output);

	branches.clear();
	current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current));
	ASSERT_EQ(8U, branches.size());
	EXPECT_EQ(0, current);
	EXPECT_STREQ(L"(HEAD", branches[0].c_str());
	EXPECT_STREQ(L"forconflict", branches[1].c_str());
	EXPECT_STREQ(L"master", branches[2].c_str());
	EXPECT_STREQ(L"master2", branches[3].c_str());
	EXPECT_STREQ(L"signed-commit", branches[4].c_str());
	EXPECT_STREQ(L"simple-conflict", branches[5].c_str());
	EXPECT_STREQ(L"subdir/branch", branches[6].c_str());
	EXPECT_STREQ(L"tree-ref", branches[7].c_str());
}

TEST_P(CBasicGitWithEmptyBareRepositoryFixture, GetEmptyBranchesTagsRefs)
{
	EXPECT_STREQ(L"master", m_Git.GetCurrentBranch());

	std::vector<std::wstring> branches;
	int current = -2;
	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current));
	EXPECT_TRUE(branches.empty());
	EXPECT_EQ(-2, current); // not touched

	EXPECT_EQ(0, m_Git.GetBranchList(branches, &current, TGitH::BRANCH_ALL));
	EXPECT_TRUE(branches.empty());
	EXPECT_EQ(-2, current); // not touched

	std::vector<std::wstring> tags;
	EXPECT_EQ(0, m_Git.GetTagList(tags));
	EXPECT_TRUE(tags.empty());

	std::vector<std::wstring> refs;
	EXPECT_EQ(0, m_Git.GetRefList(refs));
	EXPECT_TRUE(refs.empty());

	MAP_HASH_NAME map;
	EXPECT_EQ(0, m_Git.GetMapHashToFriendName(map));
	EXPECT_TRUE(map.empty());

	std::vector<std::wstring> remotes;
	EXPECT_EQ(0, m_Git.GetRemoteList(remotes));
	EXPECT_TRUE(remotes.empty());
}

TEST_P(CBasicGitWithEmptyRepositoryFixture, CheckCleanWorkTree)
{
	// this test is known to fail with cygwin and also not enabled by default
	if (GetParam() == LIBGIT2_ALL && GitCall::ms_bCygwinGit)
		return;

	std::string output;
	auto testFile = m_Dir.GetTempDir() / L"test.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is testing file."));
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"test.txt"}, &output));
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"commit", L"-m", L"Add test.txt"}, &output));
	// repo with 1 versioned file
	EXPECT_NE("", output);
	EXPECT_TRUE(m_Git.CheckCleanWorkTree());
	EXPECT_TRUE(m_Git.CheckCleanWorkTree(true));

	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"Overwriting this testing file."));
	// repo with 1 modified versioned file
	EXPECT_FALSE(m_Git.CheckCleanWorkTree());
	EXPECT_FALSE(m_Git.CheckCleanWorkTree(true));

	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"test.txt"}, &output));
	// repo with 1 modified versioned and staged file
	EXPECT_STREQ("", output);
	EXPECT_FALSE(m_Git.CheckCleanWorkTree());
	EXPECT_TRUE(m_Git.CheckCleanWorkTree(true));

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"commit", L"-m", L"Modified test.txt"}, &output));
	testFile = m_Dir.GetTempDir() / L"test2.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is ANOTHER testing file."));
	EXPECT_TRUE(m_Git.CheckCleanWorkTree());
	EXPECT_TRUE(m_Git.CheckCleanWorkTree(true));

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--orphan", L"orphanic"}, &output));
	EXPECT_NE("", output);
	EXPECT_FALSE(m_Git.CheckCleanWorkTree());
	EXPECT_FALSE(m_Git.CheckCleanWorkTree(true));
}

TEST_P(CBasicGitWithTestRepoFixture, GetTagInfo)
{
	CString output;
	EXPECT_EQ(0, m_Git.GetTagInfo(L"refs/tags/all-files-signed", output, [](const __time64_t time) { EXPECT_EQ(1425490937, time); return L"DATE"; }));
	EXPECT_STREQ(L"tag all-files-signed\ntagger Sven Strickroth <email@cs-ware.de> DATE\n\nSigned tag\n-----BEGIN PGP SIGNATURE-----\nVersion: GnuPG v1.4.13 (MingW32)\n\niEYEABECAAYFAlT3RAEACgkQFkZnpPWp1MQ8igCffp485VEKdcgWPR/2dhkkE7ub\nB50AoLmVj/oGcJYKnKag4WqHyi1zoaTU\n=0WTc\n-----END PGP SIGNATURE-----\n", output);

	output.Empty();
	EXPECT_EQ(-1, m_Git.GetTagInfo(L"refs/tags/normal-tag", output, [](const __time64_t time) { EXPECT_EQ(0, time); return L"DATE"; }));
}

TEST(CGit, CEnvironment)
{
	CEnvironment env;
	env.clear(); // the constructor copies the process environment; this test starts from an empty one
	const LPWSTR* basePtr = env.put();
	ASSERT_TRUE(basePtr);
	EXPECT_FALSE(*basePtr);
	EXPECT_TRUE(env.empty());
	env.UnsetEnv(L"not-found");
	EXPECT_FALSE(env.get());
	EXPECT_STREQ(L"", env.GetEnv(L"test").c_str());
	env.SetEnv(L"key1", L"value1");
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_STREQ(L"value1", env.GetEnv(L"kEy1").c_str()); // check case insensitivity
	EXPECT_TRUE(*basePtr);
	EXPECT_EQ(env.get(), *basePtr);
	EXPECT_FALSE(env.empty());
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	env.UnsetEnv(L"key1"); // delete first
	EXPECT_FALSE(*basePtr);
	EXPECT_EQ(env.get(), *basePtr);
	EXPECT_TRUE(env.empty());
	env.SetEnv(L"key1", L"value1");
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_TRUE(*basePtr);
	EXPECT_EQ(env.get(), *basePtr);
	EXPECT_FALSE(env.empty());
	env.SetEnv(L"key2", L"value2");
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_STREQ(L"value2", env.GetEnv(L"key2").c_str());
	EXPECT_EQ(env.get(), *basePtr);
	env.UnsetEnv(L"not-found");
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_STREQ(L"value2", env.GetEnv(L"key2").c_str());
	env.UnsetEnv(L"key2"); // delete last
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_STREQ(L"", env.GetEnv(L"key2").c_str());
	env.SetEnv(L"key3", L"value3");
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_STREQ(L"value3", env.GetEnv(L"key3").c_str());
	env.SetEnv(L"key4", L"value4");
	env.UnsetEnv(L"value3"); // delete middle
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_STREQ(L"value4", env.GetEnv(L"key4").c_str());
	env.SetEnv(L"key5", L"value5");
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_STREQ(L"value4", env.GetEnv(L"key4").c_str());
	EXPECT_STREQ(L"value5", env.GetEnv(L"key5").c_str());
	env.SetEnv(L"key4", L"value4a");
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_STREQ(L"value4a", env.GetEnv(L"key4").c_str());
	EXPECT_STREQ(L"value5", env.GetEnv(L"key5").c_str());
	env.SetEnv(L"key5", L"value5a");
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_STREQ(L"value4a", env.GetEnv(L"key4").c_str());
	EXPECT_STREQ(L"value5a", env.GetEnv(L"key5").c_str());
#pragma warning(push)
#pragma warning(disable: 4996)
	CString windir = _wgetenv(L"windir");
#pragma warning(pop)
	env.CopyProcessEnvironment();
	EXPECT_STREQ(windir, env.GetEnv(L"windir").c_str());
	EXPECT_STREQ(L"value1", env.GetEnv(L"key1").c_str());
	EXPECT_STREQ(L"value4a", env.GetEnv(L"key4").c_str());
	EXPECT_STREQ(L"value5a", env.GetEnv(L"key5").c_str());
	env.clear();
	EXPECT_FALSE(*basePtr);
	EXPECT_TRUE(env.empty());
	EXPECT_STREQ(L"", env.GetEnv(L"key4").c_str());
	env.CopyProcessEnvironment();
	EXPECT_STREQ(windir, env.GetEnv(L"windir").c_str());
	EXPECT_TRUE(*basePtr);

	// make sure baseptr always points to current values
	EXPECT_EQ(env.get(), *basePtr);

	env.clear();
	CString path = L"c:\\windows;c:\\windows\\system32";
	env.SetEnv(L"PATH", tgit::str::StringView(path));
	env.AddToPath(L"");
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());
	env.AddToPath(L"c:\\windows");
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());
	env.AddToPath(L"c:\\windows\\");
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());
	env.AddToPath(L"c:\\windows\\system32");
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());
	env.AddToPath(L"c:\\windows\\system32\\");
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());
	path += L";c:\\windows\\system";
	env.AddToPath(L"c:\\windows\\system");
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());
	path += L";c:\\test";
	env.AddToPath(L"c:\\test\\");
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());
	env.AddToPath(L"c:\\test\\");
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());
	env.AddToPath(L"c:\\test");
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());
	path = L"c:\\windows;c:\\windows\\system32;";
	env.SetEnv(L"PATH", tgit::str::StringView(path));
	env.AddToPath(L"");
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());
	env.AddToPath(L"c:\\test");
	path += L"c:\\test";
	EXPECT_STREQ(path, env.GetEnv(L"PATH").c_str());

	// also test copy constructor
	CEnvironment env2(env);
	EXPECT_EQ(env2.get(), *env2.put());
	EXPECT_NE(env2.get(), *basePtr);

	// also test assignment operation
	CEnvironment env3a;
	basePtr = env3a.put();
	env3a.SetEnv(L"something", L"else");
	CEnvironment env3b;
	env3b = env3a;
	EXPECT_FALSE(env3b.empty());
	EXPECT_EQ(env3a.put(), basePtr);
	EXPECT_EQ(env3a.get(), *basePtr);
	EXPECT_NE(env3b.get(), *basePtr);
}

static std::vector<std::wstring> SplitEnvironmentBlock(const wchar_t* block)
{
	std::vector<std::wstring> entries;
	for (const wchar_t* p = block; p && *p; p += wcslen(p) + 1)
		entries.emplace_back(p);
	return entries;
}

TEST(CGit, CEnvironmentBlockLayout)
{
	// The serialized block is the actual contract with CreateProcess and with
	// gitdll, and nothing here used to look at it - the tests above only ever
	// checked what GetEnv gave back.
	CEnvironment env;
	env.clear(); // the constructor copies the process environment; the layout checks start from an empty one
	// nullptr rather than an empty block. CreateProcess reads nullptr as "inherit
	// the parent's environment" and an empty block as "the child gets none".
	EXPECT_EQ(nullptr, env.get());

	env.SetEnv(L"zulu", L"z");
	env.SetEnv(L"Alpha", L"a");
	env.SetEnv(L"mike", L"");

	auto entries = SplitEnvironmentBlock(env.get());
	ASSERT_EQ(3U, entries.size());
	// case-insensitive name order, which is the order Windows' own environment
	// blocks arrive in and which the flat block lost as soon as anything was added
	EXPECT_STREQ(L"Alpha=a", entries[0].c_str());
	EXPECT_STREQ(L"mike=", entries[1].c_str());
	EXPECT_STREQ(L"zulu=z", entries[2].c_str());

	// The spelling of the most recent SetEnv wins. Windows does not care, but the
	// cygwin and msys builds hand this block to a shell, which does.
	env.SetEnv(L"ALPHA", L"a2");
	entries = SplitEnvironmentBlock(env.get());
	ASSERT_EQ(3U, entries.size());
	EXPECT_STREQ(L"ALPHA=a2", entries[0].c_str());
	EXPECT_STREQ(L"a2", env.GetEnv(L"alpha").c_str());

	env.UnsetEnv(L"mike");
	entries = SplitEnvironmentBlock(env.get());
	ASSERT_EQ(2U, entries.size());

	env.clear();
	EXPECT_EQ(nullptr, env.get());
}

static void GetOneFile(CGit& m_Git)
{
	auto tmpFile = GitCall::GetTempFile();
	ASSERT_STRNE(L"", tmpFile);
	EXPECT_EQ(0, m_Git.GetOneFile(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", CTGitPath(L"utf8-nobom.txt"), tmpFile));
	CString fileContents;
	EXPECT_EQ(true, StringUtils::ReadStringFromTextFile(tmpFile, fileContents));
	struct _stat32 stat_buf{};
	EXPECT_EQ(0, _wstat32(tmpFile, &stat_buf));
	EXPECT_EQ(139, stat_buf.st_size);
	EXPECT_EQ(108, fileContents.GetLength());
	EXPECT_STREQ(L"ä#äf34ööcöäß€9875oe\r\nfgdjkglsfdg\r\nöäöü45g\r\nfdgi&§$%&hfdsgä\r\nä#äf34öööäß€9875oe\r\nöäcüpfgmfdg\r\n€fgfdsg\r\n45\r\näü", fileContents);
	::DeleteFile(tmpFile);
}

static void GetOneFile_utf8(CGit& m_Git)
{
	auto tmpFile = GitCall::GetTempFile();
	ASSERT_STRNE(L"", tmpFile);
	EXPECT_EQ(0, m_Git.GetOneFile(L"4c5c93d2a0b368bc4570d5ec02ab03b9c4334d44", CTGitPath(L"newfiles2 - Cöpy.txt"), tmpFile));
	CString fileContents;
	EXPECT_EQ(true, StringUtils::ReadStringFromTextFile(tmpFile, fileContents));
	struct _stat32 stat_buf {};
	EXPECT_EQ(0, _wstat32(tmpFile, &stat_buf));
	EXPECT_EQ(14, stat_buf.st_size);
	EXPECT_EQ(14, fileContents.GetLength());
	EXPECT_STREQ(L"more new stuff", fileContents);
	::DeleteFile(tmpFile);
}

TEST_P(CBasicGitWithTestRepoFixture, GetOneFile)
{
	GetOneFile(m_Git);
	GetOneFile_utf8(m_Git);

	// clean&smudge filters are not available for GetOneFile without libigt2
	if (GetParam() == GIT_CLI || GetParam() == LIBGIT)
		return;

	auto cleanFilterFilename = m_Git.m_CurrentDir / L"clean_filter_openssl";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(cleanFilterFilename.c_str(), L"#!/bin/bash\nopenssl version | grep -q \" 1\\\\.0\\\\.\"\nif [[ $? = 0 ]]; then\n\topenssl enc -base64 -aes-256-ecb -S FEEDDEADBEEF -k PASS_FIXED\n\techo 1.0>openssl10\nelse\n\topenssl enc -base64 -pbkdf2 -aes-256-ecb -nosalt -k PASS_FIXED\nfi\n"));
	auto smudgeFilterFilename = m_Git.m_CurrentDir / L"smudge_filter_openssl";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(smudgeFilterFilename.c_str(), L"#!/bin/bash\nopenssl version | grep -q \" 1\\\\.0\\\\.\"\nif [[ $? = 0 ]]; then\n\topenssl enc -d -base64 -aes-256-ecb -k PASS_FIXED\nelse\n\topenssl enc -d -base64 -pbkdf2 -aes-256-ecb -nosalt -k PASS_FIXED\nfi\n"));

	const wil::unique_git_repository repo(m_Git.GetGitRepository());
	ASSERT_TRUE(repo.is_valid());
	wil::unique_git_config config;
	git_repository_config(config.put(), repo.get());
	ASSERT_TRUE(config.is_valid());
	CStringA path = CUnicodeUtils::GetUTF8<CStringA>(m_Git.m_CurrentDir.native());
	path.Replace('\\', '/');
	EXPECT_EQ(0, git_config_set_string(config.get(), "filter.openssl.clean", path + "/clean_filter_openssl"));
	EXPECT_EQ(0, git_config_set_string(config.get(), "filter.openssl.smudge", path + "/smudge_filter_openssl"));
	EXPECT_EQ(0, git_config_set_bool(config.get(), "filter.openssl.required", 1));

	auto attributesFile = m_Git.m_CurrentDir / L".gitattributes";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(attributesFile.c_str(), L"*.enc filter=openssl\n"));

	auto encryptedFileOne = m_Git.m_CurrentDir / L"1.enc";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(encryptedFileOne.c_str(), L"This should be encrypted...\nAnd decrypted on the fly\n"));

	auto encryptedFileTwo = m_Git.m_CurrentDir / L"2.enc";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(encryptedFileTwo.c_str(), L"This should also be encrypted...\nAnd also decrypted on the fly\n"));

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"1.enc"}, &output));
	if (!GitCall::ms_bCygwinGit) // on AppVeyor with the VS2017 image we get a warning: "WARNING: can't open config file: /usr/local/ssl/openssl.cnf"
		EXPECT_STREQ("", output);

	wil::unique_git_index index;
	ASSERT_EQ(0, git_repository_index(index.put(), repo.get()));
	EXPECT_EQ(0, git_index_add_bypath(index.get(), "2.enc"));
	EXPECT_EQ(0, git_index_write(index.get()));

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"commit", L"-m", L"Message"}, &output));
	EXPECT_NE("", output);

	CString fileContents;
	auto tmpFile = GitCall::GetTempFile();
	ASSERT_STRNE(L"", tmpFile);
	EXPECT_EQ(0, m_Git.GetOneFile(L"HEAD", CTGitPath(L"1.enc"), tmpFile));
	EXPECT_EQ(true, StringUtils::ReadStringFromTextFile(tmpFile, fileContents));
	EXPECT_STREQ(L"This should be encrypted...\nAnd decrypted on the fly\n", fileContents);
	::DeleteFile(tmpFile);

	fileContents.Empty();
	tmpFile = GitCall::GetTempFile();
	ASSERT_STRNE(L"", tmpFile);
	EXPECT_EQ(0, m_Git.GetOneFile(L"HEAD", CTGitPath(L"2.enc"), tmpFile));
	EXPECT_EQ(true, StringUtils::ReadStringFromTextFile(tmpFile, fileContents));
	EXPECT_STREQ(L"This should also be encrypted...\nAnd also decrypted on the fly\n", fileContents);
	::DeleteFile(tmpFile);

	EXPECT_TRUE(::DeleteFile(attributesFile.c_str()));

	bool openssl10 = fs::exists(L"openssl10");

	fileContents.Empty();
	tmpFile = GitCall::GetTempFile();
	ASSERT_STRNE(L"", tmpFile);
	EXPECT_EQ(0, m_Git.GetOneFile(L"HEAD", CTGitPath(L"1.enc"), tmpFile));
	EXPECT_EQ(true, StringUtils::ReadStringFromTextFile(tmpFile, fileContents));
	fileContents.Replace(L"\r\n", L"\n");
	if (openssl10)
		EXPECT_STREQ(L"U2FsdGVkX1/+7d6tvu8AABwbE+Xy7U4l5boTKjIgUkYHONqmYHD+0e6k35MgtUGx\ns11nq1QuKeFCW5wFWNSj1WcHg2n4W59xfnB7RkSSIDQ=\n", fileContents);
	else
		EXPECT_STREQ(L"JlEiSB71Z3+fM2/Y2lAvxck187c4O1aE7epFJHVoDYJmKNBVvHB2zIPEoTAwW2pJ\nYxWZFBoqGROkX+koseasng==\n", fileContents);
	::DeleteFile(tmpFile);

	fileContents.Empty();
	tmpFile = GitCall::GetTempFile();
	ASSERT_STRNE(L"", tmpFile);
	EXPECT_EQ(0, m_Git.GetOneFile(L"HEAD", CTGitPath(L"2.enc"), tmpFile));
	EXPECT_EQ(true, StringUtils::ReadStringFromTextFile(tmpFile, fileContents));
	fileContents.Replace(L"\r\n", L"\n");
	if (openssl10)
		EXPECT_STREQ(L"U2FsdGVkX1/+7d6tvu8AAIDDx8qi/l0qzkSMsS2YLt8tYK1oWzj8+o78fXH0/tlO\nCRVrKqTvh9eUFklY8QFYfZfj01zBkFat+4zrW+1rV4Q=\n", fileContents);
	else
		EXPECT_STREQ(L"iU6jZ5d/5XBR9BvT1s+oqF7vIxabgYETQftI27P6jWQky308ukprRuOVZNr3NSH6\nlgXwDTo+mez3JB31HVSMCA==\n", fileContents);
	::DeleteFile(tmpFile);
}

TEST_P(CBasicGitWithTestRepoBareFixture, GetOneFile)
{
	GetOneFile(m_Git);
}

static void GetBranchDescriptions(CGit& m_Git)
{
	std::map<std::wstring, std::wstring> descriptions;
	EXPECT_EQ(0, m_Git.GetBranchDescriptions(descriptions));
	EXPECT_EQ(0U, descriptions.size());

	g_Git.SetConfigValue(L"branch.master.description", L"test");
	g_Git.SetConfigValue(L"branch.subdir/branch.description", L"multi\nline");

	EXPECT_EQ(0, m_Git.GetBranchDescriptions(descriptions));
	ASSERT_EQ(2U, descriptions.size());
	EXPECT_STREQ(L"test", descriptions[L"master"].c_str());
	EXPECT_STREQ(L"multi\nline", descriptions[L"subdir/branch"].c_str());
}

TEST_P(CBasicGitWithEmptyRepositoryFixture, GetBranchDescriptions)
{
	GetBranchDescriptions(m_Git);
}

TEST_P(CBasicGitWithEmptyBareRepositoryFixture, GetBranchDescriptions)
{
	GetBranchDescriptions(m_Git);
}

TEST_P(CBasicGitWithTestRepoFixture, Config)
{
	EXPECT_STREQ(L"", m_Git.GetConfigValue(L"not-found"));
	EXPECT_STREQ(L"default", m_Git.GetConfigValue(L"not-found", L"default"));

	EXPECT_STREQ(L"false", m_Git.GetConfigValue(L"core.bare"));
	EXPECT_STREQ(L"false", m_Git.GetConfigValue(L"core.bare", L"default-value")); // value exist, so default does not match
	EXPECT_STREQ(L"true", m_Git.GetConfigValue(L"core.ignorecase"));
	EXPECT_STREQ(L"0", m_Git.GetConfigValue(L"core.repositoryformatversion"));
	EXPECT_STREQ(L"https://example.com/git/testing", m_Git.GetConfigValue(L"remote.origin.url"));

	EXPECT_EQ(false, m_Git.GetConfigValueBool(L"not-found"));
	EXPECT_EQ(true, m_Git.GetConfigValueBool(L"not-found", true));
	EXPECT_EQ(false, m_Git.GetConfigValueBool(L"core.bare"));
	EXPECT_EQ(false, m_Git.GetConfigValueBool(L"core.bare", true)); // value exist, so default does not match
	EXPECT_EQ(false, m_Git.GetConfigValueBool(L"core.repositoryformatversion"));
	EXPECT_EQ(false, m_Git.GetConfigValueBool(L"remote.origin.url"));
	EXPECT_EQ(true, m_Git.GetConfigValueBool(L"core.ignorecase"));

	CString values[] = { L"", L" ", L"\n", L"\n\n", L"ending-with-space ", L" starting with-space", L"test1", L"some\\backslashes\\in\\it", L"with \" doublequote", L"with backslash before \\\" doublequote", L"with'quote", L"multi\nline", L"no-multi\\nline", L"new line at end\n", L"with ümlaut", L"*", L"`pwd`" };
	for (auto i = 0ull; i < _countof(values); ++i)
	{
		CString key;
		key.Format(L"re-read.test%d", i);
		EXPECT_EQ(0, m_Git.SetConfigValue(key, values[i]));
		EXPECT_STREQ(values[i], m_Git.GetConfigValue(key));
	}

	m_Git.SetConfigValue(L"booltest.true1", L"1");
	m_Git.SetConfigValue(L"booltest.true2", L"100");
	m_Git.SetConfigValue(L"booltest.true3", L"-2");
	m_Git.SetConfigValue(L"booltest.true4", L"yes");
	m_Git.SetConfigValue(L"booltest.true5", L"yEs");
	m_Git.SetConfigValue(L"booltest.true6", L"true");
	m_Git.SetConfigValue(L"booltest.true7", L"on");
	for (int i = 1; i <= 7; ++i)
	{
		CString key;
		key.Format(L"booltest.true%d", i);
		EXPECT_EQ(true, m_Git.GetConfigValueBool(key));
	}
	m_Git.SetConfigValue(L"booltest.false1", L"0");
	EXPECT_EQ(false, m_Git.GetConfigValueBool(L"booltest.false1"));
	m_Git.SetConfigValue(L"booltest.false2", L"");
	EXPECT_EQ(false, m_Git.GetConfigValueBool(L"booltest.false2"));
	for (const auto* falsy : { L"no", L"nO", L"off", L"FALSE" })
	{
		m_Git.SetConfigValue(L"booltest.falsy", falsy);
		EXPECT_EQ(false, m_Git.GetConfigValueBool(L"booltest.falsy", true)) << falsy;
	}

	// A value git itself would reject is not "false" - it is unusable, so the
	// caller's default stands, and it does so identically on every backend. The
	// inline parser this used to use answered false here while the --bool path
	// answered the default, so the two disagreed.
	m_Git.SetConfigValue(L"booltest.garbage", L"banana");
	EXPECT_EQ(false, m_Git.GetConfigValueBool(L"booltest.garbage"));
	EXPECT_EQ(true, m_Git.GetConfigValueBool(L"booltest.garbage", true));

	EXPECT_EQ(0, m_Git.GetConfigValueInt32(L"does-not-exist"));
	EXPECT_EQ(15, m_Git.GetConfigValueInt32(L"does-not-exist", 15));
	EXPECT_EQ(0, m_Git.GetConfigValueInt32(L"core.repositoryformatversion"));
	EXPECT_EQ(0, m_Git.GetConfigValueInt32(L"core.repositoryformatversion", 42)); // value exist, so default should not be returned
	EXPECT_EQ(1, m_Git.GetConfigValueInt32(L"booltest.true1"));
	EXPECT_EQ(100, m_Git.GetConfigValueInt32(L"booltest.true2"));
	EXPECT_EQ(-2, m_Git.GetConfigValueInt32(L"booltest.true3"));
	EXPECT_EQ(0, m_Git.GetConfigValueInt32(L"booltest.true4"));
	EXPECT_EQ(42, m_Git.GetConfigValueInt32(L"booltest.true4", 42));
	EXPECT_EQ(0, m_Git.GetConfigValueInt32(L"booltest.true8"));
	EXPECT_EQ(42, m_Git.GetConfigValueInt32(L"booltest.true8", 42));

	// git's size suffixes, which is the sort of thing a call site that parses the
	// string itself with _wtoi() silently gets wrong.
	m_Git.SetConfigValue(L"inttest.suffix", L"2k");
	EXPECT_EQ(2048, m_Git.GetConfigValueInt32(L"inttest.suffix"));

	EXPECT_NE(0, m_Git.UnsetConfigValue(L"does-not-exist"));
	EXPECT_STREQ(L"false", m_Git.GetConfigValue(L"core.bare"));
	EXPECT_STREQ(L"true", m_Git.GetConfigValue(L"core.ignorecase"));
	EXPECT_EQ(0, m_Git.UnsetConfigValue(L"core.bare"));
	EXPECT_STREQ(L"default", m_Git.GetConfigValue(L"core.bare", L"default"));
	EXPECT_STREQ(L"true", m_Git.GetConfigValue(L"core.ignorecase"));

	EXPECT_EQ(0, m_Git.SetConfigValue(L"--some.bla", L"my value"));
	EXPECT_STREQ(L"my value", m_Git.GetConfigValue(L"--some.bla"));
	EXPECT_EQ(0, m_Git.UnsetConfigValue(L"--some.bla"));
	EXPECT_STREQ(L"", m_Git.GetConfigValue(L"--some.bla"));

	auto gitConfig = m_Git.m_CurrentDir / L".git\\config";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(gitConfig.c_str(), L"[booltest]\nistrue"));
	EXPECT_EQ(true, m_Git.GetConfigValueBool(L"booltest.istrue"));

	// test includes from %HOME% specified as ~/
	EXPECT_STREQ(L"not-found", g_Git.GetConfigValue(L"test.fromincluded", L"not-found"));
	EXPECT_EQ(0, m_Git.SetConfigValue(L"include.path", L"~/a-path-that-should-not-exist.gconfig"));
	EXPECT_STREQ(L"~/a-path-that-should-not-exist.gconfig", g_Git.GetConfigValue(L"include.path", L"not-found"));
	CString testFile = g_Git.GetHomeDirectory() + L"\\a-path-that-should-not-exist.gconfig";
	ASSERT_FALSE(fs::exists(testFile.GetString())); // make sure we don't override a file by mistake ;)
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile, L"[test]\nfromincluded=yeah-this-is-included\n"));
	EXPECT_STREQ(L"yeah-this-is-included", g_Git.GetConfigValue(L"test.fromincluded", L"not-found"));
	EXPECT_TRUE(::DeleteFile(testFile));
}

TEST_P(CBasicGitWithTestRepoFixture, GetWorkingTreeChanges)
{
	if (GetParam() != 0)
		return;

	// adding ansi2.txt (as a copy of ansi.txt) produces a warning
	m_Git.SetConfigValue(L"core.autocrlf", L"false");

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);

	CTGitPathList filter(CTGitPath(L"copy"));

	// no changes
	CTGitPathList list;
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	EXPECT_TRUE(list.IsEmpty());
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	EXPECT_TRUE(list.IsEmpty());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	EXPECT_TRUE(list.IsEmpty());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);

	// untracked file
	auto testFile = m_Git.m_CurrentDir / L"untracked-file.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	EXPECT_TRUE(list.IsEmpty());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	EXPECT_TRUE(list.IsEmpty());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	EXPECT_TRUE(list.IsEmpty());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);

	// untracked file in sub-directory
	testFile = m_Git.m_CurrentDir / L"copy\\untracked-file.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	EXPECT_TRUE(list.IsEmpty());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	EXPECT_TRUE(list.IsEmpty());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	EXPECT_TRUE(list.IsEmpty());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	EXPECT_TRUE(list.IsEmpty());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	EXPECT_TRUE(list.IsEmpty());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);

	// modified file in sub-directory
	testFile = m_Git.m_CurrentDir / L"copy\\utf8-nobom.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[1].m_stagingStatus);

	// two modified files, one in root and one in sub-directory
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	testFile = m_Git.m_CurrentDir / L"utf8-bom.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	testFile = m_Git.m_CurrentDir / L"copy\\utf8-nobom.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"utf8-bom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"utf8-bom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[1].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(3, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_STREQ(L"utf8-bom.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[2].m_Action);
	EXPECT_FALSE(list[2].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[1].m_stagingStatus);

	// Staged modified file
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	testFile = m_Git.m_CurrentDir / L"utf8-nobom.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"utf8-nobom.txt"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// Staged modified file in subfolder
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	testFile = m_Git.m_CurrentDir / L"copy\\utf8-nobom.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"copy/utf8-nobom.txt"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// Modified file modified after staging
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	testFile = m_Git.m_CurrentDir / L"copy\\utf8-nobom.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"copy/utf8-nobom.txt"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"now with different content after staging"));
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/utf8-nobom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[1].m_stagingStatus);

	// Missing file
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	EXPECT_TRUE(::DeleteFile((m_Dir.GetTempDir() / L"copy\\ansi.txt").c_str()));
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list[1].m_Action);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list[0].m_Action);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list[0].m_Action);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list[0].m_Action);
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list[1].m_Action);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MISSING, list[1].m_Action);
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[1].m_stagingStatus);

	// deleted file, also deleted in index
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"rm", L"copy/ansi.txt"}, &output));
	EXPECT_NE("", output);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[1].m_Action);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[1].m_Action);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[1].m_Action);
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// file deleted in index, but still on disk
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"rm", L"--cached", L"copy/ansi.txt"}, &output));
	EXPECT_NE("", output);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[1].m_Action);
	EXPECT_STREQ(L"0", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// file deleted in index, but still on disk, but modified
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"rm", L"--cached", L"copy/ansi.txt"}, &output));
	EXPECT_NE("", output);
	testFile = m_Git.m_CurrentDir / L"copy\\ansi.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[1].m_Action);
	EXPECT_STREQ(L"0", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_DELETED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// renamed file in same folder (root)
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"mv", L"ansi.txt", L"änsi2.txt"}, &output));
	EXPECT_STREQ("", output);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"änsi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"änsi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"änsi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"änsi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"änsi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"änsi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"änsi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// renamed file in same folder (subfolder)
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"mv", L"copy/ansi.txt", L"copy/ansi2.txt"}, &output));
	EXPECT_STREQ("", output);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/ansi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/ansi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/ansi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"copy/ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// renamed file in same folder (root) and modified file (without staging)
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"mv", L"ansi.txt", L"änsi2.txt"}, &output));
	EXPECT_STREQ("", output);
	testFile = m_Git.m_CurrentDir / L"änsi2.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"lkjglkäöfdg\nfgdjsäkglsfdg\nöäöü45g\nfdgi & §$ % &hfdsgä\nä#äf34öööäß€9875oe\nöäSOMETHINGELSEmfdg\n€fgfdsg\n45\näü\n"));
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"änsi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	//EXPECT_STREQ(L"9", list[0].m_StatAdd.c_str());
	//EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"änsi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	//EXPECT_STREQ(L"9", list[0].m_StatAdd.c_str());
	//EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[0].m_stagingStatus);
	//EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"änsi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	//EXPECT_STREQ(L"9", list[1].m_StatAdd.c_str());
	//EXPECT_STREQ(L"0", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"änsi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"änsi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"änsi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"änsi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[1].m_stagingStatus);

	// renamed file in same folder (root) and modified file (staged all changes)
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"mv", L"ansi.txt", L"änsi2.txt"}, &output));
	EXPECT_STREQ("", output);
	testFile = m_Git.m_CurrentDir / L"änsi2.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"lkjglkäöfdg\nfgdjsäkglsfdg\nöäöü45g\nfdgi & §$ % &hfdsgä\nä#äf34öööäß€9875oe\nöäSOMETHINGELSEmfdg\n€fgfdsg\n45\näü\n"));
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"änsi2.txt"}, &output));
	EXPECT_STREQ("", output);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"änsi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"änsi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_STREQ(L"9", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(3, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"9", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"ascii.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_STREQ(L"2", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_STREQ(L"änsi2.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[2].m_Action);
	EXPECT_STREQ(L"9", list[2].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[2].m_StatDel.c_str());
	EXPECT_FALSE(list[2].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"änsi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"änsi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(3, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"ascii.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_STREQ(L"änsi2.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[2].m_Action);
	EXPECT_FALSE(list[2].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(3, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED | CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_DELETED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"ascii.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);
	EXPECT_STREQ(L"änsi2.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[2].m_Action);
	EXPECT_FALSE(list[2].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[2].m_stagingStatus);

	// added and staged new file
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	testFile = m_Git.m_CurrentDir / L"copy\\test-file.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"copy/test-file.txt"}, &output));
	EXPECT_STREQ("", output);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/test-file.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/test-file.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/test-file.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/test-file.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/test-file.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/test-file.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/test-file.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/test-file.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// added and staged new file, then modified without staging
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	testFile = m_Git.m_CurrentDir / L"copy\\test-file.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"*.enc filter=openssl\n"));
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"copy/test-file.txt"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"something else\n"));
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/test-file.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/test-file.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"2", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"2", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/test-file.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/test-file.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/test-file.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/test-file.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/test-file.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/test-file.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[1].m_stagingStatus);

	// file copied and staged
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	testFile = m_Git.m_CurrentDir / L"ansi.txt";
	EXPECT_TRUE(CopyFile((m_Git.m_CurrentDir / L"ansi.txt").c_str(), (m_Git.m_CurrentDir / L"ansi2.txt").c_str(), TRUE));
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"ansi2.txt"}, &output));
	EXPECT_STREQ("", output);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_STREQ(L"9", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"ascii.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"ascii.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"ascii.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// file renamed + moved to sub-folder
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"mv", L"ansi.txt", L"copy/ansi2.txt"}, &output));
	EXPECT_STREQ("", output);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/ansi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false)); // TODO: due to filter restrictions we cannot detect the rename here
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action); // TODO
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true)); // TODO: due to filter restrictions we cannot detect the rename here
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action); // TODO
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false)); // TODO: due to filter restrictions we cannot detect the rename here
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action); // TODO
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true)); // TODO: due to filter restrictions we cannot detect the rename here
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"copy/ansi2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action); // TODO
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"copy/ansi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED | CTGitPath::LOGACTIONS_REPLACED, list.GetAction());
	EXPECT_STREQ(L"ascii.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"copy/ansi2.txt", list[1].GetGitPathString().c_str());
	EXPECT_STREQ(L"ansi.txt", list[1].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_REPLACED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// conflicting files
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"merge", L"forconflict"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(1, m_Git.Run({L"git.exe", L"merge", L"simple-conflict"}, &output));
	EXPECT_NE("", output);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"", list[0].GetGitOldPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"6", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(7, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"utf16-be-bom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_STREQ(L"utf16-be-nobom.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[2].m_Action);
	EXPECT_FALSE(list[2].IsDirectory());
	EXPECT_STREQ(L"utf16-le-bom.txt", list[3].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[3].m_Action);
	EXPECT_FALSE(list[3].IsDirectory());
	EXPECT_STREQ(L"utf16-le-nobom.txt", list[4].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[4].m_Action);
	EXPECT_FALSE(list[4].IsDirectory());
	EXPECT_STREQ(L"utf8-bom.txt", list[5].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[5].m_Action);
	EXPECT_FALSE(list[5].IsDirectory());
	EXPECT_STREQ(L"utf8-nobom.txt", list[6].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[6].m_Action);
	EXPECT_FALSE(list[6].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true));
	ASSERT_EQ(7, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"utf16-be-bom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_STREQ(L"utf16-be-nobom.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[2].m_Action);
	EXPECT_FALSE(list[2].IsDirectory());
	EXPECT_STREQ(L"utf16-le-bom.txt", list[3].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[3].m_Action);
	EXPECT_FALSE(list[3].IsDirectory());
	EXPECT_STREQ(L"utf16-le-nobom.txt", list[4].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[4].m_Action);
	EXPECT_FALSE(list[4].IsDirectory());
	EXPECT_STREQ(L"utf8-bom.txt", list[5].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[5].m_Action);
	EXPECT_FALSE(list[5].IsDirectory());
	EXPECT_STREQ(L"utf8-nobom.txt", list[6].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[6].m_Action);
	EXPECT_FALSE(list[6].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, &filter, true, true));
	ASSERT_EQ(7, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"utf16-be-bom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);
	EXPECT_STREQ(L"utf16-be-nobom.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[2].m_Action);
	EXPECT_FALSE(list[2].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[2].m_stagingStatus);
	EXPECT_STREQ(L"utf16-le-bom.txt", list[3].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[3].m_Action);
	EXPECT_FALSE(list[3].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[3].m_stagingStatus);
	EXPECT_STREQ(L"utf16-le-nobom.txt", list[4].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[4].m_Action);
	EXPECT_FALSE(list[4].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[4].m_stagingStatus);
	EXPECT_STREQ(L"utf8-bom.txt", list[5].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[5].m_Action);
	EXPECT_FALSE(list[5].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[5].m_stagingStatus);
	EXPECT_STREQ(L"utf8-nobom.txt", list[6].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[6].m_Action);
	EXPECT_FALSE(list[6].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[6].m_stagingStatus);
}

TEST_P(CBasicGitWithTestRepoFixture, GetWorkingTreeChanges_DeleteModifyConflict_DeletedRemotely)
{
	if (GetParam() != 0)
		return;

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);

	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"forconflict"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"rm", L"ansi.txt"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"commit", L"-m", L"Prepare conflict case"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"simple-conflict"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(1, m_Git.Run({L"git.exe", L"merge", L"forconflict"}, &output));
	EXPECT_NE("", output);

	CTGitPathList filter(CTGitPath(L"copy"));

	CTGitPathList list;
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(7, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"utf16-be-bom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_STREQ(L"-", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"-", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_STREQ(L"utf16-be-nobom.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[2].m_Action);
	EXPECT_STREQ(L"-", list[2].m_StatAdd.c_str());
	EXPECT_STREQ(L"-", list[2].m_StatDel.c_str());
	EXPECT_FALSE(list[2].IsDirectory());
	EXPECT_STREQ(L"utf16-le-bom.txt", list[3].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[3].m_Action);
	EXPECT_STREQ(L"-", list[3].m_StatAdd.c_str());
	EXPECT_STREQ(L"-", list[3].m_StatDel.c_str());
	EXPECT_FALSE(list[3].IsDirectory());
	EXPECT_STREQ(L"utf16-le-nobom.txt", list[4].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[4].m_Action);
	EXPECT_STREQ(L"-", list[4].m_StatAdd.c_str());
	EXPECT_STREQ(L"-", list[4].m_StatDel.c_str());
	EXPECT_FALSE(list[4].IsDirectory());
	EXPECT_STREQ(L"utf8-bom.txt", list[5].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[5].m_Action);
	EXPECT_STREQ(L"6", list[5].m_StatAdd.c_str());
	EXPECT_STREQ(L"6", list[5].m_StatDel.c_str());
	EXPECT_FALSE(list[5].IsDirectory());
	EXPECT_STREQ(L"utf8-nobom.txt", list[6].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[6].m_Action);
	EXPECT_STREQ(L"3", list[6].m_StatAdd.c_str());
	EXPECT_STREQ(L"3", list[6].m_StatDel.c_str());
	EXPECT_FALSE(list[6].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(7, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"1", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"utf16-be-bom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_STREQ(L"-", list[1].m_StatAdd.c_str());
	EXPECT_STREQ(L"-", list[1].m_StatDel.c_str());
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_STREQ(L"utf16-be-nobom.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[2].m_Action);
	EXPECT_STREQ(L"-", list[2].m_StatAdd.c_str());
	EXPECT_STREQ(L"-", list[2].m_StatDel.c_str());
	EXPECT_FALSE(list[2].IsDirectory());
	EXPECT_STREQ(L"utf16-le-bom.txt", list[3].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[3].m_Action);
	EXPECT_STREQ(L"-", list[3].m_StatAdd.c_str());
	EXPECT_STREQ(L"-", list[3].m_StatDel.c_str());
	EXPECT_FALSE(list[3].IsDirectory());
	EXPECT_STREQ(L"utf16-le-nobom.txt", list[4].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[4].m_Action);
	EXPECT_FALSE(list[4].IsDirectory());
	EXPECT_STREQ(L"utf8-bom.txt", list[5].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[5].m_Action);
	EXPECT_FALSE(list[5].IsDirectory());
	EXPECT_STREQ(L"utf8-nobom.txt", list[6].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[6].m_Action);
	EXPECT_FALSE(list[6].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true));
	ASSERT_EQ(7, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"utf16-be-bom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_STREQ(L"utf16-be-nobom.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[2].m_Action);
	EXPECT_FALSE(list[2].IsDirectory());
	EXPECT_STREQ(L"utf16-le-bom.txt", list[3].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[3].m_Action);
	EXPECT_FALSE(list[3].IsDirectory());
	EXPECT_STREQ(L"utf16-le-nobom.txt", list[4].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[4].m_Action);
	EXPECT_FALSE(list[4].IsDirectory());
	EXPECT_STREQ(L"utf8-bom.txt", list[5].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[5].m_Action);
	EXPECT_FALSE(list[5].IsDirectory());
	EXPECT_STREQ(L"utf8-nobom.txt", list[6].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[6].m_Action);
	EXPECT_FALSE(list[6].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(7, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"utf16-be-bom.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);
	EXPECT_STREQ(L"utf16-be-nobom.txt", list[2].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[2].m_Action);
	EXPECT_FALSE(list[2].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[2].m_stagingStatus);
	EXPECT_STREQ(L"utf16-le-bom.txt", list[3].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[3].m_Action);
	EXPECT_FALSE(list[3].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[3].m_stagingStatus);
	EXPECT_STREQ(L"utf16-le-nobom.txt", list[4].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[4].m_Action);
	EXPECT_FALSE(list[4].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[4].m_stagingStatus);
	EXPECT_STREQ(L"utf8-bom.txt", list[5].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[5].m_Action);
	EXPECT_FALSE(list[5].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[5].m_stagingStatus);
	EXPECT_STREQ(L"utf8-nobom.txt", list[6].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[6].m_Action);
	EXPECT_FALSE(list[6].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[6].m_stagingStatus);
}

TEST_P(CBasicGitWithTestRepoFixture, GetWorkingTreeChanges_DeleteModifyConflict_DeletedLocally)
{
	if (GetParam() != 0)
		return;

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);

	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"forconflict"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"rm", L"ansi.txt"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"commit", L"-m", L"Prepare conflict case"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(1, m_Git.Run({L"git.exe", L"merge", L"simple-conflict"}, &output));
	EXPECT_NE("", output);

	CTGitPathList filter(CTGitPath(L"copy"));

	CTGitPathList list;
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_ADDED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	//EXPECT_STREQ(L"9", list[0].m_StatAdd.c_str());
	//EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, true, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"4", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"4", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(0, list.GetCount());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list.GetAction());
	EXPECT_STREQ(L"ansi.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list[0].m_Action);
	EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
}

TEST_P(CBasicGitWithEmptyRepositoryFixture, GetWorkingTreeChanges)
{
	if (GetParam() != 0)
		return;

	CTGitPathList list;
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	EXPECT_TRUE(list.IsEmpty());

	auto testFile = m_Dir.GetTempDir() / L"test.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is testing file."));
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	EXPECT_TRUE(list.IsEmpty());
	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"test.txt"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"test.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_STREQ(L"", list[0].m_StatAdd.c_str()); // TODO: right now no numstat is parsed
	EXPECT_STREQ(L"", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"test.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, false, true));
	ASSERT_EQ(1, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"test.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);

	CTGitPathList filter(CTGitPath(L"copy"));
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(1, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"test.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(1, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"test.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(1, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"test.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);

	list.Clear();
	EXPECT_TRUE(::CreateDirectory((m_Dir.GetTempDir() / L"copy").c_str(), nullptr));
	testFile = m_Dir.GetTempDir() / L"copy\\test2.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is another testing file."));
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"copy/test2.txt"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(2, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"copy/test2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"test.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"copy/test2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"test.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(2, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"copy/test2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"test.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);

	// file modified after staging
	output.clear();
	list.Clear();
	testFile = m_Dir.GetTempDir() / L"copy\\test2.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"different content now"));
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter));
	ASSERT_EQ(2, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"copy/test2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_STREQ(L"test.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, true, true));
	ASSERT_EQ(2, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"copy/test2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"test.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, &filter, false, true));
	ASSERT_EQ(2, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"copy/test2.txt", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_FALSE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[0].m_stagingStatus);
	EXPECT_STREQ(L"test.txt", list[1].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
	EXPECT_FALSE(list[1].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyStaged, list[1].m_stagingStatus);
}

static int DoSubmodule(const wil::ArgvData& cmd, CGit& git, const fs::path& submoduleDir, std::string& output)
{
	output.clear();
	fs::path old = git.m_CurrentDir;
	SCOPE_EXIT { git.SetCurrentDirExact(old); };
	git.SetCurrentDirExact(submoduleDir);
	return git.Run(cmd, &output);
}

TEST_P(CBasicGitWithSubmoduleRepositoryFixture, GetWorkingTreeChanges_Submodules)
{
	if (GetParam() != 0)
		return;

	CTGitPathList list;

	fs::path resourcesDir;
	ASSERT_TRUE(GetResourcesDir(resourcesDir));
	auto submoduleDir = m_Dir.GetTempDir() / L"submodule";
	ASSERT_TRUE(CreateDirectory(submoduleDir.c_str(), nullptr));
	ASSERT_TRUE(CreateDirectory((submoduleDir / L".git").c_str(), nullptr));
	auto repoDir = resourcesDir / L"git-repo1";
	CopyRecursively(repoDir.c_str(), (submoduleDir / L".git").c_str());

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);

	// test for clean repo which contains an unrelated git submodule
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	EXPECT_TRUE(list.IsEmpty());

	// test for added, uncommitted submodule
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"submodule"}, &output));
	//EXPECT_STREQ("", output); // Git 2.14 starts to print a warning here
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"submodule", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_TRUE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	// EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list.GetAction()); // we do not care here for the list action, as its only used in GitLogListBase and there we re-calculate it in AsyncDiffThread
	EXPECT_STREQ(L"submodule", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
	EXPECT_TRUE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::PartiallyStaged, list[0].m_stagingStatus);

	// cleanup
	CAutoTempDir::DeleteDirectoryRecursive(submoduleDir);
	submoduleDir = m_Dir.GetTempDir() / L"something";

	// test for unchanged submodule
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"branch1"}, &output));
	EXPECT_NE("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(0, list.GetCount());

	// test for unchanged submodule with checkout out files
	output.clear();
	ASSERT_TRUE(CreateDirectory((submoduleDir / L".git").c_str(), nullptr));
	CopyRecursively(repoDir.c_str(), (submoduleDir / L".git").c_str());
	EXPECT_EQ(0, DoSubmodule({L"git.exe", L"reset", L"--hard", L"49ecdfff36bfe2b9b499b33e5034f427e2fa54dd"}, m_Git, submoduleDir, output));
	EXPECT_NE("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(0, list.GetCount());

	// test for changed submodule
	EXPECT_EQ(0, DoSubmodule({L"git.exe", L"reset", L"--hard", L"HEAD~1"}, m_Git, submoduleDir, output));
	EXPECT_NE("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"1", list[0].m_StatDel.c_str());
	EXPECT_TRUE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"1", list[0].m_StatDel.c_str());
	EXPECT_TRUE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);

	// test for modify-delete conflict (deleted remote)
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--force", L"branch1"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(1, m_Git.Run({L"git.exe", L"merge", L"deleted"}, &output));
	EXPECT_NE("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"1", list[0].m_StatDel.c_str());
	EXPECT_TRUE(list[0].IsDirectory());
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"1", list[0].m_StatDel.c_str());
	EXPECT_TRUE(list[0].IsDirectory());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);

	// test for modify-delete conflict (deleted locally)
	output.clear();
	CAutoTempDir::DeleteDirectoryRecursive(submoduleDir);
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--force", L"deleted"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(1, m_Git.Run({L"git.exe", L"merge", L"branch1"}, &output));
	EXPECT_NE("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	if (TGitH::ms_LastMsysGitVersion < ConvertVersionToInt(2, 34, 0)) {
		EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MISSING, list[0].m_Action);
		EXPECT_FALSE(list[0].IsDirectory()); // neither file nor directory is in filesystem
	}
	else
	{
		EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
		EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
		EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
		EXPECT_TRUE(list[0].IsDirectory()); // now a directory is in filesystem
	}
	list.Clear();
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr, true, true));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::StagingStatus::TotallyUnstaged, list[0].m_stagingStatus);
	if (TGitH::ms_LastMsysGitVersion < ConvertVersionToInt(2, 34, 0)) {
		EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MISSING, list[0].m_Action);
		EXPECT_FALSE(list[0].IsDirectory()); // neither file nor directory is in filesystem
	}
	else
	{
		EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_ADDED, list[0].m_Action);
		EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
		EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
		EXPECT_TRUE(list[0].IsDirectory()); // now a directory is in filesystem
	}

	// test for merge conflict submodule/file (local submodule, remote file)
	output.clear();
	CAutoTempDir::DeleteDirectoryRecursive(submoduleDir);
	DeleteFile(submoduleDir.c_str());
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--force", L"branch1"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(1, m_Git.Run({L"git.exe", L"merge", L"file"}, &output));
	EXPECT_NE("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	if (TGitH::ms_LastMsysGitVersion < ConvertVersionToInt(2, 34, 0))
		ASSERT_EQ(1, list.GetCount());
	else
		ASSERT_EQ(2, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	if (TGitH::ms_LastMsysGitVersion < ConvertVersionToInt(2, 34, 0) && (GitCall::ms_bCygwinGit || GitCall::ms_bMsys2Git))
	{
		EXPECT_NE(std::string::npos, output.find("error: failed to create path"));
		EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list[0].m_Action);
		EXPECT_TRUE(list[0].IsDirectory()); // folder is in filesystem
	}
	else if (TGitH::ms_LastMsysGitVersion < ConvertVersionToInt(2, 34, 0))
	{
		EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
		EXPECT_FALSE(list[0].IsDirectory()); // file is in filesystem
	}
	else
	{
		EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
		EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED, list[0].m_Action);
		EXPECT_STREQ(L"0", list[0].m_StatAdd.c_str());
		EXPECT_STREQ(L"0", list[0].m_StatDel.c_str());
		EXPECT_TRUE(list[0].IsDirectory()); // directory is in filesystem
		EXPECT_STREQ(L"something~file", list[1].GetGitPathString().c_str());
		EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
		EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
		EXPECT_STREQ(L"0", list[1].m_StatDel.c_str());
		EXPECT_FALSE(list[1].IsDirectory()); // alternative file is in filesystem
	}

	// test for merge conflict submodule/file (remote submodule, local file)
	output.clear();
	CAutoTempDir::DeleteDirectoryRecursive(submoduleDir);
	DeleteFile(submoduleDir.c_str());
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--force", L"file"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(1, m_Git.Run({L"git.exe", L"merge", L"branch1"}, &output));
	EXPECT_NE("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	if (TGitH::ms_LastMsysGitVersion < ConvertVersionToInt(2, 34, 0))
		ASSERT_EQ(1, list.GetCount());
	else
		ASSERT_EQ(2, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"1", list[0].m_StatDel.c_str());
	if (TGitH::ms_LastMsysGitVersion < ConvertVersionToInt(2, 34, 0))
		EXPECT_FALSE(list[0].IsDirectory()); // file is in filesystem
	else
	{
		EXPECT_TRUE(list[0].IsDirectory()); // directory is in filesystem
		EXPECT_STREQ(L"something~HEAD", list[1].GetGitPathString().c_str());
		EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_ADDED, list[1].m_Action);
		EXPECT_STREQ(L"1", list[1].m_StatAdd.c_str());
		EXPECT_STREQ(L"0", list[1].m_StatDel.c_str());
		EXPECT_FALSE(list[1].IsDirectory()); // alternative file is in filesystem
	}

	// test for simple merge conflict
	DeleteFile(submoduleDir.c_str());
	CAutoTempDir::DeleteDirectoryRecursive(submoduleDir); // delete .git folder so that we get a simple merge conflict!
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--force", L"branch1"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(1, m_Git.Run({L"git.exe", L"merge", L"branch2"}, &output));
	EXPECT_NE("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_UNMERGED | CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"1", list[0].m_StatDel.c_str());
	EXPECT_TRUE(list[0].IsDirectory());

	// test for submodule to file
	DeleteFile(submoduleDir.c_str());
	CAutoTempDir::DeleteDirectoryRecursive(submoduleDir);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--force", L"branch1"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"rm", L"-rf", L"something"}, &output));
	EXPECT_NE("", output);
	ASSERT_TRUE(StringUtils::WriteStringToTextFile(submoduleDir.c_str(), L"something"));
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"something"}, &output));
	EXPECT_STREQ("", output);
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"1", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());

	// test for file to submodule
	DeleteFile(submoduleDir.c_str());
	CAutoTempDir::DeleteDirectoryRecursive(submoduleDir);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"checkout", L"--force", L"file"}, &output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"rm", L"something"}, &output));
	EXPECT_NE("", output);
	ASSERT_TRUE(CreateDirectory(submoduleDir.c_str(), nullptr));
	ASSERT_TRUE(CreateDirectory((submoduleDir / L".git").c_str(), nullptr));
	CopyRecursively(repoDir.c_str(), (submoduleDir / L".git").c_str());
	EXPECT_EQ(0, DoSubmodule({L"git.exe", L"reset", L"--hard", L"49ecdfff36bfe2b9b499b33e5034f427e2fa54dd"}, m_Git, submoduleDir, output));
	EXPECT_NE("", output);
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"something"}, &output));
	//EXPECT_STREQ("", output); // Git 2.14 starts to print a warning here
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	ASSERT_EQ(1, list.GetCount());
	EXPECT_STREQ(L"something", list[0].GetGitPathString().c_str());
	EXPECT_EQ(CTGitPath::LOGACTIONS_MODIFIED, list[0].m_Action);
	EXPECT_STREQ(L"1", list[0].m_StatAdd.c_str());
	EXPECT_STREQ(L"1", list[0].m_StatDel.c_str());
	EXPECT_FALSE(list[0].IsDirectory());
}

TEST_P(CBasicGitWithTestRepoFixture, GetWorkingTreeChanges_RefreshGitIndex)
{
	if (GetParam() != 0)
		return;

	// adding ansi2.txt (as a copy of ansi.txt) produces a warning
	m_Git.SetConfigValue(L"core.autocrlf", L"false");

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);

	CTGitPathList list;
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	EXPECT_TRUE(list.IsEmpty());

	// touch file
	HANDLE handle = CreateFile((m_Git.m_CurrentDir / L"ascii.txt").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(handle, INVALID_HANDLE_VALUE);
	FILETIME ft;
	EXPECT_EQ(TRUE, GetFileTime(handle, nullptr, nullptr, &ft));
	ft.dwLowDateTime -= 1000 * 60 * 1000;
	EXPECT_NE(0, SetFileTime(handle, nullptr, nullptr, &ft));
	CloseHandle(handle);

	// START: this is the undesired behavior
	// this test is just there so we notice when this change somehow
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	if (GitCall::ms_bCygwinGit || GitCall::ms_bMsys2Git)
		EXPECT_EQ(0, list.GetCount());
	else
		EXPECT_EQ(1, list.GetCount());
	list.Clear();
	// END: this is the undesired behavior

	m_Git.RefreshGitIndex(); // without this GetWorkingTreeChanges might report this file as modified (if no msys or cygwin hack is on)
	EXPECT_EQ(0, m_Git.GetWorkingTreeChanges(list, false, nullptr));
	EXPECT_EQ(0, list.GetCount());
}

TEST_P(CBasicGitWithTestRepoFixture, GetBisectTerms)
{
	if (GitCall::ms_bCygwinGit)
		return;

	CString good, bad;
	std::string output;

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"bisect", L"start"}, &output));
	m_Git.GetBisectTerms(&good, &bad);
	EXPECT_STREQ(L"good", good);
	EXPECT_STREQ(L"bad", bad);

	good.Empty();
	bad.Empty();
	m_Git.GetBisectTerms(&good, &bad);
	EXPECT_STREQ(L"good", good);
	EXPECT_STREQ(L"bad", bad);

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"bisect", L"reset"}, &output));

	if (m_Git.GetGitVersion(nullptr, nullptr) < 0x02070000)
		return;

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"bisect", L"start", L"--term-good=original", L"--term-bad=changed"}, &output));
	m_Git.GetBisectTerms(&good, &bad);
	EXPECT_STREQ(L"original", good);
	EXPECT_STREQ(L"changed", bad);

	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"bisect", L"reset"}, &output));
}

TEST_P(CBasicGitWithTestRepoFixture, GetRefsCommitIsOn)
{
	std::vector<std::wstring> list;
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"7c3cbfe13a929d2291a574dca45e4fd2d2ac1aa6"), false, false));
	EXPECT_TRUE(list.empty());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"7c3cbfe13a929d2291a574dca45e4fd2d2ac1aa6"), true, true));
	ASSERT_EQ(1U, list.size());
	EXPECT_STREQ(L"refs/heads/master", list[0].c_str());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"1fc3c9688e27596d8717b54f2939dc951568f6cb"), true, true));
	ASSERT_EQ(1U, list.size());
	EXPECT_STREQ(L"refs/heads/master", list[0].c_str());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"1fc3c9688e27596d8717b54f2939dc951568f6cb"), false, true));
	ASSERT_EQ(1U, list.size());
	EXPECT_STREQ(L"refs/heads/master", list[0].c_str());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"1fc3c9688e27596d8717b54f2939dc951568f6cb"), true, true, TGitH::BRANCH_REMOTE));
	EXPECT_TRUE(list.empty());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"1fc3c9688e27596d8717b54f2939dc951568f6cb"), false, true, TGitH::BRANCH_ALL));
	ASSERT_EQ(1U, list.size());
	EXPECT_STREQ(L"refs/heads/master", list[0].c_str());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"1fc3c9688e27596d8717b54f2939dc951568f6cb"), true, false));
	EXPECT_TRUE(list.empty());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb"), true, false));
	ASSERT_EQ(2U, list.size());
	EXPECT_STREQ(L"refs/tags/also-signed", list[0].c_str());
	EXPECT_STREQ(L"refs/tags/normal-tag", list[1].c_str());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"35c91b4ae2f77f4f21a7aba56d3c473c705d89e6"), true, true));
	ASSERT_EQ(3U, list.size());
	EXPECT_STREQ(L"refs/heads/master", list[0].c_str());
	EXPECT_STREQ(L"refs/heads/master2", list[1].c_str());
	EXPECT_STREQ(L"refs/tags/also-signed", list[2].c_str());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"313a41bc88a527289c87d7531802ab484715974f"), false, true));
	ASSERT_EQ(6U, list.size());
	EXPECT_STREQ(L"refs/heads/forconflict", list[0].c_str());
	EXPECT_STREQ(L"refs/heads/master", list[1].c_str());
	EXPECT_STREQ(L"refs/heads/master2", list[2].c_str());
	EXPECT_STREQ(L"refs/heads/signed-commit", list[3].c_str());
	EXPECT_STREQ(L"refs/heads/simple-conflict", list[4].c_str());
	EXPECT_STREQ(L"refs/heads/subdir/branch", list[5].c_str());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"313a41bc88a527289c87d7531802ab484715974f"), false, true, TGitH::BRANCH_REMOTE));
	ASSERT_EQ(1U, list.size());
	EXPECT_STREQ(L"refs/remotes/origin/master", list[0].c_str());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"313a41bc88a527289c87d7531802ab484715974f"), false, true, TGitH::BRANCH_LOCAL));
	ASSERT_EQ(6U, list.size());
	EXPECT_STREQ(L"refs/heads/forconflict", list[0].c_str());
	EXPECT_STREQ(L"refs/heads/master", list[1].c_str());
	EXPECT_STREQ(L"refs/heads/master2", list[2].c_str());
	EXPECT_STREQ(L"refs/heads/signed-commit", list[3].c_str());
	EXPECT_STREQ(L"refs/heads/simple-conflict", list[4].c_str());
	EXPECT_STREQ(L"refs/heads/subdir/branch", list[5].c_str());

	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"313a41bc88a527289c87d7531802ab484715974f"), false, true, TGitH::BRANCH_ALL));
	ASSERT_EQ(7U, list.size());
	EXPECT_STREQ(L"refs/heads/forconflict", list[0].c_str());
	EXPECT_STREQ(L"refs/remotes/origin/master", list[6].c_str());

	// test for symbolic refs
	const auto admin = GitAdminDir::GetAdminDirPath(g_Git.m_CurrentDir.native());
	ASSERT_TRUE(admin);
	CString adminDir = admin->path.c_str();
	adminDir += L"refs\\remotes\\origin\\HEAD";
	ASSERT_TRUE(StringUtils::WriteStringToTextFile(adminDir, L"ref: refs/remotes/origin/master"));
	list.clear();
	EXPECT_EQ(0, m_Git.GetRefsCommitIsOn(list, CGitHash::FromHexStr(L"313a41bc88a527289c87d7531802ab484715974f"), false, true, TGitH::BRANCH_ALL));
	ASSERT_EQ(8U, list.size());
	EXPECT_STREQ(L"refs/heads/forconflict", list[0].c_str());
	EXPECT_STREQ(L"refs/remotes/origin/HEAD", list[6].c_str());
	EXPECT_STREQ(L"refs/remotes/origin/master", list[7].c_str());
}

TEST_P(CBasicGitWithTestRepoFixture, GetUnifiedDiff)
{
	auto tmpfile = m_Dir.GetTempDir() / L"output.txt";
	EXPECT_EQ(0, m_Git.GetUnifiedDiff(CTGitPath(L""), L"b02add66f48814a73aa2f0876d6bbc8662d6a9a8", L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb", tmpfile.c_str(), false, false, -1, false));
	CString fileContents;
	EXPECT_EQ(true, StringUtils::ReadStringFromTextFile(tmpfile.c_str(), fileContents));
	EXPECT_STREQ(L" utf8-nobom.txt | 4 ++--\n 1 file changed, 2 insertions(+), 2 deletions(-)\n\ndiff --git a/utf8-nobom.txt b/utf8-nobom.txt\nindex ffa0d50..c225b3f 100644\n--- a/utf8-nobom.txt\n+++ b/utf8-nobom.txt\n@@ -1,9 +1,9 @@\n-ä#äf34öööäß€9875oe\r\n+ä#äf34ööcöäß€9875oe\r\n fgdjkglsfdg\r\n öäöü45g\r\n fdgi&§$%&hfdsgä\r\n ä#äf34öööäß€9875oe\r\n-öäüpfgmfdg\r\n+öäcüpfgmfdg\r\n €fgfdsg\r\n 45\r\n äü\n\\ No newline at end of file\n", fileContents);
}

// GIT_CMD_DIFF is not in the default mask, so which backend GetUnifiedDiff picks
// is decided here rather than by the fixture parameter: the whole point is to run
// both and compare them.
static CString GetUnifiedDiffWith(CGit& git, bool useLibGit2, const CString& rev1, const CString& rev2, bool bMerge, int diffContext, const fs::path& tmpfile)
{
	const bool oldUse = git.m_IsUseLibGit2;
	const DWORD oldMask = git.m_IsUseLibGit2_mask;
	SCOPE_EXIT
	{
		git.m_IsUseLibGit2 = oldUse;
		git.m_IsUseLibGit2_mask = oldMask;
		g_Git.m_IsUseLibGit2 = oldUse;
		g_Git.m_IsUseLibGit2_mask = oldMask;
	};
	git.m_IsUseLibGit2 = useLibGit2;
	git.m_IsUseLibGit2_mask = useLibGit2 ? (1 << TGitH::GIT_CMD_DIFF) : 0;
	// the libgit2 path opens the repository through g_Git, not through the CGit it
	// was called on, so both have to agree
	g_Git.m_IsUseLibGit2 = git.m_IsUseLibGit2;
	g_Git.m_IsUseLibGit2_mask = git.m_IsUseLibGit2_mask;

	CString contents;
	EXPECT_EQ(0, git.GetUnifiedDiff(CTGitPath(L""), rev1, rev2, tmpfile.c_str(), bMerge, false, diffContext, false));
	EXPECT_TRUE(StringUtils::ReadStringFromTextFile(tmpfile.c_str(), contents));
	return contents;
}

TEST_P(CBasicGitWithTestRepoFixture, GetUnifiedDiffLibGit2MatchesGitExe)
{
	const auto tmpfile = m_Dir.GetTempDir() / L"output.txt";
	constexpr auto rev1 = L"b02add66f48814a73aa2f0876d6bbc8662d6a9a8";
	constexpr auto rev2 = L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb";

	// -1 is "leave it to git"; the other two are --unified=N, which the libgit2 path
	// used to drop on the floor - it took no diffContext argument at all.
	for (const int diffContext : { -1, 1, 7 })
	{
		SCOPED_TRACE(diffContext);
		EXPECT_STREQ(GetUnifiedDiffWith(m_Git, false, rev1, rev2, false, diffContext, tmpfile),
					 GetUnifiedDiffWith(m_Git, true, rev1, rev2, false, diffContext, tmpfile));
	}

	// A merge, against every parent. 18da7c33 differs from both of its parents, so
	// the first-parent-only diff the libgit2 path used to emit is half the answer:
	// its bMerge argument was a named-out, unused parameter.
	// git.exe prints the commit id ahead of each parent's diff and libgit2 has no
	// such line to print, which is the one difference the comparison allows for.
	CString cli = GetUnifiedDiffWith(m_Git, false, L"", L"18da7c332dcad0f37f9977d9176dce0b0c66f3eb", true, -1, tmpfile);
	cli.Replace(L"18da7c332dcad0f37f9977d9176dce0b0c66f3eb\n", L"");
	EXPECT_STREQ(cli, GetUnifiedDiffWith(m_Git, true, L"", L"18da7c332dcad0f37f9977d9176dce0b0c66f3eb", true, -1, tmpfile));

	// And the direction. With the working copy as the *old* side the diff runs
	// backwards - the `-R` of the command line spelling, which the libgit2 path had
	// no equivalent of and so produced the diff the wrong way round.
	std::string output;
	ASSERT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	ASSERT_TRUE(StringUtils::WriteStringToTextFile((m_Dir.GetTempDir() / L"utf8-nobom.txt").c_str(), L"changed in the working copy\n"));
	EXPECT_STREQ(GetUnifiedDiffWith(m_Git, false, GitRev::GetWorkingCopyRef(), L"HEAD", false, -1, tmpfile),
				 GetUnifiedDiffWith(m_Git, true, GitRev::GetWorkingCopyRef(), L"HEAD", false, -1, tmpfile));
	EXPECT_STREQ(GetUnifiedDiffWith(m_Git, false, L"HEAD", GitRev::GetWorkingCopyRef(), false, -1, tmpfile),
				 GetUnifiedDiffWith(m_Git, true, L"HEAD", GitRev::GetWorkingCopyRef(), false, -1, tmpfile));
}

static void GetGitNotes(CGit& m_Git, config testConfig)
{
	if (testConfig != LIBGIT2_ALL)
		return;

	CString notes;
	EXPECT_EQ(0, m_Git.GetGitNotes(CGitHash::FromHexStr(L"1fc3c9688e27596d8717b54f2939dc951568f6cb"), notes));
	EXPECT_STREQ(L"A note here!\n", notes);

	EXPECT_EQ(0, m_Git.GetGitNotes(CGitHash::FromHexStr(L"1ce788330fd3a306c8ad37654063ceee13a7f172"), notes));
	EXPECT_STREQ(L"", notes);
}

TEST_P(CBasicGitWithTestRepoFixture, GetGitNotes)
{
	GetGitNotes(m_Git, GetParam());
}

TEST_P(CBasicGitWithTestRepoBareFixture, GetGitNotes)
{
	GetGitNotes(m_Git, GetParam());
}

TEST_P(CBasicGitWithTestRepoFixture, GuessRefForHash)
{
	CString ref;
	// HEAD
	EXPECT_EQ(0, m_Git.GuessRefForHash(ref, CGitHash::FromHexStr(L"7c3cbfe13a929d2291a574dca45e4fd2d2ac1aa6")));
	EXPECT_STREQ(L"master", ref);

	// local branch
	EXPECT_EQ(0, m_Git.GuessRefForHash(ref, CGitHash::FromHexStr(L"10385764a4d42d7428bbeb245015f8f338fc1e40")));
	EXPECT_STREQ(L"forconflict", ref);

	// stash
	EXPECT_EQ(1, m_Git.GuessRefForHash(ref, CGitHash::FromHexStr(L"18da7c332dcad0f37f9977d9176dce0b0c66f3eb")));
	EXPECT_STREQ(CString(L"18da7c332dcad0f37f9977d9176dce0b0c66f3eb").Left(m_Git.GetShortHASHLength()), ref);

	// tag only
	EXPECT_EQ(0, m_Git.GuessRefForHash(ref, CGitHash::FromHexStr(L"b9ef30183497cdad5c30b88d32dc1bed7951dfeb")));
	EXPECT_STREQ(L"normal-tag", ref);

	// local branch & tag
	EXPECT_EQ(0, m_Git.GuessRefForHash(ref, CGitHash::FromHexStr(L"49ecdfff36bfe2b9b499b33e5034f427e2fa54dd")));
	EXPECT_STREQ(L"master2", ref);

	// remote branch
	EXPECT_EQ(0, m_Git.GuessRefForHash(ref, CGitHash::FromHexStr(L"a9d53b535cb49640a6099860ac4999f5a0857b91")));
	EXPECT_STREQ(L"origin/master", ref);

	// no ref
	EXPECT_EQ(1, m_Git.GuessRefForHash(ref, CGitHash::FromHexStr(L"1ce788330fd3a306c8ad37654063ceee13a7f172")));
	EXPECT_STREQ(CString(L"1ce788330fd3a306c8ad37654063ceee13a7f172").Left(m_Git.GetShortHASHLength()), ref);
}

TEST_P(CBasicGitWithTestRepoFixture, IsResultingCommitBecomeEmpty)
{
	// all files are missing, thus we resulting commit won't be empty
	EXPECT_EQ(FALSE, m_Git.IsResultingCommitBecomeEmpty());
	EXPECT_EQ(FALSE, m_Git.IsResultingCommitBecomeEmpty(true));

	std::string output;
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"reset", L"--hard", L"master"}, &output));
	EXPECT_NE("", output);

	// now we have no staged stuff
	EXPECT_EQ(TRUE, m_Git.IsResultingCommitBecomeEmpty());
	EXPECT_EQ(FALSE, m_Git.IsResultingCommitBecomeEmpty(true));

	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"commit", L"--allow-empty", L"-m", L"Add test.txt"}, &output));
	EXPECT_NE("", output);

	// empty commit and no staged files
	EXPECT_EQ(TRUE, m_Git.IsResultingCommitBecomeEmpty());
	EXPECT_EQ(TRUE, m_Git.IsResultingCommitBecomeEmpty(true));

	// create a file and stage it, thus, resulting commits won't be empty
	auto testFile = m_Dir.GetTempDir() / L"test.txt";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"this is testing file."));
	EXPECT_EQ(TRUE, m_Git.IsResultingCommitBecomeEmpty());
	EXPECT_EQ(TRUE, m_Git.IsResultingCommitBecomeEmpty(true));
	output.clear();
	EXPECT_EQ(0, m_Git.Run({L"git.exe", L"add", L"test.txt"}, &output));
	EXPECT_STREQ("", output);

	EXPECT_EQ(FALSE, m_Git.IsResultingCommitBecomeEmpty());
	EXPECT_EQ(FALSE, m_Git.IsResultingCommitBecomeEmpty(true));
}

TEST(CGit, ParseConflictHashesFromLsFile_Empty)
{
	std::string byteArray;
	bool baseIsFile = true, localIsFile = true, remoteIsFile = true;
	CGitHash baseHash, localHash, remoteHash;
	EXPECT_NE(0, TGitH::ParseConflictHashesFromLsFile(byteArray, baseHash, baseIsFile, localHash, localIsFile, remoteHash, remoteIsFile));
}

TEST(CGit, ParseConflictHashesFromLsFile_SimpleConflict)
{
	constexpr char git_ls_file_u_t_z_output[] = { "M 100644 1f9f46da1ee155aa765d6e379d9d19853358cb07 1	bla.txt\0M 100644 3aa011e7d3609ab9af90c4b10f616312d2be422f 2	bla.txt\0M 100644 56d252d69d535834b9fbfa6f6a633ecd505ea2e6 3	bla.txt" };
	std::string byteArray;
	byteArray.append(git_ls_file_u_t_z_output, sizeof(git_ls_file_u_t_z_output));
	bool baseIsFile = true, localIsFile = true, remoteIsFile = true;
	CGitHash baseHash, localHash, remoteHash;
	EXPECT_EQ(0, TGitH::ParseConflictHashesFromLsFile(byteArray, baseHash, baseIsFile, localHash, localIsFile, remoteHash, remoteIsFile));
	EXPECT_STREQ(L"1f9f46da1ee155aa765d6e379d9d19853358cb07", baseHash.ToString());
	EXPECT_TRUE(baseIsFile);
	EXPECT_STREQ(L"3aa011e7d3609ab9af90c4b10f616312d2be422f", localHash.ToString());
	EXPECT_TRUE(localIsFile);
	EXPECT_STREQ(L"56d252d69d535834b9fbfa6f6a633ecd505ea2e6", remoteHash.ToString());
	EXPECT_TRUE(remoteIsFile);
}

TEST(CGit, ParseConflictHashesFromLsFile_SubmoduleConflict_Simple)
{
	constexpr char git_ls_files_u_t_z_output[] = { "M 160000 46a2b8e855d5f6d8b60b81500a9f6779c7f63e63 1	libgit2\0M 160000 533da4ea00703f4ad6d5518e1ce81d20261c40c0 2	libgit2\0M 160000 ab2af775ec467ebb328a7374653f247920f258f3 3	libgit2" };
	std::string byteArray;
	byteArray.append(git_ls_files_u_t_z_output, sizeof(git_ls_files_u_t_z_output));
	bool baseIsFile = true, localIsFile = true, remoteIsFile = true;
	CGitHash baseHash, localHash, remoteHash;
	EXPECT_EQ(0, TGitH::ParseConflictHashesFromLsFile(byteArray, baseHash, baseIsFile, localHash, localIsFile, remoteHash, remoteIsFile));
	EXPECT_STREQ(L"46a2b8e855d5f6d8b60b81500a9f6779c7f63e63", baseHash.ToString());
	EXPECT_FALSE(baseIsFile);
	EXPECT_STREQ(L"533da4ea00703f4ad6d5518e1ce81d20261c40c0", localHash.ToString());
	EXPECT_FALSE(localIsFile);
	EXPECT_STREQ(L"ab2af775ec467ebb328a7374653f247920f258f3", remoteHash.ToString());
	EXPECT_FALSE(remoteIsFile);
}

TEST(CGit, ParseConflictHashesFromLsFile_SubmoduleConflict_DeletedModified)
{
	// merged commit where submodule was modified into commit where it was deleted (git status says "deleted by us: libgit2")
	constexpr char git_ls_files_u_t_z_output[] = { "M 160000 46a2b8e855d5f6d8b60b81500a9f6779c7f63e63 1	libgit2\0M 160000 ab2af775ec467ebb328a7374653f247920f258f3 3	libgit2" };
	std::string byteArray;
	byteArray.append(git_ls_files_u_t_z_output, sizeof(git_ls_files_u_t_z_output));
	bool baseIsFile = true, localIsFile = true, remoteIsFile = true;
	CGitHash baseHash, localHash, remoteHash;
	EXPECT_EQ(0, TGitH::ParseConflictHashesFromLsFile(byteArray, baseHash, baseIsFile, localHash, localIsFile, remoteHash, remoteIsFile));
	EXPECT_STREQ(L"46a2b8e855d5f6d8b60b81500a9f6779c7f63e63", baseHash.ToString());
	EXPECT_FALSE(baseIsFile);
	EXPECT_STREQ(L"0000000000000000000000000000000000000000", localHash.ToString());
	EXPECT_TRUE(localIsFile);
	EXPECT_STREQ(L"ab2af775ec467ebb328a7374653f247920f258f3", remoteHash.ToString());
	EXPECT_FALSE(remoteIsFile);
}

TEST(CGit, ParseConflictHashesFromLsFile_SubmoduleConflict_ToNormalDir)
{
	// merged commit where libgit2 is a normal folder (git status says "added by us: libgit2" and "new file: libgit2/Neues Textdokument.txt")
	constexpr char git_ls_files_u_t_z_output[] = { "M 160000 533da4ea00703f4ad6d5518e1ce81d20261c40c0 2	libgit2" };
	std::string byteArray;
	byteArray.append(git_ls_files_u_t_z_output, sizeof(git_ls_files_u_t_z_output));
	bool baseIsFile = true, localIsFile = true, remoteIsFile = true;
	CGitHash baseHash, localHash, remoteHash;
	EXPECT_EQ(0, TGitH::ParseConflictHashesFromLsFile(byteArray, baseHash, baseIsFile, localHash, localIsFile, remoteHash, remoteIsFile));
	EXPECT_STREQ(L"0000000000000000000000000000000000000000", baseHash.ToString());
	EXPECT_TRUE(baseIsFile);
	EXPECT_STREQ(L"533da4ea00703f4ad6d5518e1ce81d20261c40c0", localHash.ToString());
	EXPECT_FALSE(localIsFile);
	EXPECT_STREQ(L"0000000000000000000000000000000000000000", remoteHash.ToString());
	EXPECT_TRUE(remoteIsFile);
}

TEST(CGit, ParseConflictHashesFromLsFile_SubmoduleConflict_FileSubmodule)
{
	// merged commit where a file with the same name was created (git status says "both added: libgit2")
	constexpr char git_ls_file_u_t_z_output[] = { "M 160000 533da4ea00703f4ad6d5518e1ce81d20261c40c0 2	libgit2\0M 100644 9ae3e601584cc03f8f03f93761416b6599ac7c0d 3	libgit2" };
	std::string byteArray;
	byteArray.append(git_ls_file_u_t_z_output, sizeof(git_ls_file_u_t_z_output));
	bool baseIsFile = true, localIsFile = true, remoteIsFile = true;
	CGitHash baseHash, localHash, remoteHash;
	EXPECT_EQ(0, TGitH::ParseConflictHashesFromLsFile(byteArray, baseHash, baseIsFile, localHash, localIsFile, remoteHash, remoteIsFile));
	EXPECT_STREQ(L"0000000000000000000000000000000000000000", baseHash.ToString());
	EXPECT_TRUE(baseIsFile);
	EXPECT_STREQ(L"533da4ea00703f4ad6d5518e1ce81d20261c40c0", localHash.ToString());
	EXPECT_FALSE(localIsFile);
	EXPECT_STREQ(L"9ae3e601584cc03f8f03f93761416b6599ac7c0d", remoteHash.ToString());
	EXPECT_TRUE(remoteIsFile);
}

TEST(CGit, ParseConflictHashesFromLsFile_DeletedFileConflict)
{
	// file added, modified on branch A, deleted on branch B, merge branch A on B (git status says: "deleted by us")
	constexpr char git_ls_file_u_t_z_output[] = { "M 100644 24091f0add7afc47ac7cdc80ae4d3866b2ef588c 1	Neues Textdokument.txt\0M 100644 293b6f6293106b6ebb5d54ad482d7561b0f1c9ae 3	Neues Textdokument.txt" };
	std::string byteArray;
	byteArray.append(git_ls_file_u_t_z_output, sizeof(git_ls_file_u_t_z_output));
	bool baseIsFile = true, localIsFile = true, remoteIsFile = true;
	CGitHash baseHash, localHash, remoteHash;
	EXPECT_EQ(0, TGitH::ParseConflictHashesFromLsFile(byteArray, baseHash, baseIsFile, localHash, localIsFile, remoteHash, remoteIsFile));
	EXPECT_STREQ(L"24091f0add7afc47ac7cdc80ae4d3866b2ef588c", baseHash.ToString());
	EXPECT_TRUE(baseIsFile);
	EXPECT_STREQ(L"0000000000000000000000000000000000000000", localHash.ToString());
	EXPECT_TRUE(localIsFile);
	EXPECT_STREQ(L"293b6f6293106b6ebb5d54ad482d7561b0f1c9ae", remoteHash.ToString());
	EXPECT_TRUE(remoteIsFile);
}

TEST(CGit, ParseConflictHashesFromLsFile_Invalid)
{
	constexpr char git_ls_file_u_t_z_output[] = { "M 160000 533da4ea00703f4ad6d5518e1ce81d20261c40c0 2	libgit2\0M 100644 9ae3e601584cc03f8f03f93761416b6599ac7c0d 3	libgit2" };
	for (auto i = 1ull; i < sizeof(git_ls_file_u_t_z_output); ++i)
	{
		if (i == 60)
			continue;
		std::string byteArray;
		byteArray.append(git_ls_file_u_t_z_output, i);
		bool baseIsFile = true, localIsFile = true, remoteIsFile = true;
		CGitHash baseHash, localHash, remoteHash;
		EXPECT_EQ(-1, TGitH::ParseConflictHashesFromLsFile(byteArray, baseHash, baseIsFile, localHash, localIsFile, remoteHash, remoteIsFile));
	}
}

TEST_P(CBasicGitWithSubmoduleRepositoryFixture, GetSubmoduleHash)
{
	if (GetParam() == GIT_CLI)
	{
		CGitHash submoduleHash;
		CString error;
		EXPECT_EQ(0, g_Git.GetSubmoduleHash(L"something", CGitHash::FromHexStr(L"900539cd24776a94d1b642358ccfdb9d897c8254"), submoduleHash, error));
		EXPECT_TRUE(submoduleHash.IsEmpty());
		EXPECT_EQ(0, g_Git.GetSubmoduleHash(L"doesnotexist", CGitHash::FromHexStr(L"900539cd24776a94d1b642358ccfdb9d897c8254"), submoduleHash, error));
		EXPECT_TRUE(submoduleHash.IsEmpty());
	}
	if (GetParam() != LIBGIT2)
		return;

	{
		CGitHash submoduleHash;
		CString error;
		EXPECT_EQ(0, g_Git.GetSubmoduleHash(L"something", CGitHash::FromHexStr(L"900539cd24776a94d1b642358ccfdb9d897c8254"), submoduleHash, error));
		EXPECT_STREQ(L"35c91b4ae2f77f4f21a7aba56d3c473c705d89e6", submoduleHash.ToString());
	}

	{
		CGitHash submoduleHash;
		CString error;
		EXPECT_EQ(0, g_Git.GetSubmoduleHash(L"something", CGitHash::FromHexStr(L"c8d17f57c7b511aff4aa2fbfae158902281cad8e"), submoduleHash, error));
		EXPECT_STREQ(L"8eabf9a475b4a15c0f4d2169e5947534dff38037", submoduleHash.ToString());
	}

	{
		CGitHash submoduleHash;
		CString error;
		EXPECT_EQ(-1, g_Git.GetSubmoduleHash(L"some file.txt", CGitHash::FromHexStr(L"e8199180d4f1eda395cb47c6142f78b2c525bb70"), submoduleHash, error));
		EXPECT_STREQ(L"No submodule found at \"some file.txt\" in revision e8199180d4f1eda395cb47c6142f78b2c525bb70.", error);
	}

	{
		CGitHash submoduleHash;
		CString error;
		EXPECT_EQ(-1, g_Git.GetSubmoduleHash(L"doesnotexist", CGitHash::FromHexStr(L"b9ad06b41c00bd309f9639f92c96165813c6ebe3"), submoduleHash, error));
		EXPECT_STRNE(L"", error);
	}
}

TEST(CGit, QuoteParameter)
{
	const auto oldCygwinGit = GitCall::ms_bCygwinGit;
	const auto oldMsys2Git = GitCall::ms_bMsys2Git;
	SCOPE_EXIT{ GitCall::ms_bCygwinGit = oldCygwinGit; GitCall::ms_bMsys2Git = oldMsys2Git; };
	GitCall::ms_bCygwinGit = GitCall::ms_bMsys2Git = false;

	EXPECT_STREQ(L"\"\"", CGit::QuoteParameter(L""));
	EXPECT_STREQ(L"\"/\"", CGit::QuoteParameter(L"/"));
	EXPECT_STREQ(L"\"src/Git.h\"", CGit::QuoteParameter(L"src/Git.h"));
	EXPECT_STREQ(L"\"src/Git/\"", CGit::QuoteParameter(L"src/Git/"));
	EXPECT_STREQ(L"\"src/Gi't/\"", CGit::QuoteParameter(L"src/Gi't/"));
	EXPECT_STREQ(L"\"src/Gi%t/\"", CGit::QuoteParameter(L"src/Gi%t/"));
	EXPECT_STREQ(L"\"src/Git/\"", CGit::QuoteParameter(L"src/Git/"));
	EXPECT_STREQ(L"\"src Git/\"", CGit::QuoteParameter(L"src Git/"));
	EXPECT_STREQ(L"\"HEAD~3\"", CGit::QuoteParameter(L"HEAD~3"));
	EXPECT_STREQ(L"\"^HEAD\"", CGit::QuoteParameter(L"^HEAD"));
	EXPECT_STREQ(L"\"ümlaut\"", CGit::QuoteParameter(L"ümlaut"));
	EXPECT_STREQ(L"\"--safe\"", CGit::QuoteParameter(L"--safe"));

	EXPECT_STREQ(L"\"src/Gi$t/\"", CGit::QuoteParameter(L"src/Gi$t/"));
	EXPECT_STREQ(L"\"src/Gi`pwd`t/\"", CGit::QuoteParameter(L"src/Gi`pwd`t/"));

	EXPECT_STREQ(L"\"/\"", CGit::QuoteParameter(L"\\"));
	EXPECT_STREQ(L"\"src/Git.h\"", CGit::QuoteParameter(L"src\\Git.h"));
	EXPECT_STREQ(L"\"C:/src/Git.h\"", CGit::QuoteParameter(L"C:\\src\\Git.h"));
	EXPECT_STREQ(L"\"C:/\"", CGit::QuoteParameter(L"C:\\"));
	EXPECT_STREQ(L"\"//localhost/some/path\"", CGit::QuoteParameter(L"\\\\localhost\\some\\path"));

	// These eight threw illegal_git_parameter. Any value is quotable now: a double
	// quote is escaped (\") as CommandLineToArgvW reads it back, and ? and * are
	// plain characters inside quotes (wil::ArgvToCommandLine, ForceQuotes).
	EXPECT_STREQ(L"\"src\\\"/Git.h\"", CGit::QuoteParameter(L"src\"/Git.h"));
	EXPECT_STREQ(L"\"sr\\\"c/Git.h\"", CGit::QuoteParameter(L"sr\"c/Git.h"));
	EXPECT_STREQ(L"\"\\\"git\\\"\"", CGit::QuoteParameter(L"\"git\""));
	EXPECT_STREQ(L"\"\\\"\"", CGit::QuoteParameter(L"\""));
	EXPECT_STREQ(L"\"/\\\"\"", CGit::QuoteParameter(L"\\\""));
	EXPECT_STREQ(L"\"git\\\"\"", CGit::QuoteParameter(L"git\""));
	EXPECT_STREQ(L"\"src/Gi*t/\"", CGit::QuoteParameter(L"src/Gi*t/"));
	EXPECT_STREQ(L"\"src/Gi?t/\"", CGit::QuoteParameter(L"src/Gi?t/"));

	// Relaxed mode (e.g., for config)
	EXPECT_STREQ(L"\"C:\\\\\"", CGit::QuoteParameter(L"C:\\", true));
	EXPECT_STREQ(L"\"Hell*o \\\"Git\\\"\"", CGit::QuoteParameter(L"Hell*o \"Git\"", true));
}

static void QuoteParameter_Msys2Cygwin()
{
	EXPECT_STREQ(L"''", CGit::QuoteParameter(L""));
	EXPECT_STREQ(L"'/'", CGit::QuoteParameter(L"/"));
	EXPECT_STREQ(L"'src/Git.h'", CGit::QuoteParameter(L"src/Git.h"));
	EXPECT_STREQ(L"'src/Git/'", CGit::QuoteParameter(L"src/Git/"));
	EXPECT_STREQ(L"'src/Gi'\\''t/'", CGit::QuoteParameter(L"src/Gi't/"));
	EXPECT_STREQ(L"'src/Gi%t/'", CGit::QuoteParameter(L"src/Gi%t/"));
	EXPECT_STREQ(L"'src/Git/'", CGit::QuoteParameter(L"src/Git/"));
	EXPECT_STREQ(L"'src Git/'", CGit::QuoteParameter(L"src Git/"));
	EXPECT_STREQ(L"'HEAD~3'", CGit::QuoteParameter(L"HEAD~3"));
	EXPECT_STREQ(L"'^HEAD'", CGit::QuoteParameter(L"^HEAD"));
	EXPECT_STREQ(L"'ümlaut'", CGit::QuoteParameter(L"ümlaut"));
	EXPECT_STREQ(L"'--safe'", CGit::QuoteParameter(L"--safe"));

	EXPECT_STREQ(L"'src/Gi$t/'", CGit::QuoteParameter(L"src/Gi$t/"));
	EXPECT_STREQ(L"'src/Gi`pwd`t/'", CGit::QuoteParameter(L"src/Gi`pwd`t/"));

	EXPECT_STREQ(L"'/'", CGit::QuoteParameter(L"\\"));
	EXPECT_STREQ(L"'src/Git.h'", CGit::QuoteParameter(L"src\\Git.h"));
	EXPECT_STREQ(L"'C:/src/Git.h'", CGit::QuoteParameter(L"C:\\src\\Git.h"));
	EXPECT_STREQ(L"'C:/'", CGit::QuoteParameter(L"C:\\"));
	EXPECT_STREQ(L"'//localhost/some/path'", CGit::QuoteParameter(L"\\\\localhost\\some\\path"));

	EXPECT_STREQ(L"'src\"/Git.h'", CGit::QuoteParameter(L"src\"/Git.h"));
	EXPECT_STREQ(L"'sr\"c/Git.h'", CGit::QuoteParameter(L"sr\"c/Git.h"));
	EXPECT_STREQ(L"'\"git\"'", CGit::QuoteParameter(L"\"git\""));
	EXPECT_STREQ(L"'\"'", CGit::QuoteParameter(L"\""));
	EXPECT_STREQ(L"'/\"'", CGit::QuoteParameter(L"\\\""));
	EXPECT_STREQ(L"'git\"'", CGit::QuoteParameter(L"git\""));
	EXPECT_STREQ(L"'src/Gi*t/'", CGit::QuoteParameter(L"src/Gi*t/"));
	EXPECT_STREQ(L"'src/Gi?t/'", CGit::QuoteParameter(L"src/Gi?t/"));

	// Relaxed mode (e.g., for config)
	EXPECT_STREQ(L"'C:\\'", CGit::QuoteParameter(L"C:\\", true));
	EXPECT_STREQ(L"'Hell*o \"Git\"'", CGit::QuoteParameter(L"Hell*o \"Git\"", true));
	EXPECT_STREQ(L"'Hell*o'\\''s \"Git\"'", CGit::QuoteParameter(L"Hell*o's \"Git\"", true));
}

TEST(CGit, QuoteParameter_Msys2)
{
	const auto oldCygwinGit = GitCall::ms_bCygwinGit;
	const auto oldMsys2Git = GitCall::ms_bMsys2Git;
	SCOPE_EXIT{ GitCall::ms_bCygwinGit = oldCygwinGit; GitCall::ms_bMsys2Git = oldMsys2Git; };
	GitCall::ms_bCygwinGit = GitCall::ms_bMsys2Git = false;
	GitCall::ms_bMsys2Git = true;

	QuoteParameter_Msys2Cygwin();
}

TEST(CGit, QuoteParameter_Cygwin)
{
	const auto oldCygwinGit = GitCall::ms_bCygwinGit;
	const auto oldMsys2Git = GitCall::ms_bMsys2Git;
	SCOPE_EXIT{ GitCall::ms_bCygwinGit = oldCygwinGit; GitCall::ms_bMsys2Git = oldMsys2Git; };
	GitCall::ms_bCygwinGit = GitCall::ms_bMsys2Git = false;
	GitCall::ms_bCygwinGit = true;

	QuoteParameter_Msys2Cygwin();
}

// SerializeArgv moved onto wil::ArgvData and takes the msys/cygwin switch explicitly;
// these tests flip GitCall's flags and expect the serialization to follow them.
static std::wstring SerializeArgv(const wil::ArgvData& argv)
{
	return argv.SerializeArgv(GitCall::has_weird_paths());
}

TEST(CGit, SerializeArgv)
{
	const auto oldCygwinGit = GitCall::ms_bCygwinGit;
	const auto oldMsys2Git = GitCall::ms_bMsys2Git;
	SCOPE_EXIT{ GitCall::ms_bCygwinGit = oldCygwinGit; GitCall::ms_bMsys2Git = oldMsys2Git; };
	GitCall::ms_bCygwinGit = GitCall::ms_bMsys2Git = false;

	EXPECT_STREQ(L"", SerializeArgv({}).c_str());
	EXPECT_STREQ(L"git.exe log --pretty=raw", SerializeArgv({ L"git.exe", L"log", L"--pretty=raw" }).c_str());

	// argv[0] must come out bare: RunAsync decides whether to prepend
	// ms_LastMsysGitDir by testing whether the line starts with "git", so a
	// quoted argv[0] would silently resolve git.exe through PATH instead.
	EXPECT_TRUE(SerializeArgv({ L"git.exe", L"status" }).starts_with(L"git.exe "));

	// only elements that would read differently unquoted get quotes
	EXPECT_STREQ(L"git.exe show \"my file.txt\"", SerializeArgv({ L"git.exe", L"show", L"my file.txt" }).c_str());
	// backslashes are not special unless a double quote follows them
	EXPECT_STREQ(L"git.exe add C:\\src\\a.txt", SerializeArgv({ L"git.exe", L"add", L"C:\\src\\a.txt" }).c_str());
	// an empty element has to stay an empty element rather than vanish
	EXPECT_STREQ(L"git.exe commit -m \"\"", SerializeArgv({ L"git.exe", L"commit", L"-m", L"" }).c_str());
	// unlike QuoteParameter, a quote is representable rather than a thrown exception
	EXPECT_STREQ(L"git.exe commit -m \"say \\\"hi\\\"\"", SerializeArgv({ L"git.exe", L"commit", L"-m", L"say \"hi\"" }).c_str());
}

static std::vector<std::wstring> ParseWindowsCommandLine(const std::wstring& cmd)
{
	std::vector<std::wstring> parsed;
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(cmd.c_str(), &argc);
	if (!argv)
		return parsed;
	SCOPE_EXIT{ LocalFree(argv); };
	for (int i = 0; i < argc; ++i)
		parsed.emplace_back(argv[i]);
	return parsed;
}

// The real oracle for the native serializer: hand the line back to the same
// parser CreateProcess'd children use and require every element to survive
// unchanged. Testing the *shape* of the quoting only pins today's spelling;
// this pins the property the spelling exists for.
TEST(CGit, SerializeArgvRoundTrip)
{
	const auto oldCygwinGit = GitCall::ms_bCygwinGit;
	const auto oldMsys2Git = GitCall::ms_bMsys2Git;
	SCOPE_EXIT{ GitCall::ms_bCygwinGit = oldCygwinGit; GitCall::ms_bMsys2Git = oldMsys2Git; };
	GitCall::ms_bCygwinGit = GitCall::ms_bMsys2Git = false;

	const std::vector<std::wstring> argv = {
		L"git.exe", // CommandLineToArgvW parses argv[0] by different rules, so it is excluded from the comparison below
		L"plain",
		L"with space",
		L"with\ttab",
		L"", // an empty argument is a real argument
		L"quote\"inside",
		L"\"",
		L"trailing\\",
		L"C:\\path\\to\\file",
		L"C:\\path with space\\",
		L"back\\\\slashes\"quote",
		L"\\\\?\\C:\\long\\path",
		L"\u00fcmlaut \u00e4\u00f6\u00fc",
		L"--pretty=format:%H %s",
		L"refs/heads/feature/x",
		L"HEAD~3",
		L"^HEAD",
		L"a*b?c",
	};

	const std::wstring cmd = SerializeArgv(argv);
	const std::vector<std::wstring> parsed = ParseWindowsCommandLine(cmd);
	ASSERT_EQ(argv.size(), parsed.size()) << "serialized as: " << CUnicodeUtils::StdGetUTF8(cmd);
	for (size_t i = 1; i < argv.size(); ++i)
		EXPECT_EQ(argv[i], parsed[i]) << "element " << i << " of: " << CUnicodeUtils::StdGetUTF8(cmd);
}

static void SerializeArgv_Msys2Cygwin()
{
	// bash reads this, so the rules are the shell's, not CommandLineToArgvW's
	EXPECT_STREQ(L"git.exe log", SerializeArgv({ L"git.exe", L"log" }).c_str());
	EXPECT_STREQ(L"git.exe show 'my file.txt'", SerializeArgv({ L"git.exe", L"show", L"my file.txt" }).c_str());
	// quoted because ~ is tilde expansion, ^ and * are history/glob - the
	// whitelist means new shell metacharacters are safe by default
	EXPECT_STREQ(L"git.exe log 'HEAD~3'", SerializeArgv({ L"git.exe", L"log", L"HEAD~3" }).c_str());
	EXPECT_STREQ(L"git.exe log '^HEAD'", SerializeArgv({ L"git.exe", L"log", L"^HEAD" }).c_str());
	EXPECT_STREQ(L"git.exe log '$(rm -rf /)'", SerializeArgv({ L"git.exe", L"log", L"$(rm -rf /)" }).c_str());
	// a single quote is the one character single quotes cannot hold
	EXPECT_STREQ(L"git.exe commit -m 'it'\\''s'", SerializeArgv({ L"git.exe", L"commit", L"-m", L"it's" }).c_str());
	EXPECT_STREQ(L"git.exe show ''", SerializeArgv({ L"git.exe", L"show", L"" }).c_str());
	// argv[0] stays bare here too - RunAsync prepends /usr/bin or /bin to it
	EXPECT_TRUE(SerializeArgv({ L"git.exe", L"status" }).starts_with(L"git.exe "));
}

TEST(CGit, SerializeArgv_Msys2)
{
	const auto oldCygwinGit = GitCall::ms_bCygwinGit;
	const auto oldMsys2Git = GitCall::ms_bMsys2Git;
	SCOPE_EXIT{ GitCall::ms_bCygwinGit = oldCygwinGit; GitCall::ms_bMsys2Git = oldMsys2Git; };
	GitCall::ms_bCygwinGit = false;
	GitCall::ms_bMsys2Git = true;

	SerializeArgv_Msys2Cygwin();
}

TEST(CGit, SerializeArgv_Cygwin)
{
	const auto oldCygwinGit = GitCall::ms_bCygwinGit;
	const auto oldMsys2Git = GitCall::ms_bMsys2Git;
	SCOPE_EXIT{ GitCall::ms_bCygwinGit = oldCygwinGit; GitCall::ms_bMsys2Git = oldMsys2Git; };
	GitCall::ms_bMsys2Git = false;
	GitCall::ms_bCygwinGit = true;

	SerializeArgv_Msys2Cygwin();
}
