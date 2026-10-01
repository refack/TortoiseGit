// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2026 - TortoiseGit

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

// DRAFT, NOT YET BUILT. TDD assertions for conjectures about what TortoiseGit
// hands the shell: the clipboard, OLE data objects, and the credential store
// (2026-09-28).
//   FAILS - a real bug; PASSES - a pin, or a spurious conjecture;
//   CRASHES - the old code terminates rather than failing an expectation.
//
// Side effects: the clipboard tests replace the clipboard's contents, and the
// credential test writes (and deletes) one entry in the user's credential store.
//
// GitDataObject tests need the module to *export* the class: it is declared in
// a non-exported namespace, and `export using namespace` does not export the
// entities themselves.

#include "stdafx.h"
#include "Utils/DragDropImpl.h"
#include "WindowsCredentialsStore.h"

import std;
import StringUtils;
import TGitPath;
import GitHash;
import GitDataObject;

using namespace std::literals;

namespace
{
/// The clipboard's data for \a format, as raw bytes of the block's full size
/// (which GlobalAlloc may have rounded up), or nullopt when there is none.
std::optional<std::string> ClipboardBlock(const UINT format)
{
	if (!OpenClipboard(nullptr))
		return std::nullopt;
	SCOPE_EXIT { CloseClipboard(); };
	const HANDLE data = GetClipboardData(format);
	if (!data)
		return std::nullopt;
	const auto bytes = static_cast<const char*>(GlobalLock(data));
	if (!bytes)
		return std::nullopt;
	std::string copy(bytes, GlobalSize(data));
	GlobalUnlock(data);
	return copy;
}

HANDLE ClipboardHandle(const UINT format)
{
	if (!OpenClipboard(nullptr))
		return nullptr;
	SCOPE_EXIT { CloseClipboard(); };
	return GetClipboardData(format);
}
}

// ---------------------------------------------------------------------------
// Clipboard (StringUtils)

// Conjecture: two of the three writers opened the clipboard in an
// if-init-statement - `if (CClipboardHelper h; !h.Open(w)) return false;` - so
// it closed again at the end of that `if`, before EmptyClipboard/SetClipboardData
// ran. Those calls then fail on a closed clipboard and the writer returns false.
// Old StringUtils: FAILS.
TEST(ConjectureShellData, AsciiClipboardWriterKeepsTheClipboardOpen)
{
	const LCID lcid = MAKELCID(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), SORT_DEFAULT);
	ASSERT_TRUE(StringUtils::WriteAsciiStringToClipboard(CStringA("hello"), lcid, nullptr));
	const auto text = ClipboardBlock(CF_TEXT);
	ASSERT_TRUE(text.has_value());
	EXPECT_EQ("hello", std::string(text->c_str()));
}

// Conjecture: CF_LOCALE was given the GlobalLock'ed *pointer* rather than the
// HGLOBAL, which differ for a GMEM_MOVEABLE block, so the locale never arrived.
// Old StringUtils: FAILS (once the clipboard stays open at all).
TEST(ConjectureShellData, AsciiClipboardWriterSetsTheLocale)
{
	const LCID lcid = MAKELCID(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), SORT_DEFAULT);
	ASSERT_TRUE(StringUtils::WriteAsciiStringToClipboard(CStringA("hello"), lcid, nullptr));
	const auto locale = ClipboardBlock(CF_LOCALE);
	ASSERT_TRUE(locale.has_value());
	ASSERT_GE(locale->size(), sizeof(LCID));
	LCID stored = 0;
	std::memcpy(&stored, locale->data(), sizeof(stored));
	EXPECT_EQ(lcid, stored);
}

// Conjecture: the same if-init-statement defect in WriteDiffToClipboard.
// Old StringUtils: FAILS.
TEST(ConjectureShellData, DiffClipboardWriterKeepsTheClipboardOpen)
{
	ASSERT_TRUE(StringUtils::WriteDiffToClipboard(CStringA("--- a\n+++ b\n"), nullptr));
	const UINT diffFormat = RegisterClipboardFormat(L"TGIT_UNIFIEDDIFF");
	const auto diff = ClipboardBlock(diffFormat);
	ASSERT_TRUE(diff.has_value());
	EXPECT_EQ("--- a\n+++ b\n", std::string(diff->c_str()));
	const auto unicode = ClipboardBlock(CF_UNICODETEXT);
	ASSERT_TRUE(unicode.has_value());
	EXPECT_EQ(L"--- a\n+++ b\n"sv, std::wstring_view(reinterpret_cast<const wchar_t*>(unicode->data())));
}

// Conjecture (unfixed): WriteDiffToClipboard hands the *same* HGLOBAL to the
// clipboard twice, as the diff format and as CF_TEXT. The clipboard then owns it
// twice, and the next EmptyClipboard frees it twice.
// Old and current StringUtils: FAILS.
TEST(ConjectureShellData, DiffClipboardFormatsAreSeparateAllocations)
{
	ASSERT_TRUE(StringUtils::WriteDiffToClipboard(CStringA("diff"), nullptr));
	const HANDLE diff = ClipboardHandle(RegisterClipboardFormat(L"TGIT_UNIFIEDDIFF"));
	const HANDLE text = ClipboardHandle(CF_TEXT);
	ASSERT_NE(nullptr, diff);
	ASSERT_NE(nullptr, text);
	EXPECT_NE(diff, text);
}

// ---------------------------------------------------------------------------
// OLE data objects

// Conjecture: CIDataObject::SetDropDescription left medium.tymed at TYMED_NULL.
// SetData asserts that it matches fetc.tymed (a debug assertion dialog), and
// what it stores is a medium that ReleaseStgMedium will not free and GetData
// hands back as TYMED_NULL.
// Old DragDropImpl: FAILS (after the assertion).
TEST(ConjectureShellData, DropDescriptionIsStoredAsAnHGlobal)
{
	auto* dataObject = new CIDataObject(nullptr);
	dataObject->AddRef();
	SCOPE_EXIT { dataObject->Release(); };

	ASSERT_EQ(S_OK, dataObject->SetDropDescription(DROPIMAGE_COPY, L"Copy to %1", L"target"));

	FORMATETC fetc = { static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_DROPDESCRIPTION)), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	STGMEDIUM medium = {};
	ASSERT_EQ(S_OK, dataObject->GetData(&fetc, &medium));
	SCOPE_EXIT { ReleaseStgMedium(&medium); };
	ASSERT_EQ(static_cast<DWORD>(TYMED_HGLOBAL), medium.tymed);
	const auto description = static_cast<const DROPDESCRIPTION*>(GlobalLock(medium.hGlobal));
	ASSERT_NE(nullptr, description);
	EXPECT_EQ(DROPIMAGE_COPY, description->type);
	EXPECT_STREQ(L"Copy to %1", description->szMessage);
	EXPECT_STREQ(L"target", description->szInsert);
	GlobalUnlock(medium.hGlobal);
}

// Conjecture: an over-long drop description is truncated and terminated, where
// StringCchCopy's truncation was silent and wcscpy_s would have crashed.
TEST(ConjectureShellData, DropDescriptionTruncatesLongText)
{
	auto* dataObject = new CIDataObject(nullptr);
	dataObject->AddRef();
	SCOPE_EXIT { dataObject->Release(); };

	const std::wstring longText(2 * MAX_PATH, L'm');
	ASSERT_EQ(S_OK, dataObject->SetDropDescription(DROPIMAGE_MOVE, longText.c_str(), L""));

	FORMATETC fetc = { static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_DROPDESCRIPTION)), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	STGMEDIUM medium = {};
	ASSERT_EQ(S_OK, dataObject->GetData(&fetc, &medium));
	SCOPE_EXIT { ReleaseStgMedium(&medium); };
	const auto description = static_cast<const DROPDESCRIPTION*>(GlobalLock(medium.hGlobal));
	ASSERT_NE(nullptr, description);
	EXPECT_EQ(std::size(description->szMessage) - 1, wcsnlen(description->szMessage, std::size(description->szMessage)));
	GlobalUnlock(medium.hGlobal);
}

namespace
{
TGitPath::CTGitPathList TwoFiles()
{
	TGitPath::CTGitPathList list;
	list.AddPath(TGitPath::CTGitPath(L"a.txt"));
	list.AddPath(TGitPath::CTGitPath(L"b.txt"));
	return list;
}

FORMATETC Format(const CLIPFORMAT format)
{
	return { format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
}
}

// Conjecture (unfixed): GitDataObject appends to m_allPaths on every
// CF_FILEDESCRIPTOR request and never clears it, so asking twice - which the
// shell does - describes every file twice. CF_FILECONTENTS indexes into the
// same vector, so the indices go on matching only by accident.
// Old and current GitDataObject: FAILS.
TEST(ConjectureShellData, RepeatedFileDescriptorRequestsDoNotAccumulate)
{
	auto* dataObject = new GitDataObject::GitDataObject(TwoFiles(), TGitHash::CGitHash());
	dataObject->AddRef();
	SCOPE_EXIT { dataObject->Release(); };
	FORMATETC fetc = Format(static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR)));

	for (int request = 0; request < 2; ++request)
	{
		STGMEDIUM medium = {};
		ASSERT_EQ(S_OK, dataObject->GetData(&fetc, &medium));
		const auto group = static_cast<const FILEGROUPDESCRIPTOR*>(GlobalLock(medium.hGlobal));
		ASSERT_NE(nullptr, group);
		EXPECT_EQ(2u, group->cItems) << "request " << request;
		GlobalUnlock(medium.hGlobal);
		ReleaseStgMedium(&medium);
	}
}

// Conjecture: the CF_HDROP header was built in make_unique<char[]> storage,
// which is not zeroed, so DROPFILES::pt and fNC were whatever the heap held.
// Old GitDataObject: FAILS nondeterministically (whenever the heap is dirty).
TEST(ConjectureShellData, HDropHeaderIsFullyInitialized)
{
	auto* dataObject = new GitDataObject::GitDataObject(TwoFiles(), TGitHash::CGitHash());
	dataObject->AddRef();
	SCOPE_EXIT { dataObject->Release(); };
	FORMATETC fetc = Format(CF_HDROP);

	STGMEDIUM medium = {};
	ASSERT_EQ(S_OK, dataObject->GetData(&fetc, &medium));
	SCOPE_EXIT { ReleaseStgMedium(&medium); };
	const auto header = static_cast<const DROPFILES*>(GlobalLock(medium.hGlobal));
	ASSERT_NE(nullptr, header);
	EXPECT_EQ(sizeof(DROPFILES), header->pFiles);
	EXPECT_TRUE(header->fWide);
	EXPECT_EQ(0, header->pt.x);
	EXPECT_EQ(0, header->pt.y);
	EXPECT_FALSE(header->fNC);
	// the names: each NUL-terminated, then one more NUL
	const auto names = reinterpret_cast<const wchar_t*>(reinterpret_cast<const BYTE*>(header) + header->pFiles);
	const std::wstring_view first(names);
	const std::wstring_view second(names + first.size() + 1);
	EXPECT_TRUE(first.ends_with(L"a.txt")) << first;
	EXPECT_TRUE(second.ends_with(L"b.txt")) << second;
	EXPECT_EQ(L'\0', names[first.size() + 1 + second.size() + 1]);
	GlobalUnlock(medium.hGlobal);
}

// Conjecture: a file whose name does not fit FILEDESCRIPTOR::cFileName is left
// out, and cItems counts only the ones that are there. Pins the skip.
// Old GitDataObject: PASSES.
TEST(ConjectureShellData, FileDescriptorSkipsNamesThatDoNotFit)
{
	TGitPath::CTGitPathList list;
	list.AddPath(TGitPath::CTGitPath(std::wstring(MAX_PATH, L'n') + L".txt"));
	list.AddPath(TGitPath::CTGitPath(L"fits.txt"));
	auto* dataObject = new GitDataObject::GitDataObject(list, TGitHash::CGitHash());
	dataObject->AddRef();
	SCOPE_EXIT { dataObject->Release(); };
	FORMATETC fetc = Format(static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR)));

	STGMEDIUM medium = {};
	ASSERT_EQ(S_OK, dataObject->GetData(&fetc, &medium));
	SCOPE_EXIT { ReleaseStgMedium(&medium); };
	const auto group = static_cast<const FILEGROUPDESCRIPTOR*>(GlobalLock(medium.hGlobal));
	ASSERT_NE(nullptr, group);
	ASSERT_EQ(1u, group->cItems);
	EXPECT_STREQ(L"fits.txt", group->fgd[0].cFileName);
	GlobalUnlock(medium.hGlobal);
}

// ---------------------------------------------------------------------------
// Credential store

// Conjecture: GetCredential copied the blob with wcsncpy_s and an explicit
// count, so a blob longer than m_password - which another program can store
// under our entry name - went to the invalid-parameter handler.
// Old header: CRASHES.
TEST(ConjectureShellData, OverlongStoredPasswordIsTruncatedNotFatal)
{
	constexpr auto entry = L"TGitUnitTest-OverlongPassword";
	CWindowsCredentialsStore::DeleteCredential(entry);
	const std::wstring password(300, L'p');
	ASSERT_EQ(0, CWindowsCredentialsStore::SaveCredential(entry, L"user", password.c_str()));
	SCOPE_EXIT { CWindowsCredentialsStore::DeleteCredential(entry); };

	CCredentials credentials;
	ASSERT_EQ(0, CWindowsCredentialsStore::GetCredential(entry, credentials));
	const size_t length = wcsnlen(credentials.m_password, std::size(credentials.m_password));
	EXPECT_EQ(std::size(credentials.m_password) - 1, length);
	EXPECT_EQ(std::wstring_view(password).substr(0, length), std::wstring_view(credentials.m_password, length));
}
