module;
#include "stdafx.h"
#include <tgitlib/sanity.h>	// SCOPE_EXIT

#include "Resources/resource.h"

#include "ProtoModules/GitDiff.h"
#include "ProtoModules/IgnoreDlg.h"
#include "ProtoModules/DeleteConflictDlg.h"
#include "ProtoModules/SubmoduleResolveConflictDlg.h"


export module AppUtils.WorkingTree;
import tgit;
import TortoiseProc;
import AppUtils.Launch;
import TGitMessageBox;
import Registry;

/**
 * \ingroup TortoiseProc
 * Operations on single working-tree entries that a file list offers from its context
 * menu: adding paths to an ignore file, and resolving a conflicted path (the conflict
 * titles, the external merge tool, or the delete/modify and submodule dialogs).
 * Split out of AppUtils.Workflow so GitStatusListCtrl can import them: Workflow's
 * dialogs (CommitDlg, RebaseDlg, ...) import GitStatusListCtrl, so it cannot import
 * Workflow back. Nothing here may depend on GitStatusListCtrl or AppUtils.Workflow.
 */
export namespace CAppUtils {

class CIgnoreFile : public CStdioFile
{
public:
	std::vector<std::wstring> m_Items;
	std::string m_eol{ "\n" };

	BOOL ReadString(CString& rString) override
	{
		if (GetPosition() == 0)
		{
			constexpr unsigned char utf8bom[] = { 0xEF, 0xBB, 0xBF };
			char buf[3] = { 0, 0, 0 };
			Read(buf, 3);
			if (memcpy(buf, utf8bom, sizeof(utf8bom)))
				SeekToBegin();
		}

		CStringA strA;
		char lastChar = '\0';
		for (char c = '\0'; Read(&c, 1) == 1; lastChar = c)
		{
			if (c == '\r')
				continue;
			if (c == '\n')
			{
				m_eol = lastChar == L'\r' ? "\r\n" : "\n";
				break;
			}
			strA.AppendChar(c);
		}
		if (strA.IsEmpty())
			return FALSE;

		rString = CUnicodeUtils::GetUnicode<CString>(strA);
		return TRUE;
	}

	void ResetState()
	{
		m_Items.clear();
		m_eol = { "\n" };
	}
};

bool OpenIgnoreFile(const HWND hWnd, CIgnoreFile &file, const CString& filename)
{
	file.ResetState();
	if (!file.Open(filename, CFile::modeCreate | CFile::modeReadWrite | CFile::modeNoTruncate | CFile::typeBinary))
	{
		MessageBox(hWnd, filename + L" Open Failure", L"TortoiseGit", MB_OK | MB_ICONERROR);
		return false;
	}

	if (file.GetLength() > 0)
	{
		CString fileText;
		while (file.ReadString(fileText))
			file.m_Items.push_back(std::wstring(fileText));
		file.Seek(gsl::narrow<long long>(file.GetLength()) - 1, 0);
		char lastchar[1] = {};
		file.Read(lastchar, 1);
		file.SeekToEnd();
		if (lastchar[0] != '\n')
			file.Write(file.m_eol.c_str(), static_cast<UINT>(file.m_eol.size()));
	}
	else
		file.SeekToEnd();

	return true;
}

bool IgnoreFile(const HWND hWnd, const CTGitPathList& path, const bool IsMask)
{
	CIgnoreDlg ignoreDlg(GetParentCWnd(hWnd));
	if (ignoreDlg.DoModal() == IDOK)
	{
		CString ignorefile = (GitCall::m_CurrentDirStorage / L"").c_str();

		switch (ignoreDlg.m_IgnoreFile)
		{
		case 0:
			ignorefile += L".gitignore";
			break;

		case 2:
			if (const auto adminDir = GitAdminDir::GetAdminDirPath(GitCall::m_CurrentDirStorage.native()))
				ignorefile = adminDir->path.c_str();
			ignorefile += L"info";
			if (!PathFileExists(ignorefile))
				CreateDirectory(ignorefile, nullptr);
			ignorefile += L"\\exclude";
			break;

		default:
			std::unreachable();
		}

		CIgnoreFile file;
		try
		{
			if (ignoreDlg.m_IgnoreFile != 1 && !OpenIgnoreFile(hWnd, file, ignorefile))
				return false;

			for (int i = 0; i < path.GetCount(); ++i)
			{
				if (ignoreDlg.m_IgnoreFile == 1)
				{
					ignorefile = (g_Git.CombinePath(path[i].GetContainingDirectory()) / L".gitignore").c_str();
					if (!OpenIgnoreFile(hWnd, file, ignorefile))
						return false;
				}

				CString ignorePattern;
				if (ignoreDlg.m_IgnoreType == 0)
				{
					if (ignoreDlg.m_IgnoreFile != 1 && !path[i].GetContainingDirectory().GetGitPathString().empty())
						ignorePattern += std::format(L"/{}", path[i].GetContainingDirectory().GetGitPathString()).c_str();

					ignorePattern += L'/';
				}
				if (IsMask)
				{
					if (path[i].GetFileExtension().empty())
						continue;
					ignorePattern += std::format(L"*{}", path[i].GetFileExtension()).c_str();
				}
				else
					ignorePattern += path[i].GetFileOrDirectoryName().c_str();

				// escape [ and ] so that files get ignored correctly
				ignorePattern.Replace(L"[", L"\\[");
				ignorePattern.Replace(L"]", L"\\]");

				bool found = false;
				for (size_t j = 0; j < file.m_Items.size(); ++j)
				{
					if (file.m_Items[j] == tgit::str::StringView(ignorePattern))
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					file.m_Items.push_back(std::wstring(ignorePattern));
					auto ignorePatternA = CUnicodeUtils::GetUTF8<CStringA>(ignorePattern);
					ignorePatternA += file.m_eol.c_str();
					file.Write(ignorePatternA, ignorePatternA.GetLength());
				}

				if (ignoreDlg.m_IgnoreFile == 1)
					file.Close();
			}

			if (ignoreDlg.m_IgnoreFile != 1)
				file.Close();
		}
		catch(...)
		{
			file.Abort();
			return false;
		}

		return true;
	}
	return false;
}

void DescribeConflictFile(const bool mode, const bool base,CString &descript)
{
	if(mode == FALSE)
	{
		descript.LoadString(IDS_SVNACTION_DELETE);
		return;
	}
	if(base)
	{
		descript.LoadString(IDS_SVNACTION_MODIFIED);
		return;
	}
	descript.LoadString(IDS_PROC_CREATED);
}

void GetConflictTitles(CString* baseText, CString& mineText, CString& theirsText, const bool rebaseActive)
{
	if (baseText)
		baseText->LoadString(IDS_PROC_DIFF_BASE);
	static bool guessBranch = (CRegDWORD(L"Software\\TortoiseGit\\ConflictDontGuessBranchNames", FALSE) == FALSE);
	if (rebaseActive)
	{
		CString adminDir;
		if (const auto adminDirMaybe = GitAdminDir::GetAdminDirPath(GitCall::m_CurrentDirStorage.native()))
			adminDir = adminDirMaybe->path.c_str();
		mineText = L"Branch being rebased onto";
		if (!StringUtils::ReadStringFromTextFile(adminDir + L"tgitrebase.active\\onto", mineText))
		{
			if (CGitHash hash; guessBranch && !g_Git.GetHash(hash, L"rebase-apply/onto"))
			{
				if (g_Git.GuessRefForHash(mineText, hash) == 0)
					mineText.AppendFormat(L", %s", (hash.ToString(TGitH::CGit::GetShortHASHLength())).c_str());
			}
		}
		theirsText = L"Branch being rebased";
		if (!StringUtils::ReadStringFromTextFile(adminDir + L"tgitrebase.active\\head-name", theirsText))
		{
			if (StringUtils::ReadStringFromTextFile(adminDir + L"rebase-apply/head-name", theirsText))
				theirsText = TGitH::StripRefName(theirsText);
		}
		return;
	}

	constexpr struct {
		const wchar_t*	headref;
		bool			guessRef;
		UINT			theirstext;
	} infotexts[] = {
		{ .headref = L"MERGE_HEAD", .guessRef = true, .theirstext = IDS_CONFLICT_INFOTEXT },
		{ .headref = L"CHERRY_PICK_HEAD", .guessRef = false, .theirstext = IDS_CONFLICT_INFOTEXT },
		{ .headref = L"REVERT_HEAD", .guessRef = false, .theirstext = IDS_CONFLICT_REVERT },
	};
	mineText = L"HEAD";
	theirsText.LoadString(IDS_CONFLICT_REFTOBEMERGED);
	for (const auto& [headref, guessRef, theirstext] : infotexts) {
		CGitHash hash;
		if (!g_Git.GetHash(hash, headref))
		{
			CString guessedRef;
			if (!guessBranch || !guessRef)
				guessedRef = (hash.ToString(TGitH::CGit::GetShortHASHLength())).c_str();
			else
			{
				if (g_Git.GuessRefForHash(guessedRef, hash) == 0)
					guessedRef.AppendFormat(L", %s", (hash.ToString(TGitH::CGit::GetShortHASHLength())).c_str());
			}
			theirsText.FormatMessage(theirstext, headref, guessedRef);
			break;
		}
	}
}

bool ConflictEdit(HWND hWnd, CTGitPath& path, bool bAlternativeTool = false, bool isRebase = false, HWND resolveMsgHwnd = nullptr)
{
	CTGitPath merge = path;

	const std::wstring mergeGitPathString = merge.GetGitPathString();

	std::string vector;
	if (g_Git.Run({ L"git.exe", L"ls-files", L"-u", L"-t", L"-z", L"--", mergeGitPathString }, &vector))
		return FALSE;

	CString baseTitle, mineTitle, theirsTitle;
	CGitHash mineHash, theirsHash;
	GetConflictTitles(&baseTitle, mineTitle, theirsTitle, isRebase);

	CGitHash baseHash, localHash, remoteHash;
	bool baseIsFile = true, localIsFile = true, remoteIsFile = true;
	if (TGitH::ParseConflictHashesFromLsFile(vector, baseHash, baseIsFile, localHash, localIsFile, remoteHash, remoteIsFile))
		return FALSE;

	if (!baseIsFile || !localIsFile || !remoteIsFile)
	{
		CTGitPath fullMergePath;
		fullMergePath.SetFromWin(g_Git.CombinePath(merge));
		if (fullMergePath.IsWCRoot())
		{
			CGit subgit;
			subgit.m_IsUseGitDLL = false;
			subgit.SetCurrentDirExact(fullMergePath.GetWinPathString().c_str());
			subgit.GetHash(baseHash, L"HEAD");
		}

		auto changeTypeMine = CGitDiff::ChangeType::Unknown;
		auto changeTypeTheirs = CGitDiff::ChangeType::Unknown;

		bool baseOK = false, mineOK = false, theirsOK = false;
		CString baseSubject, mineSubject, theirsSubject;
		if (fullMergePath.IsWCRoot())
		{
			CGit subgit;
			subgit.m_IsUseGitDLL = false;
			subgit.SetCurrentDirExact(fullMergePath.GetWinPathString().c_str());
			CGitDiff::GetSubmoduleChangeType(subgit, baseHash, localHash, baseOK, mineOK, changeTypeMine, baseSubject, mineSubject);
			CGitDiff::GetSubmoduleChangeType(subgit, baseHash, remoteHash, baseOK, theirsOK, changeTypeTheirs, baseSubject, theirsSubject);
		}
		else if (baseHash.IsEmpty() && localHash.IsEmpty() && !remoteHash.IsEmpty()) // merge conflict with no submodule, but submodule in merged revision (not initialized)
		{
			changeTypeMine = CGitDiff::ChangeType::Identical;
			changeTypeTheirs = CGitDiff::ChangeType::NewSubmodule;
			baseSubject.LoadString(IDS_CONFLICT_NOSUBMODULE);
			mineSubject = baseSubject;
			theirsSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
		}
		else if (baseHash.IsEmpty() && !localHash.IsEmpty() && remoteHash.IsEmpty()) // merge conflict with no submodule initialized, but submodule exists in base and folder with no submodule is merged
		{
			baseHash = localHash;
			baseSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
			mineSubject = baseSubject;
			theirsSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
			changeTypeMine = CGitDiff::ChangeType::Identical;
			changeTypeTheirs = CGitDiff::ChangeType::DeleteSubmodule;
		}
		else if (!baseHash.IsEmpty() && !localHash.IsEmpty() && !remoteHash.IsEmpty()) // base has submodule, mine has submodule and theirs also, but not initialized
		{
			baseSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
			mineSubject = baseSubject;
			theirsSubject = baseSubject;
			if (baseHash == localHash)
				changeTypeMine = CGitDiff::ChangeType::Identical;
		}
		else if (baseHash.IsEmpty() && !localHash.IsEmpty() && !remoteHash.IsEmpty())
		{
			baseOK = true;
			mineSubject = baseSubject;
			if (remoteIsFile)
			{
				theirsSubject.LoadString(IDS_CONFLICT_NOTASUBMODULE);
				changeTypeMine = CGitDiff::ChangeType::NewSubmodule;
			}
			else
				theirsSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
			if (localIsFile)
			{
				mineSubject.LoadString(IDS_CONFLICT_NOTASUBMODULE);
				changeTypeTheirs = CGitDiff::ChangeType::NewSubmodule;
			}
			else
				mineSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
		}
		else if (!baseHash.IsEmpty() && (localHash.IsEmpty() || remoteHash.IsEmpty()))
		{
			baseSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
			if (localHash.IsEmpty())
			{
				mineSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
				changeTypeMine = CGitDiff::ChangeType::DeleteSubmodule;
			}
			else
			{
				mineSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
				if (localHash == baseHash)
					changeTypeMine = CGitDiff::ChangeType::Identical;
			}
			if (remoteHash.IsEmpty())
			{
				theirsSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
				changeTypeTheirs = CGitDiff::ChangeType::DeleteSubmodule;
			}
			else
			{
				theirsSubject.LoadString(IDS_CONFLICT_SUBMODULENOTINITIALIZED);
				if (remoteHash == baseHash)
					changeTypeTheirs = CGitDiff::ChangeType::Identical;
			}
		}
		else
			return FALSE;

		CSubmoduleResolveConflictDlg resolveSubmoduleConflictDialog(GetParentCWnd(hWnd));
		resolveSubmoduleConflictDialog.SetDiff(merge.GetGitPathString().c_str(), isRebase, baseTitle, mineTitle, theirsTitle, baseHash, baseSubject, baseOK, localHash, mineSubject, mineOK, changeTypeMine, remoteHash, theirsSubject, theirsOK, changeTypeTheirs);
		resolveSubmoduleConflictDialog.DoModal();
		if (resolveSubmoduleConflictDialog.m_bResolved && resolveMsgHwnd)
		{
			static UINT WM_REVERTMSG = RegisterWindowMessage(L"GITSLNM_NEEDSREFRESH");
			::PostMessage(resolveMsgHwnd, WM_REVERTMSG, NULL, NULL);
		}

		return TRUE;
	}

	CTGitPath theirs;
	CTGitPath mine;
	CTGitPath base;

	const auto [mine_name, ther_name] = isRebase ? std::pair{L"REMOTE", L"LOCAL"} : std::pair{L"LOCAL", L"REMOTE"};
	mine.SetFromGit(GetMergeTempFile(mine_name, merge).GetString());
	theirs.SetFromGit(GetMergeTempFile(ther_name, merge).GetString());
	base.SetFromGit(GetMergeTempFile(L"BASE",merge).GetString());

	CFile tempfile;
	//create a empty file, incase stage is not three
	tempfile.Open(mine.GetWinPathString().c_str(),CFile::modeCreate|CFile::modeReadWrite);
	tempfile.Close();
	tempfile.Open(theirs.GetWinPathString().c_str(),CFile::modeCreate|CFile::modeReadWrite);
	tempfile.Close();
	tempfile.Open(base.GetWinPathString().c_str(),CFile::modeCreate|CFile::modeReadWrite);
	tempfile.Close();

	auto prepareFile = [&hWnd, &mergeGitPathString](int stage, const CString& outfile) {
		CString output, err;
		if (!GitCall::Run({ L"git.exe", L"checkout-index", L"--temp", std::format(L"--stage={}", stage), L"--", mergeGitPathString }, &output, &err, CP_UTF8))
		{
			int start = 0;
			const CString file = output.Tokenize(L"\t", start);
			::MoveFileEx(file, outfile, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
		}
		else
			CMessageBox::Show(hWnd, output + L'\n' + err, L"TortoiseGit", MB_OK | MB_ICONERROR);
	};
	if (!baseHash.IsEmpty())
		prepareFile(1, base.GetWinPathString().c_str());
	if (!localHash.IsEmpty())
		prepareFile(2, mine.GetWinPathString().c_str());
	if (!remoteHash.IsEmpty())
		prepareFile(3, theirs.GetWinPathString().c_str());

	if (!localHash.IsEmpty() && !remoteHash.IsEmpty())
	{
		merge.SetFromWin(g_Git.CombinePath(merge));
		if (isRebase)
			return !!StartExtMerge(bAlternativeTool, base, mine, theirs, merge, baseTitle, mineTitle, theirsTitle, CString(), resolveMsgHwnd, true);

		return !!StartExtMerge(bAlternativeTool, base, theirs, mine, merge, baseTitle, theirsTitle, mineTitle, CString(), resolveMsgHwnd, true);
	}
	else
	{
		::DeleteFile(mine.GetWinPathString().c_str());
		::DeleteFile(theirs.GetWinPathString().c_str());
		if (baseHash.IsEmpty())
			::DeleteFile(base.GetWinPathString().c_str());

		SCOPE_EXIT{
			if (!baseHash.IsEmpty())
				::DeleteFile(base.GetWinPathString().c_str());
		};

		CDeleteConflictDlg dlg(GetParentCWnd(hWnd));
		if (!isRebase)
		{
			DescribeConflictFile(!localHash.IsEmpty(), !baseHash.IsEmpty(), dlg.m_LocalStatus);
			DescribeConflictFile(!remoteHash.IsEmpty(), !baseHash.IsEmpty(), dlg.m_RemoteStatus);
			dlg.m_LocalHash = mineHash;
			dlg.m_RemoteHash = theirsHash;
			dlg.m_LocalRef = mineTitle;
			dlg.m_RemoteRef = theirsTitle;
			dlg.m_bDiffMine = !localHash.IsEmpty();
		}
		else
		{
			DescribeConflictFile(!localHash.IsEmpty(), !baseHash.IsEmpty(), dlg.m_RemoteStatus);
			DescribeConflictFile(!remoteHash.IsEmpty(), !baseHash.IsEmpty(), dlg.m_LocalStatus);
			dlg.m_LocalHash = theirsHash;
			dlg.m_RemoteHash = mineHash;
			dlg.m_LocalRef = theirsTitle;
			dlg.m_RemoteRef = mineTitle;
			dlg.m_bDiffMine = localHash.IsEmpty();
		}
		dlg.m_bShowModifiedButton = !baseHash.IsEmpty();
		dlg.m_File = merge;
		dlg.m_FileBaseVersion = base;
		if(dlg.DoModal() == IDOK)
		{
			CString out;
			if (GitCall::Run({ L"git.exe", dlg.m_bIsDelete ? L"rm" : L"add", L"--", mergeGitPathString }, &out, CP_UTF8))
			{
				MessageBox(hWnd, out, L"TortoiseGit", MB_OK | MB_ICONERROR);
				return FALSE;
			}
			if (!dlg.m_bIsDelete)
			{
				path.m_Action |= CTGitPath::LOGACTIONS_ADDED;
				path.m_Action &= ~CTGitPath::LOGACTIONS_UNMERGED;
			}
			return TRUE;
		}
		return FALSE;
	}
}

} // export namespace CAppUtils
