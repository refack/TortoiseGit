// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2011-2013 - Sven Strickroth <email@cs-ware.de>
// Copyright (C) 2013-2014, 2016-2017, 2023, 2026 - TortoiseGit

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

#pragma once
#include <cassert>
#include <format>

#include "gittype.h"
#include "TGitPath.h"
#include "Git.h"
#include "GitProgressList.h"


template <typename... T>
bool startsWithOrIsParam(const std::wstring& parameters, const std::wstring& supportedParameter, T... tail) {
	return startsWithOrIsParam(parameters, supportedParameter) || startsWithOrIsParam(parameters, tail...);
}

template <>
inline bool startsWithOrIsParam(const std::wstring& parameters, const std::wstring& supportedParameter) {
	return parameters == supportedParameter || parameters.starts_with(supportedParameter + L' ');
}


class CMassiveGitTaskBase {
	bool m_bUnused = true;
	bool m_bIsPath = TRUE;
	bool m_bIgnoreErrors = false;
	std::wstring m_sParams;
	CTGitPathList m_pathList;
	STRING_VECTOR m_itemList;

public:
	CMassiveGitTaskBase(CString gitParameters, const bool isPath, const bool ignoreErrors)
		: m_bIsPath(isPath), m_bIgnoreErrors(ignoreErrors), m_sParams(std::move(gitParameters)) {
	}

	CMassiveGitTaskBase(const CMassiveGitTaskBase&) = default;
	CMassiveGitTaskBase(CMassiveGitTaskBase&&) = default;
	CMassiveGitTaskBase& operator=(const CMassiveGitTaskBase&) = default;
	CMassiveGitTaskBase& operator=(CMassiveGitTaskBase&&) = default;
	virtual ~CMassiveGitTaskBase() = default;

	void AddFile(const CString& filename) {
		assert(m_bUnused);
		if (m_bIsPath)
			m_pathList.AddPath(CTGitPath(filename.GetString()));
		else
			m_itemList.push_back(std::wstring(filename));
	}

	void AddFile(const CTGitPath& filename) {
		assert(m_bUnused);
		if (m_bIsPath)
			m_pathList.AddPath(filename);
		else
			m_itemList.push_back(filename.GetGitPathString());
	}

	bool Execute(const bool cancel) {
		assert(m_bUnused);
		m_pathList.RemoveDuplicates();
		return ExecuteCommands(cancel);
	}

	[[nodiscard]] int GetListCount() const { return m_bIsPath ? m_pathList.GetCount() : static_cast<int>(m_itemList.size()); }
	[[nodiscard]] bool IsListEmpty() const { return m_bIsPath ? m_pathList.IsEmpty() : m_itemList.empty(); }
	/**
	 * Splits \a pathList over as many copies of \a baseArgv as it takes to stay
	 * under the command line length limit. \a baseArgv must already end with the
	 * "--" separator; the paths are appended as further elements.
	 */
	static void ConvertToCmdList(const STRING_VECTOR& baseArgv, const STRING_VECTOR& pathList, ARGV_VECTOR& cmdList);

protected:
	void SetPaths(const CTGitPathList* pathList) {
		assert(m_bUnused);
		m_bIsPath = true;
		m_pathList.Clear();
		m_pathList = *pathList;
	}

	bool ExecuteCommands(bool cancel);

	virtual void ReportError(const CString& out, int exitCode) {
		const auto msg = std::format(L"{} [{}]", out, exitCode);
		MessageBox(nullptr, msg.c_str(), L"TortoiseGit", MB_OK | MB_ICONERROR);
	}

	virtual void ReportProgress(const CTGitPath& /*path*/, int /*index*/) {
	}

	virtual void ReportUserCanceled() {
	}

	[[nodiscard]] CString GetParams() const { return m_sParams.c_str(); }

private:
	[[nodiscard]] CString GetListItem(const int index) const { return m_bIsPath ? CString(m_pathList[index].GetGitPathString().c_str()) : CString(m_itemList[index].c_str()); }
};

inline bool CMassiveGitTaskBase::ExecuteCommands(const bool cancel) {
	m_bUnused = false;

	if (IsListEmpty())
		return true;

	if (m_bIsPath && startsWithOrIsParam(m_sParams, L"add", L"rm", L"reset", L"checkout", L"restore", L"stash")) {
		CString tempFilename = GetTempFile();
		if (tempFilename.IsEmpty()) {
			ReportError(L"Error creating temp file", -1);
			return false;
		}
		SCOPE_EXIT{ ::DeleteFile(tempFilename); };

		if (!m_pathList.WriteToPathSpecFile(tempFilename.GetString())) {
			ReportError(L"Error writing to temp file", -1);
			return false;
		}

		std::wstring pathSpecFileParam = std::format(L" --pathspec-from-file=%s --pathspec-file-nul", CGit::QuoteParameter(tempFilename));
		const auto endOfParamsPosition = m_sParams.find(L" --end-of-options", 0);
		const std::wstring params =
			endOfParamsPosition == std::wstring::npos ?
			m_sParams + pathSpecFileParam : 
			tgit::wstr::Mid(m_sParams, 0ul, endOfParamsPosition) + pathSpecFileParam + tgit::wstr::Mid(m_sParams, gsl::narrow<int>(endOfParamsPosition));
		CString cmd, out;
		cmd.Format(L"git.exe %s", params.c_str());
		const int exitCode = g_Git.Run(cmd, &out, CP_UTF8);
		if (exitCode && !m_bIgnoreErrors) {
			ReportError(out, exitCode);
			return false;
		}

		for (int i = 0; i < GetListCount(); ++i)
			ReportProgress(m_pathList[i], i);

		if (cancel) {
			ReportUserCanceled();
			return false;
		}
		return true;
	}

	int max_command_line_length = 30000;
	int quotes_length = 2;
	if (CGit::ms_bCygwinGit || CGit::ms_bMsys2Git) // see issue https://tortoisegit.org/issue/3542
	{
		max_command_line_length = 3500;
		quotes_length = 4;
	}

	int maxLength = 0;
	int firstCombine = 0;
	for (int i = 0; i < GetListCount(); ++i) {
		if (maxLength + GetListItem(i).GetLength() > max_command_line_length || i == GetListCount() - 1 || cancel) {
			auto add = L"";
			try {
				for (int j = firstCombine; j <= i; ++j) {
					add += L' ';
					add = add + CGit::QuoteParameter(GetListItem(j));
				}
			} catch (illegal_git_parameter& e) {
				ReportError(e.cause(), -1);
				return false;
			}

			CString cmd, out;
			auto sep = m_bIsPath ? L"--" : L"";
			cmd.Format(L"git.exe %s %s%s", m_sParams.c_str(), sep, add);
			int exitCode = g_Git.Run(cmd, &out, CP_UTF8);
			if (exitCode && !m_bIgnoreErrors) {
				ReportError(out, exitCode);
				return false;
			}

			if (m_bIsPath) {
				for (int j = firstCombine; j <= i; ++j)
					ReportProgress(m_pathList[j], j);
			}

			maxLength = 0;
			firstCombine = i + 1;

			if (cancel) {
				ReportUserCanceled();
				return false;
			}
		} else
			maxLength += 1 + quotes_length + GetListItem(i).GetLength();
	}
	return true;
}

inline void CMassiveGitTaskBase::ConvertToCmdList(const STRING_VECTOR& baseArgv, const STRING_VECTOR& pathList, ARGV_VECTOR& cmdList) {
	if (pathList.empty())
		return;

	// see issue https://tortoisegit.org/issue/3542
	const size_t max_command_line_length{ (CGit::ms_bCygwinGit || CGit::ms_bMsys2Git) ? 3500u : 30000u };
	const size_t quotes_length{ (CGit::ms_bCygwinGit || CGit::ms_bMsys2Git) ? 4u : 2u };

	// The limit is on the command line CreateProcess is handed, which is still a
	// flat string however the arguments are spelled here, so the accounting is
	// unchanged - element lengths plus a separator and worst-case quoting.
	size_t baseLength = 0;
	for (const auto& arg : baseArgv)
		baseLength += 1 + quotes_length + arg.size();

	size_t currentLength = 0;
	for (const auto& filename : pathList) {
		// add new command if no command yet or last command will exceed max length
		if (cmdList.empty() || currentLength + 1 + quotes_length + filename.size() > max_command_line_length) {
			cmdList.push_back(baseArgv);
			currentLength = baseLength;
		}

		// update last commmand of list
		cmdList.back().push_back(filename);
		currentLength += 1 + quotes_length + filename.size();
	}
}



class CMassiveGitTask : public CMassiveGitTaskBase
{
public:
	CMassiveGitTask(const CString& params, BOOL isPath = TRUE, bool ignoreErrors = false);
	~CMassiveGitTask() override;

	void SetProgressList(CGitProgressList* notifyCallbackInstance) { m_NotifyCallbackInstance = notifyCallbackInstance; }
	void SetProgressCallback(std::function<void(const CTGitPath& path, int index)> progressCallback) { m_progressCallback = progressCallback; }
	bool					ExecuteWithNotify(CTGitPathList* pathList, volatile BOOL& cancel, CGitProgressList::WC_File_NotificationData::Git_WC_Notify_Action action, CGitProgressList* instance);

protected:
	void					ReportError(const CString& out, int exitCode) override;
	void					ReportProgress(const CTGitPath& path, int index) override;
	void					ReportUserCanceled() override;

private:
	CGitProgressList*		m_NotifyCallbackInstance = nullptr;
	CGitProgressList::WC_File_NotificationData::Git_WC_Notify_Action m_NotifyCallbackAction;
	std::function<void(const CTGitPath& path, int index)> m_progressCallback = nullptr;
};
