#pragma once
// Which CString this compile of the core library speaks.
//
// CString is not one type (CLAUDE.md, "The CString ABI wall"): MFC projects get
// CStringT<wchar_t, StrTraitMFC_DLL<...>>, ATL-only ones CStringT<wchar_t,
// StrTraitATL<...>>, and they cannot bind to each other's const CString&.
// Until the library's boundary is std::wstring (Phase A), it is compiled once
// per flavor - TGitLibgit2 (ATL) and TGitLibgit2Mfc (MFC, TGIT_MFC) - which is
// what every vcxproj consumer used to do implicitly by compiling the sources
// itself. This has to be the first include in a module's global module
// fragment: afx.h must precede windows.h, and decides CString before atlstr.h.
#ifdef TGIT_MFC
#include <afx.h>
#endif
#include <atlstr.h>
