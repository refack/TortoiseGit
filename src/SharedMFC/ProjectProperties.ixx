module;
#include <git2/config.h>


export module ProjectProperties;

import std;
import wil;
import git_h;
import str_utils;
import wil.MFC;
import libgit2;
import TGitPath;
import TempFiles;


struct num_compare
{
	bool operator() (const CString& lhs, const CString& rhs) const
	{
		return StrCmpLogicalW(lhs, rhs) < 0;
	}
};


export constexpr auto BUGTRAQPROPNAME_LABEL = L"bugtraq.label"sv;
export constexpr auto BUGTRAQPROPNAME_MESSAGE = L"bugtraq.message"sv;
export constexpr auto BUGTRAQPROPNAME_NUMBER = L"bugtraq.number"sv;
export constexpr auto BUGTRAQPROPNAME_LOGREGEX = L"bugtraq.logregex"sv;
export constexpr auto BUGTRAQPROPNAME_URL = L"bugtraq.url"sv;
export constexpr auto BUGTRAQPROPNAME_WARNIFNOISSUE = L"bugtraq.warnifnoissue"sv;
export constexpr auto BUGTRAQPROPNAME_APPEND = L"bugtraq.append"sv;
export constexpr auto BUGTRAQPROPNAME_PROVIDERUUID = L"bugtraq.provideruuid"sv;
export constexpr auto BUGTRAQPROPNAME_PROVIDERUUID64 = L"bugtraq.provideruuid64"sv;
export constexpr auto BUGTRAQPROPNAME_PROVIDERPARAMS = L"bugtraq.providerparams"sv;

export constexpr auto PROJECTPROPNAME_LOGWIDTHLINE = L"tgit.logwidthmarker"sv;
export constexpr auto PROJECTPROPNAME_LOGMINSIZE = L"tgit.logminsize"sv;
export constexpr auto PROJECTPROPNAME_LOGFILELISTLANG = L"tsvn.logfilelistenglish"sv;
export constexpr auto PROJECTPROPNAME_PROJECTLANGUAGE = L"tgit.projectlanguage"sv;
export constexpr auto PROJECTPROPNAME_WARNNOSIGNEDOFFBY = L"tgit.warnnosignedoffby"sv;
export constexpr auto PROJECTPROPNAME_ICON = L"tgit.icon"sv;

export constexpr auto PROJECTPROPNAME_WEBVIEWER_REV = L"webviewer.revision"sv;
export constexpr auto PROJECTPROPNAME_WEBVIEWER_PATHREV = L"webviewer.pathrevision"sv;

export constexpr auto PROJECTPROPNAME_STARTCOMMITHOOK = L"hook.startcommit."sv;
export constexpr auto PROJECTPROPNAME_PRECOMMITHOOK = L"hook.precommit."sv;
export constexpr auto PROJECTPROPNAME_POSTCOMMITHOOK = L"hook.postcommit."sv;
export constexpr auto PROJECTPROPNAME_PREPUSHHOOK = L"hook.prepush."sv;
export constexpr auto PROJECTPROPNAME_POSTPUSHHOOK = L"hook.postpush."sv;
export constexpr auto PROJECTPROPNAME_PREREBASEHOOK = L"hook.prerebase."sv;

/**
 * \ingroup TortoiseProc
 * Provides methods for retrieving information about bug/issue trackers
 * associated with a git repository/working copy and other project
 * related properties.
 */
export class ProjectProperties
{
public:
	ProjectProperties();

	/**
	 * Reads the properties from the current working tree
	 */
	BOOL ReadProps();

	/**
	 * Searches for the BugID inside a log message. If one is found,
	 * the method returns TRUE. The rich edit control is used to set
	 * the CFE_LINK effect on the BugID's.
	 * \param msg the log message
	 * \param pWnd Pointer to a rich edit control
	 */
#ifdef _RICHEDIT_
	std::vector<CHARRANGE> FindBugIDPositions(const CString& msg);
#endif
	BOOL FindBugID(const CString& msg, const CWnd * pWnd);

	CString FindBugID(const CString& msg);
	std::set<CString> FindBugIDs(const CString& msg);

	/**
	 * Check whether calling \ref FindBugID or \ref FindBugIDPositions
	 * is worthwhile. If the result is @a false, those functions would
	 * return empty strings or sets, respectively.
	 */
	bool MightContainABugID() const;

	/**
	 * Searches for the BugID inside a log message. If one is found,
	 * that BugID is returned. If none is found, an empty string is returned.
	 * The \c msg is trimmed off the BugID.
	 */
	CString GetBugIDFromLog(CString& msg) const;

	/**
	 * Checks if the bug ID is valid. If bugtraq:number is 'true', then the
	 * functions checks if the bug ID doesn't contain any non-number chars in it.
	 */
	BOOL CheckBugID(const CString& sID) const;

	/**
	 * Checks if the log message \c sMessage contains a bug ID. This is done by
	 * using the bugtraq:checkre property.
	 */
	BOOL HasBugID(const CString& sMsg);

	/**
	 * Returns the URL pointing to the Issue in the issue tracker. The URL is
	 * created from the bugtraq:url property and the BugID found in the log message.
	 * \param sBugID the BugID extracted from the log message
	 */
	CString GetBugIDUrl(const CString& sBugID) const;

	/**
	 * Replaces %BUGID% in the provided URL with the sBugID after doing URL encoding
	 */
	static void ReplaceBugIDPlaceholder(CString& url, const CString& sBugID);

	/** replaces bNumer: a regular expression string to check the validity of
	  * the entered bug ID. */
	const CString& GetCheckRe() const {return sCheckRe;}
	void SetCheckRe(const CString& s) {sCheckRe = s;regExNeedUpdate=true;AutoUpdateRegex();}

	/** used to extract the bug ID from the string matched by sCheckRe */
	const CString& GetBugIDRe() const {return sBugIDRe;}
	void SetBugIDRe(const CString& s) {sBugIDRe = s;regExNeedUpdate=true;AutoUpdateRegex();}

#ifdef _WIN64
	const CString& GetProviderUUID() const { return (sProviderUuid64.IsEmpty() ? sProviderUuid : sProviderUuid64); }
#else
	const CString& GetProviderUUID() const { return (sProviderUuid.IsEmpty() ? sProviderUuid64 : sProviderUuid); }
#endif

	/** The label to show in the commit dialog where the issue number/bug id
	 * is entered. Example: "Bug-ID: " or "Issue-No.:". Default is "Bug-ID :" */
	CString		sLabel;

	/** The message string to add below the log message the user entered.
	 * It must contain the string "%BUGID%" which gets replaced by the client
	 * with the issue number / bug id the user entered. */
	CString		sMessage;

	/** If this is set, then the bug-id / issue number must be a number, no text */
	BOOL		bNumber = TRUE;

	/** replaces bNumer: a regular expression string to check the validity of
	  * the entered bug ID. */
	CString		sCheckRe;

	/** used to extract the bug ID from the string matched by sCheckRe */
	CString		sBugIDRe;

	/** The url pointing to the issue tracker. If the url contains the string
	 * "%BUGID% the client has to replace it with the issue number / bug id
	 * the user entered. */
	CString		sUrl;

	/** If set to TRUE, show a warning dialog if the user forgot to enter
	 * an issue number in the commit dialog. */
	BOOL		bWarnIfNoIssue = FALSE;

	/** If set to FALSE, then the bug tracking entry is inserted at the top of the
	   log message instead of at the bottom. Default is TRUE */
	BOOL		bAppend = TRUE;

	/** the parameters passed to the COM bugtraq provider which implements the
	   IBugTraqProvider interface */
	CString		sProviderParams;

	/** The number of chars the width marker should be shown at. If the property
	 * is not set, then this value is 80 by default. */
	int			nLogWidthMarker = 0;

	/** Minimum size a log message must have in chars */
	int			nMinLogSize = 0;

	/** TRUE if the file list to be inserted in the commit dialog should be in
	 * English and not in the localized language. Default is TRUE */
	BOOL		bFileListInEnglish = TRUE;

	/** The language identifier this project uses for log messages. */
	LONG		lProjectLanguage = 0;

	/** The url pointing to the web viewer. The string %REVISION% is replaced
	 *  with the revision number, "HEAD", or a date */
	CString		sWebViewerRev;

	/** The url pointing to the web viewer. The string %REVISION% is replaced
	 *  with the revision number, "HEAD", or a date. The string %PATH% is replaced
	 *  with the path relative to the repository root, e.g. "/trunk/src/file" */
	CString		sWebViewerPathRev;

	BOOL		bWarnNoSignedOffBy = FALSE;
	CString		sIcon;

	/**
	 * A regex string to extract revisions from a log message.
	 */
	CString		sLogRevRegex;

	/** the COM uuid of the bugtraq provider which implements the IBugTraqProvider
	   interface. */
	CString		sProviderUuid;
	CString		sProviderUuid64;

	/// multi line string containing the data for a start-commit-hook
	CString		sStartCommitHook;
	/// multi line string containing the data for a pre-commit-hook
	CString		sPreCommitHook;
	/// multi line string containing the data for a post-commit-hook
	CString		sPostCommitHook;
	/// multi line string containing the data for a pre-push-hook
	CString		sPrePushHook;
	/// multi line string containing the data for a post-push-hook
	CString		sPostPushHook;
	/// multi line string containing the data for a pre-rebase-hook
	CString		sPreRebaseHook;

private:
	/**
	 * Constructing regex objects is expensive. Therefore, cache them here.
	 */
	void AutoUpdateRegex();

	static void FetchHookString(const git_config* gitconfig, const CString& sBase, CString& sHook);

	bool regExNeedUpdate = true;
	std::wregex regCheck;
	std::wregex regBugID;

	int			nBugIdPos = -1;				///< result	of sMessage.Find(L"%BUGID%");

#ifdef GOOGLETEST_INCLUDE_GTEST_GTEST_H_
	FRIEND_TEST(ProjectPropertiesTest, ParseBugIDs);
#endif
};

ProjectProperties::ProjectProperties()
{
}

int ProjectProperties::ReadProps()
{
	wil::unique_git_config gitconfig;
	git_config_new(gitconfig.put());
	const wil::unique_git_repository repo(g_Git.GetGitRepository());
	if (const auto adminDir = GitAdminDir::GetAdminDirPath(GitCall::m_CurrentDirStorage.native())) {
		const auto adminConfig = fs::path{ adminDir->path } / L"config";
		git_config_add_file_ondisk(gitconfig.get(), TGitPath::ToGitPathA(adminConfig.native()).c_str(), GIT_CONFIG_LEVEL_APP, repo.get(), FALSE); // this needs to have the highest priority in order to override .tgitconfig settings
	}

	if (!GitAdminDir::IsBareRepo(GitCall::m_CurrentDirStorage))
		git_config_add_file_ondisk(gitconfig.get(), TGitPath::ToGitPathA(g_Git.CombinePath(L".tgitconfig").native()).c_str(), GIT_CONFIG_LEVEL_LOCAL, nullptr, FALSE); // this needs to have the second highest priority
	else
	{
		const CString tmpFile = TempFiles::GetTempFilePath(true).GetWinPathString().c_str();
		const CTGitPath path(L".tgitconfig");
		if (g_Git.GetOneFile(L"HEAD", path, tmpFile) == 0)
			git_config_add_file_ondisk(gitconfig.get(), CGit::GetGitPathStringA(tmpFile), GIT_CONFIG_LEVEL_LOCAL, nullptr, FALSE); // this needs to have the second highest priority
	}

	git_config_add_file_ondisk(gitconfig.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalConfig().native()).c_str(), GIT_CONFIG_LEVEL_GLOBAL, repo.get(), FALSE);
	git_config_add_file_ondisk(gitconfig.get(), TGitPath::ToGitPathA(TGitH::GetGitGlobalXDGConfig().native()).c_str(), GIT_CONFIG_LEVEL_XDG, repo.get(), FALSE);
	git_config_add_file_ondisk(gitconfig.get(), TGitPath::ToGitPathA(TGitH::GetGitSystemConfig().native()).c_str(), GIT_CONFIG_LEVEL_SYSTEM, repo.get(), FALSE);
	tgit::libgit2::git_error_clear();

	CString sPropVal;

	TGitH::GetConfigString(gitconfig.get(), BUGTRAQPROPNAME_LABEL, sLabel);
	TGitH::GetConfigString(gitconfig.get(), BUGTRAQPROPNAME_MESSAGE, sMessage);
	nBugIdPos = sMessage.Find(L"%BUGID%");
	TGitH::GetConfigString(gitconfig.get(), BUGTRAQPROPNAME_URL, sUrl);

	TGitH::GetConfigBOOL(gitconfig.get(), BUGTRAQPROPNAME_WARNIFNOISSUE, bWarnIfNoIssue);
	TGitH::GetConfigBOOL(gitconfig.get(), BUGTRAQPROPNAME_NUMBER, bNumber);
	TGitH::GetConfigBOOL(gitconfig.get(), BUGTRAQPROPNAME_APPEND, bAppend);

	TGitH::GetConfigString(gitconfig.get(), BUGTRAQPROPNAME_PROVIDERUUID, sProviderUuid);
	TGitH::GetConfigString(gitconfig.get(), BUGTRAQPROPNAME_PROVIDERUUID64, sProviderUuid64);
	TGitH::GetConfigString(gitconfig.get(), BUGTRAQPROPNAME_PROVIDERPARAMS, sProviderParams);

	TGitH::GetConfigBOOL(gitconfig.get(), PROJECTPROPNAME_WARNNOSIGNEDOFFBY, bWarnNoSignedOffBy);
	TGitH::GetConfigString(gitconfig.get(), PROJECTPROPNAME_ICON, sIcon);

	TGitH::GetConfigString(gitconfig.get(), BUGTRAQPROPNAME_LOGREGEX, sPropVal);

	sCheckRe = sPropVal;
	if (sCheckRe.Find('\n')>=0)
	{
		sBugIDRe = sCheckRe.Mid(sCheckRe.Find('\n')).Trim();
		sCheckRe = sCheckRe.Left(sCheckRe.Find('\n')).Trim();
	}
	if (!sCheckRe.IsEmpty())
		sCheckRe = sCheckRe.Trim();

	if (TGitH::GetConfigString(gitconfig.get(), PROJECTPROPNAME_LOGWIDTHLINE, sPropVal) == 0)
	{
		const CString val = sPropVal;
		if (!val.IsEmpty())
			nLogWidthMarker = _wtoi(val);
	}

	if (TGitH::GetConfigString(gitconfig.get(), PROJECTPROPNAME_PROJECTLANGUAGE, sPropVal) == 0)
	{
		const CString val = sPropVal;
		if (val == L"-1")
			lProjectLanguage = -1;
		if (!val.IsEmpty())
		{
			LPWSTR strEnd;
			lProjectLanguage = wcstol(val, &strEnd, 0);
		}
	}

	if (TGitH::GetConfigString(gitconfig.get(), PROJECTPROPNAME_LOGMINSIZE, sPropVal) == 0)
	{
		const CString val = sPropVal;
		if (!val.IsEmpty())
			nMinLogSize = _wtoi(val);
	}

	FetchHookString(gitconfig.get(), PROJECTPROPNAME_STARTCOMMITHOOK.data(), sStartCommitHook);
	FetchHookString(gitconfig.get(), PROJECTPROPNAME_PRECOMMITHOOK.data(), sPreCommitHook);
	FetchHookString(gitconfig.get(), PROJECTPROPNAME_POSTCOMMITHOOK.data(), sPostCommitHook);
	FetchHookString(gitconfig.get(), PROJECTPROPNAME_PREPUSHHOOK.data(), sPrePushHook);
	FetchHookString(gitconfig.get(), PROJECTPROPNAME_POSTPUSHHOOK.data(), sPostPushHook);
	FetchHookString(gitconfig.get(), PROJECTPROPNAME_PREREBASEHOOK.data(), sPreRebaseHook);

	return 0;
}

CString ProjectProperties::GetBugIDFromLog(CString& msg) const {
	CString sBugID;

	if (!sMessage.IsEmpty())
	{
		CString sBugLine;
		BOOL bTop = FALSE;
		if (nBugIdPos < 0)
			return sBugID;
		const CString sFirstPart = sMessage.Left(nBugIdPos);
		const CString sLastPart = sMessage.Mid(nBugIdPos + 7);
		msg.TrimRight('\n');
		if (msg.ReverseFind('\n')>=0)
		{
			if (bAppend)
				sBugLine = msg.Mid(msg.ReverseFind('\n')+1);
			else
			{
				sBugLine = msg.Left(msg.Find('\n'));
				bTop = TRUE;
			}
		}
		else
		{
			if (bNumber)
			{
				// find out if the message consists only of numbers
				bool bOnlyNumbers = true;
				for (int i=0; i<msg.GetLength(); ++i)
				{
					if (!_istdigit(msg[i]))
					{
						bOnlyNumbers = false;
						break;
					}
				}
				if (bOnlyNumbers)
					sBugLine = msg;
			}
			else
				sBugLine = msg;
		}
		if (sBugLine.IsEmpty() && (msg.ReverseFind('\n') < 0))
			sBugLine = msg.Mid(msg.ReverseFind('\n')+1);
		if (sBugLine.Left(sFirstPart.GetLength()).Compare(sFirstPart)!=0)
			sBugLine.Empty();
		if (sBugLine.Right(sLastPart.GetLength()).Compare(sLastPart)!=0)
			sBugLine.Empty();
		if (sBugLine.IsEmpty())
		{
			if (msg.Find('\n')>=0)
				sBugLine = msg.Left(msg.Find('\n'));
			if (sBugLine.Left(sFirstPart.GetLength()).Compare(sFirstPart)!=0)
				sBugLine.Empty();
			if (sBugLine.Right(sLastPart.GetLength()).Compare(sLastPart)!=0)
				sBugLine.Empty();
			bTop = TRUE;
		}
		if (sBugLine.IsEmpty())
			return sBugID;
		sBugID = sBugLine.Mid(sFirstPart.GetLength(), sBugLine.GetLength() - sFirstPart.GetLength() - sLastPart.GetLength());
		if (bTop)
		{
			msg = msg.Mid(sBugLine.GetLength());
			msg.TrimLeft('\n');
		}
		else
		{
			msg = msg.Left(msg.GetLength()-sBugLine.GetLength());
			msg.TrimRight('\n');
		}
	}
	return sBugID;
}

void ProjectProperties::AutoUpdateRegex()
{
	if (regExNeedUpdate)
	{
		try
		{
			regCheck = std::wregex(sCheckRe);
			regBugID = std::wregex(sBugIDRe);
		}
		catch (...)
		{
		}

		regExNeedUpdate = false;
	}
}

void ProjectProperties::FetchHookString(const git_config* gitconfig, const CString& sBase, CString& sHook) {
	sHook.Empty();
	CString sVal;
	TGitH::GetConfigString(gitconfig, sBase + L"cmdline", sVal);
	if (sVal.IsEmpty())
		return;
	sHook += sVal + L'\n';
	bool boolval = false;
	TGitH::GetConfigBool(gitconfig, tgit::str::StringView(sBase + L"wait"), boolval);
	if (boolval)
		sHook += L"true\n";
	else
		sHook += L"false\n";
	boolval = false;
	TGitH::GetConfigBool(gitconfig, tgit::str::StringView(sBase + L"show"), boolval);
	if (boolval)
		sHook += L"show";
	else
		sHook += L"hide";
}

std::vector<CHARRANGE> ProjectProperties::FindBugIDPositions(const CString& msg)
{
	size_t offset1 = 0;
	size_t offset2 = 0;
	std::vector<CHARRANGE> result;

	// first use the checkre string to find bug ID's in the message
	if (!sCheckRe.IsEmpty())
	{
		if (!sBugIDRe.IsEmpty())
		{
			// match with two regex strings (without grouping!)
			try
			{
				AutoUpdateRegex();
				const std::wsregex_iterator end;
				std::wstring s { msg };
				for (std::wsregex_iterator it(s.cbegin(), s.cend(), regCheck); it != end; ++it)
				{
					// (*it)[0] is the matched string
					std::wstring matchedString = (*it)[0];
					ptrdiff_t matchpos = it->position(0);
					for (std::wsregex_iterator it2(matchedString.cbegin(), matchedString.cend(), regBugID); it2 != end; ++it2)
					{
						//ATLTRACE(L"matched id : %s\n", (*it2)[0].str().c_str());
						ptrdiff_t matchposID = it2->position(0);
						CHARRANGE range = { .cpMin = static_cast<LONG>(matchpos + matchposID), .cpMax = static_cast<LONG>(matchpos+matchposID + (*it2)[0].str().size()) };
						result.push_back(range);
					}
				}
			}
			catch (...) {}
		}
		else
		{
			try
			{
				AutoUpdateRegex();
				const std::wsregex_iterator end;
				std::wstring s { msg };
				for (std::wsregex_iterator it(s.cbegin(), s.cend(), regCheck); it != end; ++it)
				{
					const std::wsmatch match = *it;
					// we define group 1 as the whole issue text and
					// group 2 as the bug ID
					if (match.size() >= 2)
					{
						//ATLTRACE(L"matched id : %s\n", std::wstring(match[1]).c_str());
						CHARRANGE range = { .cpMin = static_cast<LONG>(match[1].first - s.cbegin()), .cpMax = static_cast<LONG>(match[1].second - s.cbegin()) };
						result.push_back(range);
					}
				}
			}
			catch (...) {}
		}
	}
	else if (result.empty() && (!sMessage.IsEmpty()))
	{
		CString sBugLine;
		CString sFirstPart;
		CString sLastPart;
		BOOL bTop = FALSE;
		if (nBugIdPos < 0)
			return result;

		sFirstPart = sMessage.Left(nBugIdPos);
		sLastPart = sMessage.Mid(nBugIdPos + 7);
		CString sMsg = msg;
		sMsg.TrimRight('\n');
		if (sMsg.ReverseFind('\n')>=0)
		{
			if (bAppend)
				sBugLine = sMsg.Mid(sMsg.ReverseFind('\n')+1);
			else
			{
				sBugLine = sMsg.Left(sMsg.Find('\n'));
				bTop = TRUE;
			}
		}
		else
			sBugLine = sMsg;
		if (sBugLine.Left(sFirstPart.GetLength()).Compare(sFirstPart)!=0)
			sBugLine.Empty();
		if (sBugLine.Right(sLastPart.GetLength()).Compare(sLastPart)!=0)
			sBugLine.Empty();
		if (sBugLine.IsEmpty())
		{
			if (sMsg.Find('\n')>=0)
				sBugLine = sMsg.Left(sMsg.Find('\n'));
			if (sBugLine.Left(sFirstPart.GetLength()).Compare(sFirstPart)!=0)
				sBugLine.Empty();
			if (sBugLine.Right(sLastPart.GetLength()).Compare(sLastPart)!=0)
				sBugLine.Empty();
			bTop = TRUE;
		}
		if (sBugLine.IsEmpty())
			return result;

		CString sBugIDPart = sBugLine.Mid(sFirstPart.GetLength(), sBugLine.GetLength() - sFirstPart.GetLength() - sLastPart.GetLength());
		if (sBugIDPart.IsEmpty())
			return result;

		//the bug id part can contain several bug id's, separated by commas
		if (!bTop)
			offset1 = sMsg.GetLength() - sBugLine.GetLength() + sFirstPart.GetLength();
		else
			offset1 = sFirstPart.GetLength();
		sBugIDPart.Trim(L',');
		while (sBugIDPart.Find(',')>=0)
		{
			offset2 = offset1 + sBugIDPart.Find(',');
			CHARRANGE range = { .cpMin = static_cast<LONG>(offset1), .cpMax = static_cast<LONG>(offset2) };
			result.push_back(range);
			sBugIDPart = sBugIDPart.Mid(sBugIDPart.Find(',')+1);
			offset1 = offset2 + 1;
		}
		offset2 = offset1 + sBugIDPart.GetLength();
		CHARRANGE range = { .cpMin = static_cast<LONG>(offset1), .cpMax = static_cast<LONG>(offset2) };
		result.push_back(range);
	}

	return result;
}

BOOL ProjectProperties::FindBugID(const CString& msg, const CWnd* pWnd)
{
	const std::vector<CHARRANGE> positions = FindBugIDPositions(msg);
	wil::MFC::SetCharFormat(pWnd, CFM_LINK, CFE_LINK, positions);

	return positions.empty() ? FALSE : TRUE;
}

std::set<CString> ProjectProperties::FindBugIDs (const CString& msg)
{
	const std::vector<CHARRANGE> positions = FindBugIDPositions(msg);
	std::set<CString> bugIDs;

	for (const auto& [cpMin, cpMax] : positions)
		bugIDs.insert(msg.Mid(cpMin, cpMax - cpMin));

	return bugIDs;
}

CString ProjectProperties::FindBugID(const CString& msg)
{
	CString sRet;
	if (!sCheckRe.IsEmpty() || (nBugIdPos >= 0))
	{
		const std::vector<CHARRANGE> positions = FindBugIDPositions(msg);
		std::set<CString, num_compare> bugIDs;
		for (const auto& [cpMin, cpMax] : positions)
			bugIDs.insert(msg.Mid(cpMin, cpMax - cpMin));

		for (const auto& id : bugIDs)
		{
			sRet += id;
			sRet += L' ';
		}
		sRet.Trim();
	}

	return sRet;
}

bool ProjectProperties::MightContainABugID() const {
	return !sCheckRe.IsEmpty() || (nBugIdPos >= 0);
}

CString ProjectProperties::GetBugIDUrl(const CString& sBugID) const {
	CString ret = sUrl;
	if (sUrl.IsEmpty())
		return ret;
	if (!sMessage.IsEmpty() || !sCheckRe.IsEmpty())
		ReplaceBugIDPlaceholder(ret, sBugID);
	return ret;
}

void ProjectProperties::ReplaceBugIDPlaceholder(CString& url, const CString& sBugID)
{
	CString parameter;
	DWORD size = INTERNET_MAX_URL_LENGTH;
	std::ignore = UrlEscape(sBugID, CStrBuf(parameter, size + 1), &size, URL_ESCAPE_SEGMENT_ONLY | URL_ESCAPE_PERCENT | URL_ESCAPE_AS_UTF8 | URL_ESCAPE_ASCII_URI_COMPONENT);
	url.Replace(L"%BUGID%", parameter);
}

BOOL ProjectProperties::CheckBugID(const CString& sID) const {
	if (bNumber)
	{
		// check if the revision actually _is_ a number
		// or a list of numbers separated by colons
		const int len = sID.GetLength();
		for (int i=0; i<len; ++i)
		{
			const wchar_t c = sID.GetAt(i);
			if ((c < '0')&&(c != ',')&&(c != ' '))
				return FALSE;
			if (c > '9')
				return FALSE;
		}
	}
	return TRUE;
}

BOOL ProjectProperties::HasBugID(const CString& sMsg)
{
	if (!sCheckRe.IsEmpty())
	{
		try
		{
			AutoUpdateRegex();
			return std::regex_search(static_cast<LPCWSTR>(sMsg), regCheck);
		}
		catch (...) {}
	}
	return FALSE;
}
