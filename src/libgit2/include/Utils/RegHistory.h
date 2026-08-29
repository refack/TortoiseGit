// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2013 - TortoiseGit
// Copyright (C) 2007 - TortoiseSVN

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
#include <windows.h>
#include <string>
#include <vector>

import Registry;

/**
 * \ingroup TortoiseProc
 * Maintains a list of X string items in the registry and provides methods
 * to add new items. The list can be used as a 'recently used' or 'recent items' list.
 */
class CRegHistory
{
public:
	CRegHistory() = default;
	virtual ~CRegHistory() = default;

	/// Loads the history
	/// \param lpszSection the section in the registry, e.g., "Software\\CompanyName\\History"
	/// \param lpszKeyPrefix the name of the registry values, e.g., "historyItem"
	/// \return the number of history items loaded
	size_t Load(LPCWSTR lpszSection, LPCWSTR lpszKeyPrefix)
	{
		if (!lpszSection || !lpszKeyPrefix || *lpszSection == '\0')
			return -1ull;

		m_arEntries.clear();

		m_sSection = lpszSection;
		m_sKeyPrefix = lpszKeyPrefix;

		int n = 0;
		std::wstring sText;
		do
		{
			//keys are of form <lpszKeyPrefix><entrynumber>
			wchar_t sKey[4096] = {};
			swprintf_s(sKey, L"%s\\%s%d", lpszSection, lpszKeyPrefix, n++);
			sText = CRegStdString(sKey);
			if (!sText.empty())
				m_arEntries.push_back(sText);
		} while (!sText.empty() && n < m_nMaxHistoryItems);

		return m_arEntries.size();
	}

	/// Saves the history.
	[[nodiscard]] bool Save() const
	{
		if (m_sSection.empty())
			return false;

		// save history to registry
		int nMax = static_cast<int>(std::min(m_arEntries.size(), static_cast<size_t>(m_nMaxHistoryItems) + 1));
		for (int n = 0; n < static_cast<int>(m_arEntries.size()); ++n)
		{
			wchar_t sKey[4096] = {};
			swprintf_s(sKey, L"%s\\%s%d", m_sSection.c_str(), m_sKeyPrefix.c_str(), n);
			CRegStdString regkey(sKey);
			regkey = m_arEntries[n];
		}
		// remove items exceeding the max number of history items
		for (int n = nMax; ; ++n)
		{
			wchar_t sKey[4096] = {};
			swprintf_s(sKey, L"%s\\%s%d", m_sSection.c_str(), m_sKeyPrefix.c_str(), n);
			CRegStdString regkey(sKey);
			if (static_cast<std::wstring>(regkey).empty())
				break;
			regkey.removeValue(); // remove entry
		}
		return true;
	}

	/// Adds a new string to the history list.
	bool AddEntry(LPCWSTR szText)
	{
		if (!szText[0])
			return false;

		if (!m_sSection.empty() && !m_sKeyPrefix.empty())
		{
			// refresh the history from the registry
			Load(m_sSection.c_str(), m_sKeyPrefix.c_str());
		}

		for (auto i = 0u; i<m_arEntries.size(); ++i)
		{
			if (wcscmp(szText, m_arEntries[i].c_str()) == 0)
			{
				m_arEntries.erase(m_arEntries.cbegin() + i);
				m_arEntries.insert(m_arEntries.cbegin(), szText);
				return false;
			}
		}
		m_arEntries.insert(m_arEntries.cbegin(), szText);
		return true;
	}

	/// Removes the entry at index \c pos.
	void RemoveEntry(int pos)
	{
		m_arEntries.erase(m_arEntries.cbegin() + pos);
	}

	/// Sets the maximum number of items in the history. Default is 25.
	void SetMaxHistoryItems(int nMax) {m_nMaxHistoryItems = nMax;}
	/// Returns the number of items in the history.
	[[nodiscard]] size_t GetCount() const {return m_arEntries.size(); }
	[[nodiscard]] bool IsEmpty() const { return m_arEntries.empty(); }
	/// Returns the entry at index \c pos
	[[nodiscard]] LPCWSTR GetEntry(size_t pos) const { return m_arEntries[pos].c_str(); }

private:
	std::wstring m_sSection;
	std::wstring m_sKeyPrefix;
	std::vector<std::wstring> m_arEntries;
	int m_nMaxHistoryItems{25};
};



size_t CRegHistory::Load(LPCWSTR lpszSection, LPCWSTR lpszKeyPrefix)

bool CRegHistory::Save() const

