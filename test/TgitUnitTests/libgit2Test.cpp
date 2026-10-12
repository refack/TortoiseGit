// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2015-2017, 2019 - TortoiseGit

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
#include <git2/sys/repository.h>
import str_utils;
import git_h;
import GitHash;

TEST(libgit2, Config)
{
	CAutoTempDir tempdir;
	CString testFile = tempdir.GetTempDir() + L"\\config";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile, L"[core]\nemail=dummy@example.com\ntrue=true\nfalse=false\n"));
	wil::unique_git_config config;
	git_config_new(config.put());
	EXPECT_EQ(0, git_config_add_file_ondisk(config.get(), CUnicodeUtils::GetUTF8<CStringA>(testFile), GIT_CONFIG_LEVEL_LOCAL, nullptr, 1));
	bool ret = false;
	EXPECT_EQ(0, TGitH::GetConfigBool(config.get(), L"core.true", ret));
	EXPECT_EQ(true, ret);
	EXPECT_EQ(0, TGitH::GetConfigBool(config.get(), L"core.false", ret));
	EXPECT_EQ(false, ret);
	EXPECT_EQ(-3, TGitH::GetConfigBool(config.get(), L"core.not-exist", ret));
	CString value;
	EXPECT_EQ(0, TGitH::GetConfigString(config.get(), L"core.email", value));
	EXPECT_STREQ(L"dummy@example.com", value);
}

// The readers moved from CAutoConfig to git_h with their contract unchanged; the
// callers (gitindex's core.ignorecase preset, SyncDlg/PullFetchDlg's pull.rebase,
// GetBoolConfigValueComboBox) depend on these exact outcomes.
TEST(libgit2, ConfigReadersKeepTheirContract)
{
	CAutoTempDir tempdir;
	CString testFile = tempdir.GetTempDir() + L"\\config";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile, L"[core]\nmaybe=maybe\nemail=dummy@example.com\n"));

	// no config: -1, and the in/out parameter is left alone
	int preset = 42;
	EXPECT_EQ(-1, TGitH::GetConfigBOOL(nullptr, L"core.maybe", preset));
	EXPECT_EQ(42, preset);
	CString untouched = L"keep";
	EXPECT_EQ(-1, TGitH::GetConfigString(nullptr, L"core.email", untouched));
	EXPECT_STREQ(L"keep", untouched);

	wil::unique_git_config config;
	git_config_new(config.put());
	EXPECT_EQ(0, git_config_add_file_ondisk(config.get(), CUnicodeUtils::GetUTF8<CStringA>(testFile), GIT_CONFIG_LEVEL_LOCAL, nullptr, 1));

	// a value that is not a bool: an error that is not GIT_ENOTFOUND, preset kept
	preset = 1;
	const int invalid = TGitH::GetConfigBOOL(config.get(), L"core.maybe", preset);
	EXPECT_LT(invalid, 0);
	EXPECT_NE(GIT_ENOTFOUND, invalid);
	EXPECT_EQ(1, preset);
	bool b = true;
	EXPECT_EQ(invalid, TGitH::GetConfigBool(config.get(), L"core.maybe", b));
	EXPECT_TRUE(b);

	// a missing key: GIT_ENOTFOUND; the bool is left alone, the string is emptied
	preset = 1;
	EXPECT_EQ(GIT_ENOTFOUND, TGitH::GetConfigBOOL(config.get(), L"core.not-exist", preset));
	EXPECT_EQ(1, preset);
	std::wstring s = L"previous";
	EXPECT_EQ(GIT_ENOTFOUND, TGitH::GetConfigString(config.get(), L"core.not-exist", s));
	EXPECT_TRUE(s.empty());

	// a CString key binds the constrained overload
	EXPECT_EQ(0, TGitH::GetConfigString(config.get(), CString(L"core.email"), s));
	EXPECT_EQ(L"dummy@example.com", s);
}

// the following test ís similar to a test in libgit2, keep it to prevent surprises
TEST(libgit2, ConfigSnaphot)
{
	CAutoTempDir tmpDir;

	wil::unique_git_repository repo;
	ASSERT_TRUE(git_repository_init(repo.put(), CUnicodeUtils::GetUTF8<CStringA>(tmpDir.GetTempDir()), false) == 0);
	// The includeIf sections below match on the branch name, and libgit2 honours the
	// host's init.defaultBranch just as git.exe does. Pin HEAD so the test describes
	// the config machinery rather than the machine it runs on.
	ASSERT_TRUE(git_repository_set_head(repo.get(), "refs/heads/master") == 0);
	CString configFile = tmpDir.GetTempDir() + L"\\.git\\config";
	CString text;
	ASSERT_TRUE(StringUtils::ReadStringFromTextFile(configFile, text));
	text += L"[includeIf \"onbranch:master\"]\n\tpath = master.txt\n";
	text += L"[includeIf \"onbranch:other\"]\n\tpath = other.txt\n";
	text += L"[include]\n\tpath = include.txt\n";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(configFile, text));

	EXPECT_TRUE(StringUtils::WriteStringToTextFile(tmpDir.GetTempDir() + L"\\.git\\master.txt", L"[blup]\n\tbran = master\nmaster\n"));
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(tmpDir.GetTempDir() + L"\\.git\\other.txt", L"[blup]\n\tbran = other\nother\n"));
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(tmpDir.GetTempDir() + L"\\.git\\include.txt", L"[blup]\n\tfoo = bar\n"));

	wil::unique_git_config config;
	EXPECT_EQ(0, git_repository_config_snapshot(config.put(), repo.get()));
	repo.reset();

	EXPECT_TRUE(StringUtils::WriteStringToTextFile(tmpDir.GetTempDir() + L"\\.git\\include.txt", L"[blup]\n\tfoo = changedafterwards\n"));

	bool bResult = false;
	EXPECT_EQ(-3, TGitH::GetConfigBool(config.get(), L"blup.doesnotexist", bResult));
	EXPECT_EQ(-3, TGitH::GetConfigBool(config.get(), L"blup.other", bResult));
	EXPECT_EQ(0, TGitH::GetConfigBool(config.get(), L"blup.master", bResult));
	EXPECT_TRUE(bResult);

	CString sResult;
	EXPECT_EQ(0, TGitH::GetConfigString(config.get(), L"blup.bran", sResult));
	EXPECT_STREQ(L"master", sResult);
	sResult.Empty();
	EXPECT_EQ(0, TGitH::GetConfigString(config.get(), L"blup.foo", sResult));
	EXPECT_STREQ(L"bar", sResult);
}

// check whether a snapshotted config can be used in git_repository and then working on the repo using hashfile as TGitCache does in GitIndex.cpp
TEST(libgit2, ConfigSnaphotRepoHashFile)
{
	CAutoTempDir tmpDir;

	wil::unique_git_repository repo;
	ASSERT_TRUE(git_repository_init(repo.put(), CUnicodeUtils::GetUTF8<CStringA>(tmpDir.GetTempDir()), false) == 0);
	CString configFile = tmpDir.GetTempDir() + L"\\.git\\config";
	CString text;
	ASSERT_TRUE(StringUtils::ReadStringFromTextFile(configFile, text));
	text += L"[user]\n\tuser = Someone\n\temail = some@one.de\n";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(configFile, text));

	EXPECT_TRUE(StringUtils::WriteStringToTextFile(tmpDir.GetTempDir() + L"\\test.txt", L"something"));
	git_oid oid;
	EXPECT_EQ(0, git_repository_hashfile(&oid, repo.get(), "test.txt", GIT_OBJECT_BLOB, nullptr));
	EXPECT_STREQ(L"a459bc245bdbc45e1bca99e7fe61731da5c48da4", CGitHash(oid).ToString());

	wil::unique_git_config config;
	EXPECT_EQ(0, git_repository_config_snapshot(config.put(), repo.get()));
	git_repository_set_config(repo.get(), config.get());

	g_Git.SetCurrentDirExact(tmpDir.GetTempDir());
	CString output;
	EXPECT_EQ(0, g_Git.Run(L"git.exe add test.txt", &output, CP_UTF8));
	EXPECT_STREQ(L"", output);
	output.Empty();
	EXPECT_EQ(0, g_Git.Run(L"git.exe commit -m \"Add test.txt\"", &output, CP_UTF8));
	EXPECT_STRNE(L"", output);

	EXPECT_TRUE(StringUtils::WriteStringToTextFile(tmpDir.GetTempDir() + L"\\test.txt", L"something else"));

	git_oid oid2;
	EXPECT_EQ(0, git_repository_hashfile(&oid2, repo.get(), "test.txt", GIT_OBJECT_BLOB, nullptr));
	EXPECT_STREQ(L"339f0be6c0f5257a3279fcefc45373e805b7ebc3", CGitHash(oid2).ToString());

	EXPECT_TRUE(StringUtils::WriteStringToTextFile(tmpDir.GetTempDir() + L"\\test.txt", L"something"));

	git_oid oid3;
	EXPECT_EQ(0, git_repository_hashfile(&oid3, repo.get(), "test.txt", GIT_OBJECT_BLOB, nullptr));
	EXPECT_STREQ(L"a459bc245bdbc45e1bca99e7fe61731da5c48da4", CGitHash(oid3).ToString());
}
