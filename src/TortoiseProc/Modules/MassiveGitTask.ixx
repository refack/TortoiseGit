module;
#include "stdafx.h"
#include "std.h"

#include "ProgressDlg.h"
#include "GitProgressList.h"


export module TortoiseProc.MassiveGitTask;
import std;
import TGitPath;
import TempFile;
import git_h;


/// "git.exe " + \a params as argv. Callers build their parameter string with
/// CGit::QuoteParameter, the quoting CommandLineToArgvW reads back, so the split
/// is exact. git.exe goes in front because argv[0] is parsed by other rules, and
/// an empty line would come back as this process's own path. Empty on failure.
inline std::vector<std::wstring> GitArgv(const std::wstring_view params) {
	const std::wstring line = std::format(L"git.exe {}", params);
	int argc = 0;
	const std::unique_ptr<LPWSTR, decltype(&::LocalFree)> argv{ ::CommandLineToArgvW(line.c_str(), &argc), &::LocalFree };
	if (!argv)
		return {};
	return { argv.get(), argv.get() + argc };
}


template <typename... T>
bool startsWithOrIsParam(const std::wstring& parameters, const std::wstring& supportedParameter, T... tail) {
	return startsWithOrIsParam(parameters, supportedParameter) || startsWithOrIsParam(parameters, tail...);
}

template <>
inline bool startsWithOrIsParam(const std::wstring& parameters, const std::wstring& supportedParameter) {
	return parameters == supportedParameter || parameters.starts_with(supportedParameter + L' ');
}


export namespace MassiveGitTask {

class CMassiveGitTaskBase {
	bool m_bUnused = true;
	bool m_bIsPath = TRUE;
	bool m_bIgnoreErrors = false;
	std::wstring m_sParams;
	CTGitPathList m_pathList;
	std::vector<std::wstring> m_itemList;

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
	static void ConvertToCmdList(const std::vector<std::wstring>& baseArgv, const std::vector<std::wstring>& pathList, ARGV_VECTOR& cmdList);

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
		const std::wstring tempFilename = TempFiles::GetTempFilePath(false).GetWinPathString();
		if (tempFilename.empty()) {
			ReportError(L"Error creating temp file", -1);
			return false;
		}
		SCOPE_EXIT{ ::DeleteFile(tempFilename.c_str()); };

		if (!m_pathList.WriteToPathSpecFile(tempFilename)) {
			ReportError(L"Error writing to temp file", -1);
			return false;
		}

		std::vector<std::wstring> argv = GitArgv(m_sParams);
		if (argv.empty()) {
			ReportError(L"Error parsing git parameters", -1);
			return false;
		}
		// the pathspec file stands in for the paths, so it goes before any --end-of-options
		argv.insert(std::ranges::find(argv, L"--end-of-options"),
			{ std::format(L"--pathspec-from-file={}", tempFilename), L"--pathspec-file-nul" });
		CString out;
		const int exitCode = g_Git.Run(argv, &out);
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
	if (GitCall::ms_bCygwinGit || GitCall::ms_bMsys2Git) // see issue https://tortoisegit.org/issue/3542
	{
		max_command_line_length = 3500;
		quotes_length = 4;
	}

	int maxLength = 0;
	int firstCombine = 0;
	for (int i = 0; i < GetListCount(); ++i) {
		if (maxLength + GetListItem(i).GetLength() > max_command_line_length || i == GetListCount() - 1 || cancel) {
			// argv elements need no quoting: each item is one element, whatever it holds
			std::vector<std::wstring> argv = GitArgv(m_sParams);
			if (argv.empty()) {
				ReportError(L"Error parsing git parameters", -1);
				return false;
			}
			if (m_bIsPath)
				argv.emplace_back(L"--");
			for (int j = firstCombine; j <= i; ++j)
				argv.emplace_back(GetListItem(j).GetString());

			CString out;
			const int exitCode = g_Git.Run(argv, &out);
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

inline void CMassiveGitTaskBase::ConvertToCmdList(const std::vector<std::wstring>& baseArgv, const std::vector<std::wstring>& pathList, ARGV_VECTOR& cmdList) {
	if (pathList.empty())
		return;

	// see issue https://tortoisegit.org/issue/3542
	const size_t max_command_line_length{ (GitCall::ms_bCygwinGit || GitCall::ms_bMsys2Git) ? 3500u : 30000u };
	const size_t quotes_length{ (GitCall::ms_bCygwinGit || GitCall::ms_bMsys2Git) ? 4u : 2u };

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
		cmdList.back() << filename;
		currentLength += 1 + quotes_length + filename.size();
	}
}



class CMassiveGitTask : public CMassiveGitTaskBase
{
public:
	CMassiveGitTask(const CString& params, BOOL isPath = TRUE, bool ignoreErrors = false)
	: CMassiveGitTaskBase(params, isPath, ignoreErrors)
	, m_NotifyCallbackAction(CGitProgressList::WC_File_NotificationData::Git_WC_Notify_Action::Add)
	{}


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



void CMassiveGitTask::ReportError(const CString& out, int exitCode)
{
	if (m_NotifyCallbackInstance)
		m_NotifyCallbackInstance->ReportError(out);
	else
	{
		CProgressDlg dlg;
		dlg.m_PreText = L"git.exe ";
		dlg.m_PreText += GetParams();
		dlg.m_PreText += L" [...]\r\n\r\n";
		dlg.m_PreText += out;
		dlg.m_PreFailText.Format(IDS_PROC_PROGRESS_GITUNCLEANEXIT, exitCode);
		dlg.DoModal();
	}
}

void CMassiveGitTask::ReportProgress(const CTGitPath& path, int index)
{
	if (m_progressCallback)
		m_progressCallback(path, index);
	else if (m_NotifyCallbackInstance)
	{
		m_NotifyCallbackInstance->AddNotify(new CGitProgressList::WC_File_NotificationData(path, m_NotifyCallbackAction));
		m_NotifyCallbackInstance->SetItemProgress(index);
	}
}

void CMassiveGitTask::ReportUserCanceled()
{
	if (m_NotifyCallbackInstance)
		m_NotifyCallbackInstance->ReportUserCanceled();
}

bool CMassiveGitTask::ExecuteWithNotify(CTGitPathList* pathList, volatile BOOL& cancel, CGitProgressList::WC_File_NotificationData::Git_WC_Notify_Action action, CGitProgressList* instance)
{
	SetPaths(pathList);
	m_NotifyCallbackInstance = instance;
	m_NotifyCallbackAction = action;
	return ExecuteCommands(cancel);
}
}