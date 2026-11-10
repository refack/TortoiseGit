module;
#include <afxtaskdialog.h>
#include <WinCrypt.h>
#include "Resources/resource.h"
#include <tgitlib/sanity.h>


export module tgit.Hooks;

import std;
import tgit;
import TGitPath;
import libgit2;
import Registry;
import RIAA;
import TempFile;
import ProjectProperties;

namespace fs = std::filesystem;


export namespace tgit::Hooks {

using namespace tgit;
using namespace libgit2;



enum class HookType : std::uint8_t {
	unknown_hook,
	start_commit_hook,
	pre_commit_hook,
	post_commit_hook,
	issue_tracker_hook,
	pre_push_hook,
	post_push_hook,
	pre_rebase_hook,
};



/**
 * \ingroup TortoiseProc
 * helper class, used as the key to the std::map we store the data for the client hook scripts in.
 */
struct hookkey {
	HookType htype = HookType::unknown_hook;
	CTGitPath path;
	bool local{};


	bool operator <(const hookkey& hk) const {
		if (htype == hk.htype) {
			if (local != hk.local)
				return local < hk.local;

			return (path < hk.path);
		} else {
			return htype < hk.htype;
		}
	}
};



/**
 * \ingroup TortoiseProc
 * helper struct, used as the value to the std::map we
 * store the data for the client hook scripts in.
 */
struct hookcmd {
	CString commandline;
	bool bWait = true;
	bool bShow = true;
	bool bEnabled = false;
	bool bLocal = false;
	bool bApproved = false; ///< user explicitly approved
	bool bStored = false; ///< use decision is stored in reg
	CString sRegKey;
};



using hookmap_t = std::map<hookkey, hookcmd>;
using hookiterator = std::map<hookkey, hookcmd>::iterator;
using const_hookiterator = std::map<hookkey, hookcmd>::const_iterator;


auto TheHookMap = hookmap_t{};
auto m_RootPath = CTGitPath{};



namespace CHooks {

	void AddPathParam(CString& sCmd, const CTGitPathList& pathList);
	void AddCWDParam(CString& sCmd, const CString& workingTree);
	void AddErrorParam(CString& sCmd, const CString& error);
	void AddParam(CString& sCmd, const CString& param);
	CTGitPath AddMessageFileParam(CString& sCmd, const CString& message);

	/// Saves the hook script information to the registry.
	bool Save();
	/**
	 * Removes the hook script identified by \c key. To make the change persistent
	 * call Save().
	 */
	bool Remove(const hookkey& key);
	/**
	 * Adds a new hook script. To make the change persistent, call Save().
	 */
	void Add(HookType ht,
	         const CTGitPath& Path,
	         LPCWSTR szCmd,
	         bool bWait,
	         bool bShow,
	         bool bEnabled,
	         bool bLocal);

	/**
	* Toggles the hook script identified by \c key. Returns whether the status has changed.
	* To make the change persistent call Save().
	*/
	bool SetEnabled(const hookkey& key, bool bEnabled);

	/// returns the string representation of the hook type.
	CString GetHookTypeString(HookType t);
	/// returns the hooktype from a string representation of the same.
	HookType GetHookType(const CString& s);

	/// Add hook script data from project properties
	void SetProjectProperties(const CTGitPath& Path, const ProjectProperties& pp);

	/**
	 * Executes the Start-Commit-Hook that first matches the path in
	 * \c workingTree.
	 * \param hWnd the Window
	 * \param workingTree working tree root directory
	 * \param pathList a list of paths to look for the hook scripts
	 * \param message a commit message
	 * \param exitcode on return, contains the exit code of the hook script
	 * \param error the data the hook script outputs to stderr
	 * \remark the string "%PATHS% in the command line of the hook script is
	 * replaced with the path to a temporary file which contains a list of files
	 * in \c pathList, separated by newlines. The hook script can parse this
	 * file to get all the paths the commit is about to be done on.
	 * The string %MESSAGEFILE% is replaced with path to temporary file containing
	 * \c message. If the script finishes successfully, contents of this file
	 * is read back into \c message parameter.
	 */
	bool StartCommit(
		const HWND& hWnd,
		const CString& workingTree,
		const CTGitPathList& pathList,
		CString& message,
		DWORD& exitcode,
		CString& error
	);
	/**
	 * Executes the Pre-Commit-Hook that first matches the path in
	 * \c workingTree.
	 * \param hWnd the Window
	 * \param workingTree working tree root directory
	 * \param pathList a list of paths to look for the hook scripts
	 * \param message the commit message
	 * \param exitcode on return, contains the exit code of the hook script
	 * \param error the data the hook script outputs to stderr
	 * \remark the string "%PATHS% in the command line of the hook script is
	 * replaced with the path to a temporary file which contains a list of files
	 * in \c pathList, separated by newlines. The hook script can parse this
	 * file to get all the paths the update is about to be done on.
	 * If the script finishes successfully, contents of this file is read back
	 * into \c message parameter.
	 */
	bool PreCommit(HWND hWnd,
	               const CString& workingTree,
	               const CTGitPathList& pathList,
	               CString& message,
	               DWORD& exitcode,
	               CString& error);
	/**
	 * Executes the Post-Commit-Hook that first matches the path in
	 * \c workingTree.
	 * \param hWnd the Window
	 * \param workingTree working tree root directory
	 * \param amend commit was amend
	 * \param exitcode out param
	 * \param error error out param
	 */
	bool PostCommit(HWND hWnd, const CString& workingTree, bool amend, DWORD& exitcode, CString& error);

	bool PrePush(HWND hWnd, const CString& workingTree, DWORD& exitcode, CString& error);
	bool PostPush(HWND hWnd, const CString& workingTree, DWORD& exitcode, CString& error);

	bool PreRebase(HWND hWnd, const CString& workingTree, const CString& upstream, const CString& rebasedBranch, DWORD& exitcode, CString& error);

	bool IsHookPresent(HookType t, const CString& workingTree);

	/**
	 * Starts a new process, specified in \c cmd.
	 * \param cmd the command
	 * \param currentDir the current directory
	 * \param error the data the process writes to stderr
	 * \param bWait if true, then this method waits until the created process has finished. If false, then the return
	 * value will always be 0 and \c error will be an empty string.
	 * \param bShow set to true if the process should be started visible.
	 * \return the exit code of the process if \c bWait is true, zero otherwise.
	 */
	DWORD RunScript(CString cmd, LPCWSTR currentDir, CString& error, bool bWait, bool bShow);
	/**
	 * Find the hook script information for the hook type \c t which matches the
	 * path in \c workingTree.
	 */
	hookiterator FindItem(HookType t, const CString& workingTree);

	void ParseHookString(CString strhooks, bool bLocal);

	/**
	 * Checks whether the hook script has been validated already and
	 * if not, asks the user to validate it.
	 */
	bool ApproveHook(HWND hWnd, hookiterator it, DWORD& exitcode);


	bool Create() {
		const auto strhooks = CString{ CRegString(L"Software\\TortoiseGit\\hooks") };
		ParseHookString(strhooks, false);
		return true;
	}
}



CString CalcSHA256(const CString& text) {
	HCRYPTPROV hProv = 0;
	if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
		return L"";
	SCOPE_EXIT { CryptReleaseContext(hProv, 0); };

	HCRYPTHASH hHash = 0;
	if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
		return L"";
	SCOPE_EXIT { CryptDestroyHash(hHash); };

	const CStringA textA = CUnicodeUtils::GetUTF8<CStringA>(text);
	if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(static_cast<LPCSTR>(textA)), textA.GetLength(), 0))
		return L"";

	CString hash;
	auto rgbHash = std::array<BYTE, 32>{};
	DWORD cbHash = win32_cast(rgbHash.size());
	if (!CryptGetHashParam(hHash, HP_HASHVAL, rgbHash.data(), &cbHash, 0))
		return L"";

	for (DWORD i = 0; i < cbHash; i++) {
		const BYTE hi = rgbHash[i] >> 4U;
		const BYTE lo = rgbHash[i] & 0xfU;
		hash.AppendChar(hi + (hi > 9 ? 87 : 48));
		hash.AppendChar(lo + (lo > 9 ? 87 : 48));
	}

	return hash;
}


bool CHooks::Save() {
	CString strhooks;
	for (const auto& [key, command] : TheHookMap) {
		if (command.bLocal)
			continue;
		strhooks += GetHookTypeString(key.htype);
		strhooks += L'\n';
		if (!command.bEnabled)
			strhooks += L'!';
		strhooks += key.path.GetWinPathString().c_str();
		strhooks += L'\n';
		strhooks += command.commandline;
		strhooks += L'\n';
		strhooks += (command.bWait ? L"true" : L"false");
		strhooks += L'\n';
		strhooks += (command.bShow ? L"show" : L"hide");
		strhooks += L'\n';
	}
	CRegString reghooks(L"Software\\TortoiseGit\\hooks");
	reghooks = strhooks;
	if (reghooks.GetLastError())
		return false;

	if (GitCall::m_CurrentDirStorage.empty() || GitAdminDir::IsBareRepo(GitCall::m_CurrentDirStorage))
		return true;

	// load the .tgitconfig file
	wil::unique_git_config gitconfig;
	git_config_new(gitconfig.put());
	git_config_add_file_ondisk(gitconfig.get(), (GitCall::m_CurrentDirStorage / L".tgitconfig"), git_config_level_t::GIT_CONFIG_LEVEL_LOCAL, nullptr, FALSE);
	// this needs to have the second highest priority

	// delete all existing local hooks
	for (const auto& hook : { PROJECTPROPNAME_STARTCOMMITHOOK, PROJECTPROPNAME_PRECOMMITHOOK, PROJECTPROPNAME_POSTCOMMITHOOK, PROJECTPROPNAME_PREPUSHHOOK,
	                          PROJECTPROPNAME_POSTPUSHHOOK, PROJECTPROPNAME_PREREBASEHOOK }) {
		CStringA hookA = CUnicodeUtils::GetUTF8(hook);
		for (const auto& val : { "cmdline", "wait", "show" }) {
			git_config_delete_entry(gitconfig.get(), hookA + val);
		}
	}

	// now save the local hooks to .tgitconfig
	for (auto& [fst, snd] : TheHookMap) {
		if (!snd.bLocal)
			continue;

		CStringA sHookPropName;
		switch (fst.htype) {
		case HookType::start_commit_hook:
			sHookPropName = CUnicodeUtils::GetUTF8(PROJECTPROPNAME_STARTCOMMITHOOK);
			break;
		case HookType::pre_commit_hook:
			sHookPropName = CUnicodeUtils::GetUTF8<CStringA>(PROJECTPROPNAME_PRECOMMITHOOK);
			break;
		case HookType::post_commit_hook:
			sHookPropName = CUnicodeUtils::GetUTF8<CStringA>(PROJECTPROPNAME_POSTCOMMITHOOK);
			break;
		case HookType::pre_push_hook:
			sHookPropName = CUnicodeUtils::GetUTF8<CStringA>(PROJECTPROPNAME_PREPUSHHOOK);
			break;
		case HookType::post_push_hook:
			sHookPropName = CUnicodeUtils::GetUTF8<CStringA>(PROJECTPROPNAME_POSTPUSHHOOK);
			break;
		case HookType::pre_rebase_hook:
			sHookPropName = CUnicodeUtils::GetUTF8<CStringA>(PROJECTPROPNAME_PREREBASEHOOK);
			break;
		default:
			std::unreachable();
		}
		git_config_set_string(gitconfig.get(), sHookPropName + "cmdline", CUnicodeUtils::GetUTF8<CStringA>(snd.commandline));
		git_config_set_bool(gitconfig.get(), sHookPropName + "wait", snd.bWait);
		git_config_set_bool(gitconfig.get(), sHookPropName + "show", snd.bShow);

		CRegDWORD reg(snd.sRegKey);
		if (!snd.bEnabled) {
			reg = 0;
			snd.bApproved = false;
			snd.bStored = true;
		} else if (reg.exists() && reg == 0) {
			reg.removeValue();
			snd.bApproved = false;
			snd.bStored = false;
		}
	}

	return true;
}


bool CHooks::Remove(const hookkey& key) {
	return (TheHookMap.erase(key) > 0);
}


void CHooks::Add(const HookType ht, const CTGitPath& Path, const LPCWSTR szCmd, const bool bWait, const bool bShow, const bool bEnabled, const bool bLocal) {
	hookkey key;
	key.htype = ht;
	key.local = bLocal;
	key.path = Path;
	const auto it = TheHookMap.find(key);
	if (it != TheHookMap.end())
		TheHookMap.erase(it);

	hookcmd cmd;
	cmd.commandline = szCmd;
	cmd.bWait = bWait;
	cmd.bShow = bShow;
	cmd.bEnabled = bEnabled;
	cmd.bLocal = bLocal;
	TheHookMap.insert(std::pair{key, cmd});
}


bool CHooks::SetEnabled(const hookkey& k, const bool bEnabled) {
	const auto it = TheHookMap.find(k);
	if (it == TheHookMap.end())
		return false;
	if (it->second.bEnabled == bEnabled)
		return false;
	it->second.bEnabled = bEnabled;
	return true;
}


CString CHooks::GetHookTypeString(const HookType t) {
	switch (t) {
	case HookType::start_commit_hook:
		return L"start_commit_hook";
	case HookType::pre_commit_hook:
		return L"pre_commit_hook";
	case HookType::post_commit_hook:
		return L"post_commit_hook";
	case HookType::pre_push_hook:
		return L"pre_push_hook";
	case HookType::post_push_hook:
		return L"post_push_hook";
	case HookType::pre_rebase_hook:
		return L"pre_rebase_hook";
	default:
		return L"";
	}
}


HookType CHooks::GetHookType(const CString& s) {
	if (s.Compare(L"start_commit_hook") == 0)
		return HookType::start_commit_hook;
	if (s.Compare(L"pre_commit_hook") == 0)
		return HookType::pre_commit_hook;
	if (s.Compare(L"post_commit_hook") == 0)
		return HookType::post_commit_hook;
	if (s.Compare(L"pre_push_hook") == 0)
		return HookType::pre_push_hook;
	if (s.Compare(L"post_push_hook") == 0)
		return HookType::post_push_hook;
	if (s.Compare(L"pre_rebase_hook") == 0)
		return HookType::pre_rebase_hook;

	return HookType::unknown_hook;
}


void CHooks::SetProjectProperties(const CTGitPath& Path, const ProjectProperties& pp) {
	m_RootPath = Path;

	CString sHookString;
	if (!pp.sPreCommitHook.IsEmpty())
		sHookString += GetHookTypeString(HookType::pre_commit_hook) + L"\n?\n" + pp.sPreCommitHook + L"\n";
	if (!pp.sStartCommitHook.IsEmpty())
		sHookString += GetHookTypeString(HookType::start_commit_hook) + L"\n?\n" + pp.sStartCommitHook + L"\n";
	if (!pp.sPostCommitHook.IsEmpty())
		sHookString += GetHookTypeString(HookType::post_commit_hook) + L"\n?\n" + pp.sPostCommitHook + L"\n";
	if (!pp.sPrePushHook.IsEmpty())
		sHookString += GetHookTypeString(HookType::pre_push_hook) + L"\n?\n" + pp.sPrePushHook + L"\n";
	if (!pp.sPostPushHook.IsEmpty())
		sHookString += GetHookTypeString(HookType::post_push_hook) + L"\n?\n" + pp.sPostPushHook + L"\n";
	if (!pp.sPreRebaseHook.IsEmpty())
		sHookString += GetHookTypeString(HookType::pre_rebase_hook) + L"\n?\n" + pp.sPreRebaseHook + L"\n";
	ParseHookString(sHookString, true);
}


void CHooks::AddParam(CString& sCmd, const CString& param) {
	sCmd += L" \"";
	sCmd += param;
	sCmd += L'"';
}


void CHooks::AddPathParam(CString& sCmd, const CTGitPathList& pathList) {
	const CTGitPath temppath = TempFiles::GetTempFilePath(true);
	std::ignore = pathList.WriteToFile(temppath.GetWinPathString().c_str(), true);
	AddParam(sCmd, temppath.GetWinPathString().c_str());
}


void CHooks::AddCWDParam(CString& sCmd, const CString& workingTree) {
	AddParam(sCmd, workingTree);
}


void CHooks::AddErrorParam(CString& sCmd, const CString& error) {
	const CTGitPath tempPath = TempFiles::GetTempFilePath(true);
	StringUtils::WriteStringToTextFile(tempPath.GetWinPath(), error);
	AddParam(sCmd, tempPath.GetWinPathString().c_str());
}


CTGitPath CHooks::AddMessageFileParam(CString& sCmd, const CString& message) {
	CTGitPath tempPath = TempFiles::GetTempFilePath(true);
	StringUtils::WriteStringToTextFile(tempPath.GetWinPath(), message);
	AddParam(sCmd, tempPath.GetWinPathString().c_str());
	return tempPath;
}


bool CHooks::StartCommit(const HWND& hWnd, const CString& workingTree, const CTGitPathList& pathList, CString& message, DWORD& exitcode, CString& error) {
	const auto it = FindItem(HookType::start_commit_hook, workingTree);
	if (it == TheHookMap.end())
		return false;
	if (!ApproveHook(hWnd, it, exitcode)) {
		error.LoadString(IDS_USERCANCELLED);
		return true;
	}
	CString sCmd = it->second.commandline;
	sCmd.Replace(L"%root%", m_RootPath.GetWinPathString().c_str());
	AddPathParam(sCmd, pathList);
	const CTGitPath temppath = AddMessageFileParam(sCmd, message);
	AddCWDParam(sCmd, workingTree);
	exitcode = RunScript(sCmd, workingTree, error, it->second.bWait, it->second.bShow);
	if (!exitcode && !temppath.IsEmpty()) {
		StringUtils::ReadStringFromTextFile(temppath.GetWinPathString().c_str(), message);
	}
	return true;
}


bool CHooks::PreCommit(const HWND hWnd, const CString& workingTree, const CTGitPathList& pathList, CString& message, DWORD& exitcode, CString& error) {
	const auto it = FindItem(HookType::pre_commit_hook, workingTree);
	if (it == TheHookMap.end())
		return false;
	if (!ApproveHook(hWnd, it, exitcode)) {
		error.LoadString(IDS_USERCANCELLED);
		return true;
	}
	CString sCmd = it->second.commandline;
	sCmd.Replace(L"%root%", m_RootPath.GetWinPathString().c_str());
	AddPathParam(sCmd, pathList);
	const CTGitPath temppath = AddMessageFileParam(sCmd, message);
	AddCWDParam(sCmd, workingTree);
	exitcode = RunScript(sCmd, workingTree, error, it->second.bWait, it->second.bShow);
	if (!exitcode && !temppath.IsEmpty())
		StringUtils::ReadStringFromTextFile(temppath.GetWinPathString().c_str(), message);
	return true;
}


bool CHooks::PostCommit(const HWND hWnd, const CString& workingTree, const bool amend, DWORD& exitcode, CString& error) {
	const auto it = FindItem(HookType::post_commit_hook, workingTree);
	if (it == TheHookMap.end())
		return false;
	if (!ApproveHook(hWnd, it, exitcode)) {
		error.LoadString(IDS_USERCANCELLED);
		return true;
	}
	CString sCmd = it->second.commandline;
	sCmd.Replace(L"%root%", m_RootPath.GetWinPathString().c_str());
	AddCWDParam(sCmd, workingTree);
	if (amend)
		AddParam(sCmd, L"true");
	else
		AddParam(sCmd, L"false");
	exitcode = RunScript(sCmd, workingTree, error, it->second.bWait, it->second.bShow);
	return true;
}


bool CHooks::PrePush(const HWND hWnd, const CString& workingTree, DWORD& exitcode, CString& error) {
	const auto it = FindItem(HookType::pre_push_hook, workingTree);
	if (it == TheHookMap.end())
		return false;
	if (!ApproveHook(hWnd, it, exitcode)) {
		error.LoadString(IDS_USERCANCELLED);
		return true;
	}
	CString sCmd = it->second.commandline;
	sCmd.Replace(L"%root%", m_RootPath.GetWinPathString().c_str());
	AddErrorParam(sCmd, error);
	AddCWDParam(sCmd, workingTree);
	exitcode = RunScript(sCmd, workingTree, error, it->second.bWait, it->second.bShow);
	return true;
}


bool CHooks::PostPush(const HWND hWnd, const CString& workingTree, DWORD& exitcode, CString& error) {
	const auto it = FindItem(HookType::post_push_hook, workingTree);
	if (it == TheHookMap.end())
		return false;
	if (!ApproveHook(hWnd, it, exitcode)) {
		error.LoadString(IDS_USERCANCELLED);
		return true;
	}
	CString sCmd = it->second.commandline;
	sCmd.Replace(L"%root%", m_RootPath.GetWinPathString().c_str());
	AddErrorParam(sCmd, error);
	AddCWDParam(sCmd, workingTree);
	exitcode = RunScript(sCmd, workingTree, error, it->second.bWait, it->second.bShow);
	return true;
}


bool CHooks::PreRebase(const HWND hWnd, const CString& workingTree, const CString& upstream, const CString& rebasedBranch, DWORD& exitcode, CString& error) {
	const auto it = FindItem(HookType::pre_rebase_hook, workingTree);
	if (it == TheHookMap.end())
		return false;
	if (!ApproveHook(hWnd, it, exitcode)) {
		error.LoadString(IDS_USERCANCELLED);
		return true;
	}
	CString sCmd = it->second.commandline;
	sCmd.Replace(L"%root%", m_RootPath.GetWinPathString().c_str());
	AddParam(sCmd, upstream);
	AddParam(sCmd, rebasedBranch);
	AddErrorParam(sCmd, error);
	AddCWDParam(sCmd, workingTree);
	exitcode = RunScript(sCmd, workingTree, error, it->second.bWait, it->second.bShow);
	return true;
}


bool CHooks::IsHookPresent(const HookType t, const CString& workingTree) {
	const auto it = FindItem(t, workingTree);
	return it != TheHookMap.end();
}


hookiterator CHooks::FindItem(const HookType t, const CString& workingTree) {
	hookkey key;
	CTGitPath path{ workingTree.GetString() };
	bool local = false;
	do {
		key.htype = t;
		key.path = path;
		key.local = local;
		auto it = TheHookMap.find(key);
		if (it != TheHookMap.end() && (it->second.bEnabled || it->second.bLocal))
			return it;
		if (!local) {
			local = true;
			continue;
		} else
			local = false;
		path = path.GetContainingDirectory();
	}
	while (!path.IsEmpty());

	/* if this ever gets called with something different than the workingTree root,
	 * recheck whether it is necessary to add a check for "key.path = m_RootPath"
	 * (e.g., for sparse checkouts or other hook types).
	 */
	ATLASSERT(CTGitPath(workingTree.GetString()).IsWCRoot());

	// look for a script with a path as '*'
	key.htype = t;
	key.path = CTGitPath(L"*");
	key.local = false;
	auto it = TheHookMap.find(key);
	if (it != TheHookMap.end() && it->second.bEnabled) {
		return it;
	}

	return TheHookMap.end();
}


void CHooks::ParseHookString(CString strhooks, const bool bLocal) {
	// now fill the map with all the hooks defined in the string
	// the string consists of multiple lines, where one hook script is defined
	// as four lines:
	// line 1: the hook type
	// line 2: path to working copy where to apply the hook script,
	//         if it starts with "!" this hook is disabled (this should provide backward and forward compatibility)
	//         if it starts with "?" this hook is for the current project folder
	// line 3: command line to execute
	// line 4: 'true' or 'false' for waiting for the script to finish
	// line 5: 'show' or 'hide' on how to start the hook script
	hookkey key;
	hookcmd cmd;
	cmd.bLocal = bLocal;
	cmd.bApproved = !bLocal; // user configured scripts are pre-approved
	cmd.bStored = false;

	int pos;
	while ((pos = strhooks.Find(L'\n')) >= 0) {
		// line 1
		key.htype = GetHookType(strhooks.Left(pos));
		if (pos + 1 < strhooks.GetLength())
			strhooks = strhooks.Mid(pos + 1);
		else
			strhooks.Empty();
		bool bComplete = false;
		if ((pos = strhooks.Find(L'\n')) >= 0) {
			// line 2
			cmd.bEnabled = true;
			if (strhooks[0] == L'!' && pos > 1) {
				cmd.bEnabled = false;
				strhooks = strhooks.Mid(static_cast<int>(wcslen(L"!")));
				--pos;
			}
			if (strhooks[0] == L'?' && pos > 0)
				key.path = CTGitPath(m_RootPath);
			else
				key.path = CTGitPath(strhooks.Left(pos).GetString());
			if (pos + 1 < strhooks.GetLength())
				strhooks = strhooks.Mid(pos + 1);
			else
				strhooks.Empty();
			if ((pos = strhooks.Find(L'\n')) >= 0) {
				// line 3
				cmd.commandline = strhooks.Left(pos);
				if (pos + 1 < strhooks.GetLength())
					strhooks = strhooks.Mid(pos + 1);
				else
					strhooks.Empty();
				if ((pos = strhooks.Find(L'\n')) >= 0) {
					// line 4
					cmd.bWait = (strhooks.Left(pos).CompareNoCase(L"true") == 0);
					if (pos + 1 < strhooks.GetLength())
						strhooks = strhooks.Mid(pos + 1);
					else
						strhooks.Empty();
					if ((pos = strhooks.Find(L'\n')) >= 0) {
						// line 5
						cmd.bShow = (strhooks.Left(pos).CompareNoCase(L"show") == 0);
						if (pos + 1 < strhooks.GetLength())
							strhooks = strhooks.Mid(pos + 1);
						else
							strhooks.Empty();
						key.local = cmd.bLocal;
						if (cmd.bLocal) {
							CString temp;
							temp.Format(L"%s|%d%s", m_RootPath.GetWinPath(), key.htype, cmd.commandline);

							cmd.sRegKey = L"Software\\TortoiseGit\\approvedhooks\\" + CalcSHA256(temp);
							CRegDWORD reg(cmd.sRegKey, 0);
							cmd.bApproved = (static_cast<DWORD>(reg) != 0);
							cmd.bStored = reg.exists();
							cmd.bEnabled = cmd.bStored ? cmd.bApproved : true;
						}

						bComplete = true;
					}
				}
			}
		}
		if (bComplete)
			TheHookMap.insert(std::pair{key, cmd});
	}
}


DWORD CHooks::RunScript(CString cmd, const LPCWSTR currentDir, CString& error, const bool bWait, const bool bShow) {
	DWORD exitcode = 0;
	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	// clear the error string
	error.Empty();

	// Create Temp File for redirection
	const auto szTempPath = GitCall::GetTortoiseGitTempPath();
	if (!szTempPath)
		return til::FailCode;

	// setup redirection handles
	// output handle must be WRITE mode, share READ
	// redirect handle must be READ mode, share WRITE
	const auto hErr = wil::unique_handle{ CreateFile(szTempPath->c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr) };

	if (!hErr) {
		error = (wil::CFormatMessageWrapper()).c_str();
		return til::FailCode;
	}

	const auto hRedir = wil::unique_handle{ CreateFile(szTempPath->c_str(), GENERIC_READ, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr) };

	if (!hRedir) {
		error = (wil::CFormatMessageWrapper()).c_str();
		return til::FailCode;
	}

	const auto szOutput = GitCall::GetTortoiseGitTempPath();
	const auto hOut = wil::unique_handle{ CreateFile(szOutput->c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr) };
	if (!hOut) {
		error = (wil::CFormatMessageWrapper()).c_str();
		return til::FailCode;
	}

	// setup startup info, set std out/err handles
	// hide window
	STARTUPINFO si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.hStdOutput = hOut.get();
	si.hStdError = hErr.get();
	si.wShowWindow = bShow ? SW_SHOW : SW_HIDE;

	PROCESS_INFORMATION pi = {};
	if (!CreateProcess(nullptr, cmd.GetBuffer(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT, nullptr, currentDir, &si, &pi)) {
		const DWORD err = GetLastError(); // preserve the CreateProcess error
		error = (wil::CFormatMessageWrapper(err)).c_str();
		SetLastError(err);
		cmd.ReleaseBuffer();
		return til::FailCode;
	}
	cmd.ReleaseBuffer();

	CloseHandle(pi.hThread);

	// wait for process to finish, capture redirection and
	// send it to the parent window/console
	if (bWait) {
		auto buf = std::array<char, 256>{};
		DWORD dw;
		do {
			while (ReadFile(hRedir.get(), &buf, win32_cast(buf.size() - 1), &dw, nullptr)) {
				if (dw == 0)
					break;
				error += CString(CStringA(buf.data(), dw));
			}
			Sleep(150);
		}
		while (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0);

		// perform any final flushing
		while (ReadFile(hRedir.get(), &buf, sizeof(buf) - 1, &dw, nullptr)) {
			if (dw == 0)
				break;

			error += CString(CStringA(buf.data(), dw));
		}
		WaitForSingleObject(pi.hProcess, INFINITE);
		GetExitCodeProcess(pi.hProcess, &exitcode);
	}
	CloseHandle(pi.hProcess);
	fs::remove(*szOutput);
	fs::remove(*szTempPath);

	return exitcode;
}


bool CHooks::ApproveHook(const HWND hWnd, hookiterator it, DWORD& exitcode) {
	if (it->second.bApproved || it->second.bStored) {
		exitcode = 0;
		return it->second.bApproved;
	}

	CString sQuestion;
	sQuestion.Format(IDS_HOOKS_APPROVE_TASK1, it->second.commandline);
	CTaskDialog taskdlg(sQuestion,
	                    CString(MAKEINTRESOURCE(IDS_HOOKS_APPROVE_TASK2)),
	                    L"TortoiseGit",
	                    0,
	                    TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT);
	taskdlg.AddCommandControl(101, CString(MAKEINTRESOURCE(IDS_HOOKS_APPROVE_TASK3)));
	taskdlg.AddCommandControl(102, CString(MAKEINTRESOURCE(IDS_HOOKS_APPROVE_TASK4)));
	taskdlg.SetCommonButtons(TDCBF_CANCEL_BUTTON);
	taskdlg.SetVerificationCheckboxText(CString(MAKEINTRESOURCE(IDS_HOOKS_APPROVE_TASK5)));
	taskdlg.SetVerificationCheckbox(false);
	taskdlg.SetDefaultCommandControl(102);
	taskdlg.SetMainIcon(TD_WARNING_ICON);
	taskdlg.SetFooterText(CString(MAKEINTRESOURCE(IDS_HOOKS_APPROVE_SECURITYHINT)));
	const auto ret = taskdlg.DoModal(hWnd);
	if (ret == IDCANCEL) {
		exitcode = 1;
		return false;
	}
	const bool bApproved = (ret == 101);
	const bool bDoNotAskAgain = !!taskdlg.GetVerificationCheckboxState();

	if (bDoNotAskAgain) {
		CRegDWORD reg(it->second.sRegKey, 0);
		reg = bApproved ? 1 : 0;
		it->second.bStored = true;
		it->second.bApproved = bApproved;
	}
	exitcode = 0;
	return bApproved;
}

}
export using namespace tgit::Hooks;
