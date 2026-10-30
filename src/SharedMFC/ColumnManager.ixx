module;
#include "stdafx.h"

#include <Resources/LoglistCommonResource.h>
#include "Utils/DPIAware.h"

export module ColumnManager;
import std;
import Registry;
import str_utils;
using TCStringW = tgit::wstr::CString;

// registry version number of column-settings of both GitLogListBase and GitStatusListCtrl
#define GITSLC_COL_VERSION 6
#define MAX_COLUMNS 0xff


export struct ColumnDefinition
{
	int localId{}; // just for sanitycheck that the IDs used map to each other
	unsigned int title{};
	bool visibleByDefault{};
	bool available{};
	int defaultWidth = 0;
};

/**
* \ingroup TortoiseProc
* Helper class for CGitStatusListCtrl that represents
* the columns visible and their order as well as
* persisting that data in the registry.
*
* It assigns logical index values to the (potential) columns:
* 0 .. GitSLC_NUMCOLUMNS-1 contain the standard attributes
*
* The column vector contains the columns that are actually
* available in the control.
*
*/
export class ColumnManager
{
public:

	/// construction / destruction

	ColumnManager(CListCtrl* control) : control(control) {};
	~ColumnManager() {};

	/// registry access

	void ReadSettings(std::span<const ColumnDefinition> columns, const TCStringW& containerName, DWORD version);
	void WriteSettings() const;

	/// read column definitions

	int GetColumnCount() const;                     ///< total number of columns
	bool IsVisible(int column) const;
	int GetInvisibleCount() const;
	bool IsRelevant(int column) const;
	TCStringW GetName(int column) const;
	int GetWidth(int column, bool useDefaults = false) const;
	int GetVisibleWidth(int column, bool useDefaults) const;
	void SetRightAlign(int column) const;

	/// switch columns on and off
	void SetVisible(int column, bool visible);

	/// tracking column modifications
	void ColumnMoved(int column, int position);
	/**
	manual: 0: automatic updates, 1: manual updates, 2: manual updates and set to optimal width, 3: reset manual adjusted state
	*/
	void ColumnResized(int column, int manual = 0);

	/// call these to update the user-prop list
	/// (will also auto-insert /-remove new list columns)

	/// don't clutter the context menu with irrelevant prop info

	void RemoveUnusedProps();

	/// bring everything back to its "natural" order
	void ResetColumns();

	void OnHeaderDblClick(NMHDR* pNMHDR, LRESULT* pResult)
	{
		LPNMHEADER header = reinterpret_cast<LPNMHEADER>(pNMHDR);
		if (header && (header->iItem >= 0) && (header->iItem < GetColumnCount()))
		{
			bool bShift = !!(GetAsyncKeyState(VK_SHIFT) & 0x8000);
			ColumnResized(header->iItem, bShift ? 3 : 2);
		}
		*pResult = 0;
	}

	void OnColumnResized(NMHDR* pNMHDR, LRESULT* pResult)
	{
		LPNMHEADER header = reinterpret_cast<LPNMHEADER>(pNMHDR);
		if (header && (header->iItem >= 0) && (header->iItem < GetColumnCount()))
			ColumnResized(header->iItem, 1);
		*pResult = 0;
	}

	void OnColumnMoved(NMHDR* pNMHDR, LRESULT* pResult)
	{
		LPNMHEADER header = reinterpret_cast<LPNMHEADER>(pNMHDR);
		*pResult = TRUE;
		if (header
			&& (header->iItem >= 0)
			&& (header->iItem < GetColumnCount())
			// only allow the reordering if the column was not moved left of the first
			// visible item - otherwise the 'invisible' columns are not at the far left
			// anymore and we get all kinds of redrawing problems.
			&& (header->pitem)
			&& (header->pitem->iOrder >= GetInvisibleCount()))
		{
			ColumnMoved(header->iItem, header->pitem->iOrder);
		}
	}

	void OnHdnBegintrack(NMHDR* pNMHDR, LRESULT* pResult) const
	{
		LPNMHEADER phdr = reinterpret_cast<LPNMHEADER>(pNMHDR);
		*pResult = 0;
		if (phdr->iItem < 0 || phdr->iItem >= static_cast<int>(columns.size()))
			return;

		if (IsVisible(phdr->iItem))
			return;
		*pResult = 1;
	}

	int OnHdnItemchanging(NMHDR* pNMHDR, LRESULT* pResult) const
	{
		LPNMHEADER phdr = reinterpret_cast<LPNMHEADER>(pNMHDR);
		*pResult = 0;
		if (phdr->iItem < 0 || phdr->iItem >= static_cast<int>(columns.size()))
			return 0;

		// visible columns may be modified
		if (IsVisible(phdr->iItem))
			return 0;

		// columns already marked as "invisible" internally may be (re-)sized to 0
		if (phdr->pitem && (phdr->pitem->mask == HDI_WIDTH) && (phdr->pitem->cxy == 0))
			return 0;

		if (phdr->pitem && (phdr->pitem->mask != HDI_WIDTH))
			return 0;

		*pResult = 1;
		return 1;
	}
	void OnContextMenuHeader(CWnd* pWnd, CPoint point, bool isGroundEnable = false);
	void SetOnVisibilityChanged(std::function<void(int, bool)> onVisibilityChanged);

private:
	void AddMenuItem(CMenu* pop)
	{
		UINT uCheckedFlags = MF_STRING | MF_ENABLED | MF_CHECKED;
		UINT uUnCheckedFlags = MF_STRING | MF_ENABLED;

		for (int i = 1; i < static_cast<int>(columns.size()); ++i)
		{
			if (IsRelevant(i))
				pop->AppendMenu(IsVisible(i)
				? uCheckedFlags
				: uUnCheckedFlags
				, i
				, GetName(i));
		}
	}

	DWORD m_dwDefaultColumns = 0;
	DWORD m_dwVersion = 0;

	/// initialization utilities
	void ParseWidths(const TCStringW& widths);
	void SetStandardColumnVisibility(DWORD visibility);
	void ParseColumnOrder(const TCStringW& widths);

	/// map internal column order onto visible column order
	/// (all invisibles in front)

	std::vector<int> GetGridColumnOrder() const;
	void ApplyColumnOrder();

	/// utilities used when writing data to the registry
	DWORD GetSelectedStandardColumns() const;
	TCStringW GetWidthString() const;
	TCStringW GetColumnOrderString() const;

	/// our parent control and its data
	CListCtrl* control = nullptr;

	/// where to store in the registry
	TCStringW registryPrefix;

	/// all columns in their "natural" order
	struct ColumnInfo
	{
		int index;          ///< is a user prop when < GitSLC_USERPROPCOLOFFSET
		int width;
		bool visible;
		bool relevant;      ///< set to @a visible, if no *shown* item has that property
		bool adjusted;
		unsigned int name;
	};

	std::function<void(int, bool)> onVisibilityChanged;

	std::vector<ColumnInfo> columns;

	/// user-defined properties
	std::set<TCStringW> itemProps{};

	/// global column ordering including unused user props
	std::vector<int> columnOrder;
};


// registry access

void ColumnManager::ReadSettings(std::span<const ColumnDefinition> definedColumns, const TCStringW& containerName, DWORD version)
{
	m_dwVersion = version;

	assert(definedColumns.size() < sizeof(DWORD) * 8 && "index is used for DWORD bit-field");
	columns.resize(definedColumns.size());
	m_dwDefaultColumns = 0;
	for (int i = 0; i < static_cast<int>(definedColumns.size()); ++i)
	{
		assert(i == definedColumns[i].localId);
		const DWORD bit = 1 << i;
		columns[i].index = i;
		columns[i].name = definedColumns[i].title;
		columns[i].width = definedColumns[i].defaultWidth;
		columns[i].visible = definedColumns[i].visibleByDefault;
		columns[i].relevant = definedColumns[i].available;
		if (columns[i].visible)
			m_dwDefaultColumns |= bit;
		columns[i].adjusted = false;
	}

	//	userProps.clear();

	// where the settings are stored within the registry
	registryPrefix = L"Software\\TortoiseGit\\StatusColumns\\" + containerName;

	// version check works two fold, generally for the generic format and specifically for the listctrl
	// we accept settings of current version only
	bool valid = static_cast<DWORD>(CRegDWORD(L"Software\\TortoiseGit\\StatusColumns\\Version", GITSLC_COL_VERSION)) == GITSLC_COL_VERSION;
	// we accept settings of current version only
	valid &= static_cast<DWORD>(CRegDWORD(registryPrefix + L"Version", 0xff)) == m_dwVersion;
	if (valid)
	{
		// read (possibly different) column selection
		const DWORD selectedStandardColumns = CRegDWORD(registryPrefix, m_dwDefaultColumns);

		// read column widths
		const TCStringW colWidths = CRegString(registryPrefix + L"_Width");

		ParseWidths(colWidths);

		// process old-style visibility setting
		SetStandardColumnVisibility(selectedStandardColumns);
	}

	// clear all previously set header columns
	int c = control->GetHeaderCtrl()->GetItemCount() - 1;
	while (c >= 0)
		control->DeleteColumn(c--);

	// create columns
	for (int i = 0, count = GetColumnCount(); i < count; ++i)
		control->InsertColumn(i, GetName(i), LVCFMT_LEFT, IsVisible(i) ? -1 : 0);

	// restore column ordering
	if (valid)
		ParseColumnOrder(TCStringW{CRegString(registryPrefix + L"_Order")});
	else
		ParseColumnOrder(TCStringW());

	ApplyColumnOrder();

	// auto-size the columns so we can see them while fetching status
	// (seems the same values will not take affect in InsertColumn)
	for (int i = 0, count = GetColumnCount(); i < count; ++i)
	{
		if (IsVisible(i))
			control->SetColumnWidth(i, GetVisibleWidth(i, true));
	}
}

void ColumnManager::WriteSettings() const
{
	CRegDWORD(L"Software\\TortoiseGit\\StatusColumns\\Version") = GITSLC_COL_VERSION;

	CRegDWORD regVersion(registryPrefix + L"Version", 0, TRUE);
	regVersion = m_dwVersion;

	// write (possibly different) column selection
	CRegDWORD regStandardColumns(registryPrefix, 0, TRUE);
	regStandardColumns = GetSelectedStandardColumns();

	// write column widths
	CRegString regWidths(registryPrefix + L"_Width", TCStringW(), TRUE);
	regWidths = GetWidthString();

	// write column ordering
	CRegString regColumnOrder(registryPrefix + L"_Order", TCStringW(), TRUE);
	regColumnOrder = GetColumnOrderString();
}

// read column definitions

int ColumnManager::GetColumnCount() const
{
	return static_cast<int>(columns.size());
}

bool ColumnManager::IsVisible(int column) const
{
	const size_t index = static_cast<size_t>(column);
	assert(columns.size() > index);

	return columns[index].visible && columns[index].relevant;
}

int ColumnManager::GetInvisibleCount() const
{
	return static_cast<int>(
		std::ranges::count_if(columns, [](const auto& col) { return !col.visible || !col.relevant; })
	);
}

bool ColumnManager::IsRelevant(int column) const
{
	const size_t index = static_cast<size_t>(column);
	assert(columns.size() > index);

	return columns[index].relevant;
}

void ColumnManager::SetRightAlign(int column) const
{
	assert(static_cast<size_t>(column) < columns.size());

	LVCOLUMN col = { 0 };
	col.mask = LVCF_FMT;
	col.fmt = LVCFMT_RIGHT;
	control->SetColumn(column, &col);

	control->Invalidate(FALSE);
}

TCStringW ColumnManager::GetName(int column) const
{
	// standard columns
	const size_t index = static_cast<size_t>(column);
	if (index < columns.size())
	{
		TCStringW result;
		result.LoadString(columns[index].name);
		return result;
	}

	// user-prop columns
	//	if (index < columns.size())
	//		return userProps[columns[index].index - SVNSLC_USERPROPCOLOFFSET].name;

	// default: empty

	return TCStringW();
}

int ColumnManager::GetWidth(int column, bool useDefaults) const
{
	const size_t index = static_cast<size_t>(column);
	assert(columns.size() > index);

	const int width = columns[index].width;
	if ((width == 0) && useDefaults)
	{
		if (index > 0)
		{
			int cx = control->GetStringWidth(GetName(column)) + 20; // 20 pixels for col separator and margin

			for (int i = 0, itemCnt = control->GetItemCount(); i < itemCnt; ++i)
			{
				// get the width of the string and add 14 pixels for the column separator and margins
				const int linewidth = control->GetStringWidth(control->GetItemText(i, column)) + 14;
				if (cx < linewidth)
					cx = linewidth;
			}
			return cx;
		}
		return LVSCW_AUTOSIZE_USEHEADER;
	}
	return width;
}

int ColumnManager::GetVisibleWidth(int column, bool useDefaults) const
{
	return IsVisible(column) ? GetWidth(column, useDefaults) : 0;
}

// switch columns on and off

void ColumnManager::SetVisible(int column, bool visible)
{
	const size_t index = static_cast<size_t>(column);
	assert(index < columns.size());
	assert(columns[index].relevant);

	if (columns[index].visible != visible)
	{
		columns[index].visible = visible;
		if (!visible)
			columns[index].width = 0;

		control->SetColumnWidth(column, GetVisibleWidth(column, true));
		ApplyColumnOrder();

		control->Invalidate(FALSE);

		if (onVisibilityChanged)
			onVisibilityChanged(column, visible);
	}
}

// tracking column modifications
void ColumnManager::ColumnMoved(int column, int position)
{
	// in front of what column has it been inserted?
	const int index = columns[column].index;

	std::vector<int> gridColumnOrder = GetGridColumnOrder();

	const size_t visiblePosition = static_cast<size_t>(position);
	const size_t columnCount = gridColumnOrder.size();

	int next = -1;
	if (visiblePosition < columnCount - 1)
	{
		// the new position (visiblePosition) is the column id w/o the moved column
		gridColumnOrder.erase(std::find(gridColumnOrder.cbegin(), gridColumnOrder.cend(), index));
		next = gridColumnOrder[visiblePosition];
	}

	// move logical column index just in front of that "next" column
	columnOrder.erase(std::find(columnOrder.cbegin(), columnOrder.cend(), index));
	columnOrder.insert(std::find(columnOrder.cbegin(), columnOrder.cend(), next), index);

	// make sure, invisible columns are still put in front of all others
	ApplyColumnOrder();
}

void ColumnManager::ColumnResized(int column, int manual)
{
	const size_t index = static_cast<size_t>(column);
	assert(index < columns.size());
	assert(columns[index].visible && columns[index].relevant);

	const int width = control->GetColumnWidth(column);
	if (manual != 0)
		columns[index].adjusted = (manual < 3);
	if (manual == 2)
	{
		control->SetColumnWidth(column, LVSCW_AUTOSIZE);
		columns[index].width = control->GetColumnWidth(column);
	}
	else if (manual == 3)
	{
		columns[index].width = 0;
		control->SetColumnWidth(column, GetWidth(column, true));
	}
	else
		columns[index].width = width;

	control->Invalidate(FALSE);
}

void ColumnManager::RemoveUnusedProps()
{
	// determine what column indexes / IDs to keep.
	// map them onto new IDs (we may delete some IDs in between)

	std::map<int, int> validIndices;

	for (size_t i = 0, count = columns.size(); i < count; ++i)
	{
		const int index = columns[i].index;

		if (itemProps.find(GetName(static_cast<int>(i))) != itemProps.end() || columns[i].visible)
			validIndices[index] = index;
	}

	// remove everything else:

	// remove from columns and control.
	// also update index values in columns

	for (size_t i = columns.size(); i > 0; --i)
	{
		const auto iter = validIndices.find(columns[i - 1].index);

		if (iter == validIndices.cend())
		{
			control->DeleteColumn(static_cast<int>(i - 1));
			columns.erase(columns.cbegin() + i - 1);
		}
		else
			columns[i - 1].index = iter->second;
	}

	// remove from and update column order

	for (size_t i = columnOrder.size(); i > 0; --i)
	{
		const auto iter = validIndices.find(columnOrder[i - 1]);

		if (iter == validIndices.cend())
			columnOrder.erase(columnOrder.cbegin() + i - 1);
		else
			columnOrder[i - 1] = iter->second;
	}
}

// bring everything back to its "natural" order

void ColumnManager::ResetColumns()
{
	// update internal data
	std::sort(columnOrder.begin(), columnOrder.end());

	for (size_t i = 0, count = columns.size(); i < count; ++i)
	{
		columns[i].width = 0;
		columns[i].visible = (i < 32) && (((m_dwDefaultColumns >> i) & 1) != 0);
		columns[i].adjusted = false;
	}

	// update UI
	for (int i = 0, count = GetColumnCount(); i < count; ++i)
		control->SetColumnWidth(i, GetVisibleWidth(i, true));

	ApplyColumnOrder();

	control->Invalidate(FALSE);
}

// initialization utilities
void ColumnManager::ParseWidths(const TCStringW& widths)
{
	for (int i = 0, count = widths.GetLength() / 8; i < count; ++i)
	{
		const long width = wcstol(widths.Mid(i * 8, 8), nullptr, 16);
		if (i < static_cast<int>(columns.size()))
		{
			// a standard column
			if (width != MAXLONG)
			{
				columns[i].width = CDPIAware::Instance().ScaleX(control->GetSafeHwnd(), width);
				columns[i].adjusted = true;
			}
		}
		else
		{
			// there is no such column
			assert(width == 0);
		}
	}
}

void ColumnManager::SetStandardColumnVisibility(DWORD visibility)
{
	for (size_t i = 0; i < columns.size(); ++i)
	{
		columns[i].visible = (visibility & 1) > 0;
		visibility /= 2;
	}
}

void ColumnManager::ParseColumnOrder(const TCStringW& widths)
{
	std::set<int> alreadyPlaced;
	columnOrder.clear();

	// place columns according to valid entries in orderString

	for (int i = 0, count = widths.GetLength() / 2; i < count; ++i)
	{
		int index = wcstol(widths.Mid(i * 2, 2), nullptr, 16);
		if ((index < static_cast<int>(columns.size())))
		{
			alreadyPlaced.insert(index);
			columnOrder.push_back(index);
		}
	}

	// place the remaining colums behind it
	for (int i = 0; i < static_cast<int>(columns.size()); ++i)
		if (alreadyPlaced.find(i) == alreadyPlaced.end())
			columnOrder.push_back(i);
}

// map internal column order onto visible column order
// (all invisibles in front)
std::vector<int> ColumnManager::GetGridColumnOrder() const
{
	// extract order of used columns from order of all columns

	std::vector<int> result;
	result.reserve(MAX_COLUMNS + 1);

	const size_t colCount = columns.size();
	bool visible = false;

	do
	{
		// put invisible cols in front

		for (size_t i = 0, count = columnOrder.size(); i < count; ++i)
		{
			const int index = columnOrder[i];
			for (size_t k = 0; k < colCount; ++k)
			{
				const ColumnInfo& column = columns[k];
				if (column.index == index && (visible && (column.visible == visible && column.relevant == visible) || !visible && (column.visible == visible || column.relevant == visible)))
					result.push_back(static_cast<int>(k));
			}
		}

		visible = !visible;
	} while (visible);
	assert(columns.size() == result.size());

	return result;
}

void ColumnManager::ApplyColumnOrder()
{
	// extract order of used columns from order of all columns
	int order[MAX_COLUMNS + 1] = { 0 };

	std::vector<int> gridColumnOrder = GetGridColumnOrder();
	std::ranges::copy(gridColumnOrder, order);

	// we must have placed all columns or something is really fishy ..
	assert(gridColumnOrder.size() == columns.size());
	assert(GetColumnCount() == control->GetHeaderCtrl()->GetItemCount());

	// o.k., apply our column ordering
	control->SetColumnOrderArray(GetColumnCount(), order);
}

// utilities used when writing data to the registry

DWORD ColumnManager::GetSelectedStandardColumns() const
{
	DWORD result = 0;
	for (size_t i = columns.size(); i > 0; --i)
		result = result * 2 + (columns[i - 1].visible ? 1 : 0);

	return result;
}

TCStringW ColumnManager::GetWidthString() const
{
	TCStringW result;

	// regular columns
	wchar_t buf[10] = { 0 };
	for (size_t i = 0; i < columns.size(); ++i)
	{
		_stprintf_s(buf, L"%08X", columns[i].adjusted ? CDPIAware::Instance().UnscaleX(control->GetSafeHwnd(), columns[i].width) : MAXLONG);
		result += buf;
	}

	return result;
}

TCStringW ColumnManager::GetColumnOrderString() const
{
	TCStringW result;

	wchar_t buf[3] = { 0 };
	for (size_t i = 0, count = columnOrder.size(); i < count; ++i)
	{
		_stprintf_s(buf, L"%02X", columnOrder[i]);
		result += buf;
	}

	return result;
}

void ColumnManager::OnContextMenuHeader(CWnd* pWnd, CPoint point, bool isGroundEnable /* = false*/)
{
	if ((point.x == -1) && (point.y == -1))
	{
		const auto pHeaderCtrl = dynamic_cast<CHeaderCtrl*>(pWnd);
		CRect rect;
		pHeaderCtrl->GetItemRect(0, &rect);
		pHeaderCtrl->ClientToScreen(&rect);
		point = rect.CenterPoint();
	}

	CMenu popup;
	if (popup.CreatePopupMenu())
	{
		const int columnCount = GetColumnCount();

		TCStringW temp;
		constexpr UINT uCheckedFlags = MF_STRING | MF_ENABLED | MF_CHECKED;
		constexpr UINT uUnCheckedFlags = MF_STRING | MF_ENABLED;

		// build control menu

		//temp.LoadString(IDS_STATUSLIST_SHOWGROUPS);
		//popup.AppendMenu(isGroundEnable? uCheckedFlags : uUnCheckedFlags, columnCount, temp);

		temp.LoadString(IDS_STATUSLIST_RESETCOLUMNORDER);
		popup.AppendMenu(uUnCheckedFlags, columnCount + 2, temp);
		popup.AppendMenu(MF_SEPARATOR);

		// standard columns
		AddMenuItem(&popup);

		// user-prop columns:
		// find relevant ones and sort 'em

		std::map<TCStringW, int> sortedProps;
		for (int i = static_cast<int>(columns.size()); i < columnCount; ++i)
			if (IsRelevant(i))
				sortedProps[GetName(i)] = i;

		if (!sortedProps.empty())
		{
			// add 'em to the menu

			popup.AppendMenu(MF_SEPARATOR);

			for (auto iter = sortedProps.cbegin(), end = sortedProps.cend(); iter != end; ++iter)
			{
				popup.AppendMenu(IsVisible(iter->second)
					? uCheckedFlags
					: uUnCheckedFlags
					, iter->second
					, iter->first);
			}
		}

		// show menu & let user pick an entry

		const int cmd = popup.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN | TPM_NONOTIFY, point.x, point.y, pWnd);
		if ((cmd >= 1) && (cmd < columnCount))
			SetVisible(cmd, !IsVisible(cmd));
		else if (cmd == columnCount)
		{
			std::ignore = pWnd->GetParent()->SendMessage(LVM_ENABLEGROUPVIEW, !isGroundEnable, NULL);
			//EnableGroupView(!isGroundEnable);
		}
		else if (cmd == columnCount + 1)
			RemoveUnusedProps();
		else if (cmd == columnCount + 2)
		{
			temp.LoadString(IDS_CONFIRMRESETCOLUMNORDER);
			if (MessageBox(pWnd->m_hWnd, temp, L"TortoiseGit", MB_YESNO | MB_ICONQUESTION) == IDYES)
				ResetColumns();
		}
	}
}

void ColumnManager::SetOnVisibilityChanged(std::function<void(int, bool)> onVisibilityChangedFct)
{
	onVisibilityChanged = std::move(onVisibilityChangedFct);
}
