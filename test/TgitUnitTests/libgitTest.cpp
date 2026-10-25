// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2016-2020, 2022-2023 - TortoiseGit

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
#include <gitdll.h>
import git_h;
import str_utils;

TEST(libgit, BrokenConfig)
{
	CAutoTempDir tempdir;
	g_Git.SetCurrentDirExact(tempdir.GetTempDir());
	g_Git.m_IsGitDllInited = false;
	g_Git.m_IsUseGitDLL = true;
	g_Git.m_IsUseLibGit2 = false;
	g_Git.m_IsUseLibGit2_mask = 0;
	// libgit relies on CWD being set to working tree
	SetCurrentDirectory(g_Git.m_CurrentDir.c_str());

	std::string output;
	EXPECT_EQ(0, g_Git.Run({L"git.exe", L"init", L"-b", L"master"}, &output));
	EXPECT_NE(""s, output);
	auto testFile = tempdir.GetTempDir() / L".git\\config";
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(testFile.c_str(), L"[push]\ndefault=something-that-is-invalid\n"));

	EXPECT_THROW(g_Git.CheckAndInitDll(), std::exception);
}

TEST(libgit, Mailmap)
{
	CAutoTempDir tempdir;
	g_Git.SetCurrentDirExact(tempdir.GetTempDir());
	// libgit relies on CWD being set to working tree
	SetCurrentDirectory(g_Git.m_CurrentDir.c_str());

	std::string output;
	EXPECT_EQ(0, g_Git.Run({L"git.exe", L"init", L"-b", L"master"}, &output));
	EXPECT_NE("", output);
	g_Git.ForceReInitDll();

	GIT_MAILMAP mailmap = reinterpret_cast<void*>(0x12345678);
	git_read_mailmap(&mailmap);
	EXPECT_EQ(nullptr, mailmap);

	CString mailmapFile = (tempdir.GetTempDir() / L".mailmap").c_str();
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(mailmapFile, L""));

	mailmap = reinterpret_cast<void*>(0x12345678);
	git_read_mailmap(&mailmap);
	EXPECT_EQ(nullptr, mailmap);

	EXPECT_TRUE(StringUtils::WriteStringToTextFile(mailmapFile, L"Sven Strickroth <sven@tortoisegit.org>"));
	git_read_mailmap(&mailmap);
	EXPECT_NE(nullptr, mailmap);
	const char* email1 = nullptr;
	const char* author1 = nullptr;
	EXPECT_EQ(-1, git_lookup_mailmap(mailmap, &email1, &author1, "email@cs-ware.de", nullptr, [](void*) { return "Sven S."; }));
	EXPECT_EQ(0, git_lookup_mailmap(mailmap, &email1, &author1, "sven@tortoisegit.org", nullptr, [](void*) { return "Sven S."; }));
	EXPECT_EQ(nullptr, email1);
	EXPECT_STREQ("Sven Strickroth", author1);

	email1 = nullptr;
	author1 = nullptr;
	EXPECT_EQ(0, git_lookup_mailmap(mailmap, &email1, &author1, "Sven@tortoisegit.org", nullptr, [](void*) { return "Sven S."; }));
	EXPECT_EQ(nullptr, email1);
	EXPECT_STREQ("Sven Strickroth", author1);

	git_free_mailmap(mailmap);
	CString content;
	for (auto& entry : { L"", L"1", L"2", L"A", L"4", L"5", L"b", L"7" })
		content.AppendFormat(L"Sven%s Strickroth <sven%s@tortoisegit.org> <email%s@cs-ware.de>\n", entry, entry, entry);
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(mailmapFile, content));
	git_read_mailmap(&mailmap);
	EXPECT_NE(nullptr, mailmap);
	email1 = nullptr;
	author1 = nullptr;
	EXPECT_EQ(-1, git_lookup_mailmap(mailmap, &email1, &author1, "sven@tortoisegit.org", nullptr, [](void*) { return "Sven S."; }));
	EXPECT_EQ(-1, git_lookup_mailmap(mailmap, &email1, &author1, "aaa@tortoisegit.org", nullptr, [](void*) { return "Sven S."; }));
	EXPECT_EQ(-1, git_lookup_mailmap(mailmap, &email1, &author1, "zzz@tortoisegit.org", nullptr, [](void*) { return "Sven S."; }));
	for (auto& entry : { "", "1", "2", "A", "4", "5", "b", "7" })
	{
		CStringA maillookup, mail, name;
		maillookup.Format("email%s@cs-ware.de", entry);
		mail.Format("sven%s@tortoisegit.org", entry);
		name.Format("Sven%s Strickroth", entry);
		email1 = nullptr;
		author1 = nullptr;
		EXPECT_EQ(0, git_lookup_mailmap(mailmap, &email1, &author1, maillookup, nullptr, [](void*) { return "Sven S."; }));
		EXPECT_STREQ(mail, email1);
		EXPECT_STREQ(name, author1);
	}

	email1 = nullptr;
	author1 = nullptr;
	EXPECT_EQ(0, git_lookup_mailmap(mailmap, &email1, &author1, "email@cs-ware.de", nullptr, [](void*) { return "Sven Strickroth"; }));
	EXPECT_STREQ("sven@tortoisegit.org", email1);
	EXPECT_STREQ("Sven Strickroth", author1);

	git_free_mailmap(mailmap);
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(mailmapFile, L"<sven@tortoisegit.org> <email@cs-ware.de>\nSven S. <sven@tortoisegit.org> Sven Strickroth <email@cs-ware.de>\n<another@example.com> S. Strickroth <email@cs-ware.de>\n"));
	git_read_mailmap(&mailmap);
	EXPECT_NE(nullptr, mailmap);
	email1 = nullptr;
	author1 = nullptr;
	EXPECT_EQ(-1, git_lookup_mailmap(mailmap, &email1, &author1, "sven@tortoisegit.org", nullptr, [](void*) { return "Sven S."; }));
	EXPECT_EQ(-1, git_lookup_mailmap(mailmap, &email1, &author1, "aaa@tortoisegit.org", nullptr, [](void*) { return "Sven S."; }));
	EXPECT_EQ(-1, git_lookup_mailmap(mailmap, &email1, &author1, "zzz@tortoisegit.org", nullptr, [](void*) { return "Sven S."; }));
	EXPECT_EQ(0, git_lookup_mailmap(mailmap, &email1, &author1, "email@cs-ware.de", nullptr, [](void*) { return "Sven S."; }));
	EXPECT_STREQ("sven@tortoisegit.org", email1);
	EXPECT_STREQ(nullptr, author1);
	email1 = nullptr;
	author1 = nullptr;
	EXPECT_EQ(0, git_lookup_mailmap(mailmap, &email1, &author1, "email@cs-ware.de", nullptr, [](void*) { return "Sven Strickroth"; }));
	EXPECT_STREQ("sven@tortoisegit.org", email1);
	EXPECT_STREQ("Sven S.", author1);
	email1 = nullptr;
	author1 = nullptr;
	EXPECT_EQ(0, git_lookup_mailmap(mailmap, &email1, &author1, "email@cs-ware.de", nullptr, [](void*) { return "S. Strickroth"; }));
	EXPECT_STREQ("another@example.com", email1);
	EXPECT_STREQ(nullptr, author1);
}

TEST(libgit, MkDir)
{
	CAutoTempDir tempdir;
	auto subdir = tempdir.GetTempDir() / L"abc";

	EXPECT_FALSE(fs::exists(subdir));
	EXPECT_EQ(0, git_mkdir(CUnicodeUtils::GetUTF8<CStringA>(subdir.wstring())));
	EXPECT_TRUE(fs::exists(subdir));
	EXPECT_TRUE(fs::is_directory(subdir));
	EXPECT_EQ(-1, git_mkdir(CUnicodeUtils::GetUTF8<CStringA>(subdir.wstring())));
}

TEST(libgit, RefreshIndex)
{
	CAutoTempDir tempdir;
	g_Git.SetCurrentDirExact(tempdir.GetTempDir());
	g_Git.m_bInitialized = false;
	g_Git.m_IsGitDllInited = false;
	g_Git.m_IsUseGitDLL = true;
	g_Git.m_IsUseLibGit2 = false;
	g_Git.m_IsUseLibGit2_mask = 0;

	// libgit relies on CWD being set to working tree
	SetCurrentDirectory(g_Git.m_CurrentDir.c_str());

	git_repository_init_options options = GIT_REPOSITORY_INIT_OPTIONS_INIT;
	options.flags = GIT_REPOSITORY_INIT_MKPATH | GIT_REPOSITORY_INIT_EXTERNAL_TEMPLATE;
	wil::unique_git_repository repo;
	ASSERT_EQ(0, git_repository_init_ext(repo.put(), CUnicodeUtils::GetUTF8<CStringA>(tempdir.GetTempDir().wstring()), &options));
	wil::unique_git_config config;
	git_repository_config(config.put(), repo.get());
	ASSERT_TRUE(config.is_valid());
	CStringA path = CUnicodeUtils::GetUTF8<CStringA>(g_Git.m_CurrentDir.wstring());
	path.Replace('\\', '/');
	EXPECT_EQ(0, git_config_set_string(config.get(), "core.autocrlf", "false")); // newer Git complains for adding 1.enc with a safecrlf warning, especially an issue on AppVeyor where AutoCrLf=input is set
	EXPECT_EQ(0, git_config_set_string(config.get(), "filter.openssl.clean", path + "/clean_filter_openssl"));
	EXPECT_EQ(0, git_config_set_string(config.get(), "filter.openssl.smudge", path + "/smudge_filter_openssl"));
	EXPECT_EQ(0, git_config_set_bool(config.get(), "filter.openssl.required", 1));
	CString cleanFilterFilename = (g_Git.m_CurrentDir / L"clean_filter_openssl").c_str();
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(cleanFilterFilename, L"#!/bin/bash\nopenssl version | grep -q \" 1\\\\.0\\\\.\"\nif [[ $? = 0 ]]; then\n\topenssl enc -base64 -aes-256-ecb -S FEEDDEADBEEF -k PASS_FIXED\nelse\n\topenssl enc -base64 -pbkdf2 -aes-256-ecb -nosalt -k PASS_FIXED\nfi\n"));
	CString smudgeFilterFilename = (g_Git.m_CurrentDir / L"smudge_filter_openssl").c_str();
	EXPECT_TRUE(StringUtils::WriteStringToTextFile(smudgeFilterFilename, L"#!/bin/bash\nopenssl version | grep -q \" 1\\\\.0\\\\.\"\nif [[ $? = 0 ]]; then\n\topenssl enc -d -base64 -aes-256-ecb -k PASS_FIXED\nelse\n\topenssl enc -d -base64 -pbkdf2 -aes-256-ecb -nosalt -k PASS_FIXED\nfi\n"));
	EXPECT_EQ(0, git_config_set_string(config.get(), "filter.test.clean", path + "/clean_filter_openssl"));
	EXPECT_EQ(0, git_config_set_string(config.get(), "filter.test.smudge", path + "/smudge_filter_openssl"));
	EXPECT_EQ(0, git_config_set_string(config.get(), "filter.test.process", path + "/clean_filter_openssl"));
	EXPECT_EQ(0, git_config_set_bool(config.get(), "filter.test.required", 1));

	// need to make sure sh.exe is on PATH
	g_Git.CheckMsysGitDir();
	size_t size;
	_wgetenv_s(&size, nullptr, 0, L"PATH");
	ASSERT_LT(0U, size);
	auto oldEnv = std::make_unique<wchar_t[]>(size);
	ASSERT_TRUE(oldEnv);
	_wgetenv_s(&size, oldEnv.get(), size, L"PATH");
	_wputenv_s(L"PATH", GitCall::m_Environment.GetEnv(L"PATH").c_str());
	SCOPE_EXIT { _wputenv_s(L"PATH", oldEnv.get()); };
	EXPECT_TRUE(StringUtils::WriteStringToTextFile((g_Git.m_CurrentDir / L"somefile.txt").c_str(), L"some content"));

	g_Git.RefreshGitIndex();

	{
		std::string output;
		EXPECT_EQ(0, g_Git.Run({L"git.exe", L"add", L"somefile.txt"}, &output));
		EXPECT_STREQ("", output);

		g_Git.RefreshGitIndex();

		EXPECT_TRUE(StringUtils::WriteStringToTextFile((g_Git.m_CurrentDir / L"somefile.txt").c_str(), L"some other content"));

		g_Git.RefreshGitIndex();
	}

	{
		std::string output;
		EXPECT_EQ(0, g_Git.Run({L"git.exe", L"add", L"somefile.txt"}, &output));
		EXPECT_STREQ("", output);

		g_Git.RefreshGitIndex();

		// now check with external command filters defined
		CString attributesFile = (g_Git.m_CurrentDir / L".gitattributes").c_str();
		EXPECT_TRUE(StringUtils::WriteStringToTextFile(attributesFile, L"*.enc filter=openssl\n"));

		CString encryptedFileOne = (g_Git.m_CurrentDir / L"1.enc").c_str();
		EXPECT_TRUE(StringUtils::WriteStringToTextFile(encryptedFileOne, L"This should be encrypted...\nAnd decrypted on the fly\n"));
	}

	{
		std::string output;
		EXPECT_EQ(0, g_Git.Run({L"git.exe", L"add", L"1.enc"}, &output));
		if (!GitCall::ms_bCygwinGit) // on AppVeyor with the VS2017 image we get a warning: "WARNING: can't open config file: /usr/local/ssl/openssl.cnf"
			EXPECT_STREQ("", output);

		WIN32_FILE_ATTRIBUTE_DATA fdata;
		GetFileAttributesEx((g_Git.m_CurrentDir / L".git\\index").c_str(), GetFileExInfoStandard, &fdata);

		g_Git.RefreshGitIndex();

		EXPECT_TRUE(StringUtils::WriteStringToTextFile((g_Git.m_CurrentDir / L"1.enc").c_str(), L"some other content"));

		g_Git.RefreshGitIndex();

		// need racy timestamp
		EXPECT_TRUE(StringUtils::WriteStringToTextFile((g_Git.m_CurrentDir / L"1.enc").c_str(), L"somE other content"));
		{
			CAutoGeneralHandle handle = ::CreateFile((g_Git.m_CurrentDir / L"1.enc").c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, 0, 0, nullptr);
			SetFileTime(handle, &fdata.ftCreationTime, &fdata.ftLastAccessTime, &fdata.ftLastWriteTime);
		}

		g_Git.RefreshGitIndex();

		CString attributesFile = (g_Git.m_CurrentDir / L".gitattributes").c_str();
		// now check with external command filters with multi-filter (process) defined
		EXPECT_TRUE(StringUtils::WriteStringToTextFile(attributesFile, L"*.enc filter=test\n"));

		g_Git.RefreshGitIndex();

		EXPECT_TRUE(StringUtils::WriteStringToTextFile((g_Git.m_CurrentDir / L"1.enc").c_str(), L"somE other conTentsome other conTentsome other conTen"));

		g_Git.RefreshGitIndex();

		// need racy timestamp
		GetFileAttributesEx((g_Git.m_CurrentDir / L".git\\index").c_str(), GetFileExInfoStandard, &fdata);
		EXPECT_TRUE(StringUtils::WriteStringToTextFile((g_Git.m_CurrentDir / L"1.enc").c_str(), L"some other conTentsome other conTentsome other conTen"));
		{
			CAutoGeneralHandle handle = ::CreateFile((g_Git.m_CurrentDir / L"1.enc").c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, 0, 0, nullptr);
			SetFileTime(handle, &fdata.ftCreationTime, &fdata.ftLastAccessTime, &fdata.ftLastWriteTime);
		}

		g_Git.RefreshGitIndex();
	}
}

TEST(libgit, IncludeIf) {
	CAutoTempDir tempdir;
	g_Git.m_bInitialized = false;
	g_Git.m_IsGitDllInited = false;
	g_Git.m_IsUseGitDLL = true;
	g_Git.m_IsUseLibGit2 = false;
	g_Git.m_IsUseLibGit2_mask = 0;

	{
		// .git dir
		auto repoDir = tempdir.GetTempDir() / L"RepoWithAInPath";
		g_Git.SetCurrentDirExact(repoDir);
		EXPECT_TRUE(CreateDirectory(repoDir.c_str(), nullptr));
		// libgit relies on CWD being set to working tree
		EXPECT_TRUE(SetCurrentDirectory(repoDir.c_str()));

		git_repository_init_options options = GIT_REPOSITORY_INIT_OPTIONS_INIT;
		options.flags = GIT_REPOSITORY_INIT_EXTERNAL_TEMPLATE;
		wil::unique_git_repository repo;
		ASSERT_EQ(0, git_repository_init_ext(repo.put(), CUnicodeUtils::GetUTF8<CStringA>(repoDir.wstring()), &options));

		EXPECT_TRUE(StringUtils::WriteStringToTextFile((repoDir / L".git\\config").c_str(), L"[core]\n	repositoryformatversion = 0\n	filemode = false\n	bare = false\n	logallrefupdates = true\n	symlinks = false\n	ignorecase = true\n	hideDotFiles = dotGitOnly\n[something]\n	thevalue = jap\n[includeIf \"gitdir:RepoWithAInPath/**\"]\n	path = configA\n[includeIf \"gitdir:RepoWithBInPath/**\"]\n	path = configB\n"));
		EXPECT_TRUE(StringUtils::WriteStringToTextFile((repoDir / L".git\\configA").c_str(), L"[somethinga]\n	thevalue = jop\n"));
		EXPECT_TRUE(StringUtils::WriteStringToTextFile((repoDir / L".git\\configB").c_str(), L"[somethingb]\n	thevalue = jup\n"));

		EXPECT_STREQ(L"jap", g_Git.GetConfigValue(L"something.thevalue"));
		EXPECT_STREQ(L"jop",g_Git.GetConfigValue(L"somethinga.thevalue"));
		EXPECT_STREQ(L"", g_Git.GetConfigValue(L"somethingb.thevalue"));

		// .git file
		g_Git.m_bInitialized = false;
		g_Git.m_IsGitDllInited = false;
		g_Git.m_IsUseGitDLL = true;
	}

	{
		auto repoDir = tempdir.GetTempDir() / L"RepoWithBInPath";
		g_Git.SetCurrentDirExact(repoDir);
		EXPECT_TRUE(CreateDirectory(repoDir.c_str(), nullptr));
		// libgit relies on CWD being set to working tree
		EXPECT_TRUE(SetCurrentDirectory(repoDir.c_str()));
		EXPECT_TRUE(StringUtils::WriteStringToTextFile((repoDir / L".git").c_str(), L"gitdir: ../RepoWithAInPath/.git\n"));

		EXPECT_STREQ(L"jap", g_Git.GetConfigValue(L"something.thevalue"));
		EXPECT_STREQ(L"jop", g_Git.GetConfigValue(L"somethinga.thevalue"));
		EXPECT_STREQ(L"", g_Git.GetConfigValue(L"somethingb.thevalue"));
	}
}

TEST(libgit, StoreUninitializedRepositoryConfig)
{
	CAutoTempDir tempdir;
	g_Git.SetCurrentDirExact(tempdir.GetTempDir());
	g_Git.m_IsGitDllInited = false;
	g_Git.SetCurrentDirExact(g_Git.m_CurrentDir);
	g_Git.m_IsUseGitDLL = true;
	g_Git.m_IsUseLibGit2 = false;
	g_Git.m_IsUseLibGit2_mask = 0;
	// libgit relies on CWD being set to working tree
	SetCurrentDirectory(g_Git.m_CurrentDir.c_str());

	// clear any leftovers in caches
	EXPECT_THROW(g_Git.CheckAndInitDll(), std::exception);

	EXPECT_THROW(git_set_config("something", "else", CONFIG_LOCAL), std::exception);
}
