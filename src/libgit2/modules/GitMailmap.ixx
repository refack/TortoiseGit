module;
#include <atlstr.h>
#include <gitdll.h>
#include <Git.h>
#include "sanity.h"

export module GitMailmap;
import Registry;
import wstr;
using tgit::wstr::StringView;
// import RIAA;

export namespace GitMailmap
{

class CGitMailmap
{
public:
	CGitMailmap();
	~CGitMailmap();

	CGitMailmap& operator=(CGitMailmap&) = delete;
	CGitMailmap(const CGitMailmap&) = delete;

	operator bool() const { return m_pMailmap; }

	[[nodiscard]] CString TranslateAuthor(const CString& name, const CString& email) const;
	[[nodiscard]] CString TranslateEmail(const CString& name, const CString& email) const;
	void Translate(CString& name, CString& email) const;

	static bool ShouldLoadMailmap();

private:
	GIT_MAILMAP m_pMailmap = nullptr;
};

CGitMailmap::CGitMailmap()
{
	if (ShouldLoadMailmap()) {
		try {
			[[maybe_unused]]CAutoLocker lock(g_Git.m_critGitDllSec);
			g_Git.CheckAndInitDll();
		} catch (const char* msg) {
			MessageBox(nullptr, L"Could not initialize libgit. Disabling Mailmap support.\nlibgit reports:\n" + CUnicodeUtils::GetUnicode<CString>(msg), L"TortoiseGit", MB_ICONERROR);
			return;
		}
		git_read_mailmap(&m_pMailmap);
	}
}

CGitMailmap::~CGitMailmap()
{
	git_free_mailmap(m_pMailmap);
}

bool CGitMailmap::ShouldLoadMailmap()
{
	return Registry::CRegDWORD(L"Software\\TortoiseGit\\UseMailmap", TRUE) == TRUE;
}

void CGitMailmap::Translate(CString& name, CString& email) const
{
	Expects(m_pMailmap);
	struct payload_struct
	{
		const CString* name;
		const char* authorName;
	} payload = { &name, nullptr };
	const char* author1 = nullptr;
	const char* email1 = nullptr;
	const auto lamb = [](void* payload) -> const char* {
		return reinterpret_cast<payload_struct*>(payload)->authorName = _strdup(
			   CUnicodeUtils::GetUTF8<CStringA>(*reinterpret_cast<payload_struct*>(payload)->name));
	};
	git_lookup_mailmap(m_pMailmap, &email1, &author1, CUnicodeUtils::GetUTF8<CStringA>(email), &payload, lamb);
	free((void*)payload.authorName);
	if (email1)
		email = CUnicodeUtils::GetUnicode<CString>(email1);
	if (author1)
		name = CUnicodeUtils::GetUnicode<CString>(author1);
}

std::wstring GetMailmapMapping(GIT_MAILMAP mailmap, std::wstring_view email, std::wstring_view name, bool returnEmail)
{
	const char* author1{};
	const char* email1{};
	const auto emailA = CUnicodeUtils::GetUTF8<CStringA>(email);
	auto callback = [](void* vd) -> const char* {
		const auto _name = *static_cast<std::wstring_view*>(vd);
		return CUnicodeUtils::StdGetUTF8(_name).data();
	};

	if (git_lookup_mailmap(mailmap, &email1, &author1, emailA, &name, callback) == -1)
		return {};
	if (returnEmail && email1)
		return CUnicodeUtils::StdGetUnicode(email1);
	if (returnEmail)
		return {};
	if (author1)
		return CUnicodeUtils::StdGetUnicode(author1);
	return {};
}


CString CGitMailmap::TranslateAuthor(const CString& name, const CString& email) const
{
	Expects(m_pMailmap);
	const std::wstring ret = GetMailmapMapping(m_pMailmap, StringView{ email }, StringView{ name }, false);
	if (ret.empty())
		return name;
	return CString{ ret.data(), gsl::narrow<int>(ret.size()) };
}


CString CGitMailmap::TranslateEmail(const CString& name, const CString& email) const
{
	Expects(m_pMailmap);
	const std::wstring ret = GetMailmapMapping(m_pMailmap, StringView{ email }, StringView{ name }, true);
	if (!ret.empty())
		return email;
	return CString{ ret.data(), gsl::narrow<int>(ret.size()) };
}


}

export using namespace GitMailmap;
