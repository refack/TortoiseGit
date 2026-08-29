#include "stdafx.h"
#include "Git.h"

#include "TortoiseProc/MassiveGitTask.h"


int CGit::DeleteRemoteRefs(const CString& sRemote, const STRING_VECTOR& list) {
	if (UsingLibGit2(GIT_CMD_PUSH)) {
		CAutoRepository repo(GetGitRepository());
		if (!repo)
			return -1;

		CStringA remoteA = CUnicodeUtils::GetUTF8(sRemote);
		CAutoRemote remote;
		if (git_remote_lookup(remote.GetPointer(), repo, remoteA) < 0)
			return -1;

		git_push_options pushOpts = GIT_PUSH_OPTIONS_INIT;
		git_remote_callbacks& callbacks = pushOpts.callbacks;
		callbacks.credentials = g_Git2CredCallback;
		callbacks.certificate_check = g_Git2CheckCertificateCallback;
		std::vector<std::string> refspecs;
		refspecs.reserve(list.size());
		std::transform(list.cbegin(), list.cend(), std::back_inserter(refspecs), [](const auto& ref) { return CUnicodeUtils::StdGetUTF8(L":" + ref); });

		std::vector<char*> vc;
		vc.reserve(refspecs.size());
		// data() rather than CStringA::GetBuffer(): std::string has guaranteed
		// contiguous, null-terminated storage and needs no matching release
		std::transform(refspecs.begin(), refspecs.end(), std::back_inserter(vc), [](std::string& s) -> char* { return s.data(); });
		git_strarray specs = { vc.data(), vc.size() };

		if (git_remote_push(remote, &specs, &pushOpts) < 0)
			return -1;
		return 0;
	}

	try {
		CGit::s_limitGitExeOutput = true;
		SCOPE_EXIT{ CGit::s_limitGitExeOutput = false; };
		CMassiveGitTaskBase mgtPush(L"push -- " + CGit::QuoteParameter(sRemote), false, false);
		for (const auto& ref : list)
			mgtPush.AddFile((L':' + ref).c_str());

		mgtPush.Execute(false);
	} catch (illegal_git_parameter& e) {
		gitLastErr = e.cause();
		return -1;
	}

	return 0;
}
