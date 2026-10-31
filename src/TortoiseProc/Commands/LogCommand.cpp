// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2007-2008,2011 - TortoiseSVN
// Copyright (C) 2008-2016, 2018-2019, 2025-2026 - TortoiseGit

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
#include "std.h"

#include "LogCommand.h"
#include "LogDlg.h"

bool LogCommand::Execute()
{
	//the log command line looks like this:
	//command:log path:<path_to_file_or_directory_to_show_the_log_messages> [startrev:<startrevision>] [endrev:<endrevision>]

	CString range;

	CString revstart = parser.GetValPtr(L"startrev");
	if (revstart.IsEmpty())
	{
		// support deprecated parameter prior 1.5.0
		revstart = parser.GetValPtr(L"revstart");
	}
	if (revstart == GitRev::GetWorkingCopyRef())
		revstart.Empty();
	if (!revstart.IsEmpty())
		range.Format(L"%s..", g_Git.FixBranchName(revstart));

	CString revend = parser.GetValPtr(L"endrev");
	if (revend.IsEmpty())
	{
		// support deprecated parameter prior 1.5.0
		revend = parser.GetValPtr(L"revend");
	}
	if (revend == GitRev::GetWorkingCopyRef())
		revend.Empty();
	if (!revend.IsEmpty())
		range += g_Git.FixBranchName(revend);

	if (parser.HasVal(L"range"))
		range = parser.GetValPtr(L"range");

	CString val = parser.GetValPtr(L"limit");
	const int limit = _wtoi(val);

	int scale = -1;
	val.MakeLower();
	if (val.Find(L"week") > 0)
		scale = CFilterData::SHOW_LAST_N_WEEKS;
	else if (val.Find(L"month") > 0)
		scale = CFilterData::SHOW_LAST_N_MONTHS;
	else if (val.Find(L"year") > 0)
		scale = CFilterData::SHOW_LAST_N_YEARS;
	else if (val.Find(L"commit") > 0 || limit > 0)
		scale = CFilterData::SHOW_LAST_N_COMMITS;
	else if (val == L"0")
		scale = CFilterData::SHOW_NO_LIMIT;

	const CString rev = parser.GetValPtr(L"rev");

	const CString findStr = parser.GetValPtr(L"findstring");
	const LONG findType = parser.GetLongVal(L"findtype");
	bool findRegex = !!CRegDWORD(L"Software\\TortoiseGit\\UseRegexFilter", FALSE);
	if (parser.HasKey(L"findtext"))
		findRegex = false;
	if (parser.HasKey(L"findregex"))
		findRegex = true;

	CLogDlg dlg;
	theApp.m_pMainWnd = &dlg;
	dlg.SetParams(orgCmdLinePath, cmdLinePath, rev, range, limit, scale);
	dlg.SetFilter(findStr, findType, findRegex);
	if (parser.HasVal(L"outfile"))
	{
		dlg.SetSelect(true);
		dlg.SingleSelection(true);
	}
	dlg.DoModal();
	if (parser.HasVal(L"outfile"))
	{
		CString sText;
		if (!dlg.GetSelectedHash().empty())
			sText = (dlg.GetSelectedHash().at(0).ToString()).c_str();
		StringUtils::WriteStringToTextFile(parser.GetValPtr(L"outfile"), sText, true);
	}
	return true;
}
