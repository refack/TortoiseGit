// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2020, 2023 - TortoiseGit

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

import std;

using std::operator""sv;

#include <afxdockablepane.h>
#include <afxdocksite.h>
#include <afxregpath.h>
#include <afxsettingsstore.h>

#include "DPIAware.h"

constexpr auto AFX_REG_SECTION_FMT = L"{}Pane-{}"sv;
constexpr auto AFX_REG_SECTION_FMT_EX = L"{}Pane-{}{}"sv;
constexpr auto n_index = -1;
constexpr auto n_ui_id = static_cast<UINT>(n_index);

struct CDockablePaneUnscaledStoredState : CDockablePane
{
	BOOL LoadState(const LPCWSTR lpszProfileName, int nIndex, UINT uiID) override {
		CString strProfileName = ::AFXGetRegPath(AFX_CONTROL_BAR_PROFILE, lpszProfileName);

		if (nIndex == n_index)
			nIndex = GetDlgCtrlID();

		std::wstring strSection;
		if (uiID == n_ui_id)
			strSection += std::format(AFX_REG_SECTION_FMT, strProfileName, nIndex);
		else
			strSection += std::format(AFX_REG_SECTION_FMT_EX, strProfileName, nIndex, uiID);

		CSettingsStoreSP regSP;
		CSettingsStore& reg = regSP.Create(FALSE, TRUE);

		if (!reg.Open(strSection.c_str()))
			return FALSE;

		reg.Read(_T("ID"), reinterpret_cast<int&>(m_nID));

		reg.Read(_T("RectRecentFloat"), m_recentDockInfo.m_rectRecentFloatingRect);
		reg.Read(_T("RectRecentDocked"), m_rectSavedDockedRect);

		CDPIAware::Instance().ScaleRect(GetSafeHwnd(), &m_recentDockInfo.m_rectRecentFloatingRect);
		CDPIAware::Instance().ScaleRect(GetSafeHwnd(), &m_rectSavedDockedRect);

		m_recentDockInfo.m_recentSliderInfo.m_rectDockedRect = m_rectSavedDockedRect;

		reg.Read(_T("RecentFrameAlignment"), m_recentDockInfo.m_dwRecentAlignmentToFrame);
		reg.Read(_T("RecentRowIndex"), m_recentDockInfo.m_nRecentRowIndex);
		reg.Read(_T("IsFloating"), m_bRecentFloatingState);
		reg.Read(_T("MRUWidth"), m_nMRUWidth);
		reg.Read(_T("PinState"), m_bPinState);

		return CDockablePane::LoadState(lpszProfileName, nIndex, uiID); // skip CDockablePane!
	}

	BOOL SaveState(const LPCWSTR lpszProfileName, int nIndex, UINT uiID) override {
		CString strProfileName = ::AFXGetRegPath(AFX_CONTROL_BAR_PROFILE, lpszProfileName);

		if (nIndex == -1)
			nIndex = GetDlgCtrlID();

		std::wstring strSection;
		if (uiID == n_ui_id)
			strSection += std::format(AFX_REG_SECTION_FMT, strProfileName, nIndex);
		else
			strSection += std::format(AFX_REG_SECTION_FMT_EX, strProfileName, nIndex, uiID);

		CSettingsStoreSP regSP;
		CSettingsStore& reg = regSP.Create(FALSE, FALSE);

		if (reg.CreateKey(strSection.c_str())) {
			BOOL bFloating = IsFloating();

			if (bFloating) {
				CPaneFrameWnd* pMiniFrame = GetParentMiniFrame();
				if (pMiniFrame)
					pMiniFrame->GetWindowRect(m_recentDockInfo.m_rectRecentFloatingRect);
			} else {
				CalcRecentDockedRect();
				if (m_pParentDockBar) {
					m_recentDockInfo.m_dwRecentAlignmentToFrame = CDockablePane::m_pParentDockBar->GetCurrentAlignment();
					m_recentDockInfo.m_nRecentRowIndex = m_pParentDockBar->FindRowIndex(m_pDockBarRow);
				}
			}

			reg.Write(_T("ID"), reinterpret_cast<int&>(m_nID));

			CRect floatingRect = m_recentDockInfo.m_rectRecentFloatingRect;
			CRect dockedRect = m_recentDockInfo.m_recentSliderInfo.m_rectDockedRect;
			CDPIAware::Instance().UnscaleRect(GetSafeHwnd(), &floatingRect);
			CDPIAware::Instance().UnscaleRect(GetSafeHwnd(), &dockedRect);

			reg.Write(_T("RectRecentFloat"), floatingRect);
			reg.Write(_T("RectRecentDocked"), dockedRect);

			reg.Write(_T("RecentFrameAlignment"), m_recentDockInfo.m_dwRecentAlignmentToFrame);
			reg.Write(_T("RecentRowIndex"), m_recentDockInfo.m_nRecentRowIndex);
			reg.Write(_T("IsFloating"), bFloating);
			reg.Write(_T("MRUWidth"), m_nMRUWidth);
			reg.Write(_T("PinState"), m_bPinState);
		}
		return CDockablePane::SaveState(lpszProfileName, nIndex, uiID); // skip CDockablePane!
	}
};
