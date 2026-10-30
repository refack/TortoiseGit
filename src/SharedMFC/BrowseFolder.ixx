module;
#include "stdafx.h"
#include <strsafe.h>
#include <tgitlib/sanity.h>


export module BrowseFolder;
import wil;


export namespace BrowseFolder {



/**
 * helper class for IFileDialogEvents and IFileDialogControlEvents.
 * use this class as a base class so you only need to implement
 * the methods you actually need and not all of them.
 */
class CFileDlgEventHandler :
public CComObjectRootEx<CComSingleThreadModel>,
public CComCoClass<CFileDlgEventHandler>,
public IFileDialogEvents,
public IFileDialogControlEvents
{
public:
	CFileDlgEventHandler() = default;
	// ReSharper disable once CppHidingFunction
	virtual ~CFileDlgEventHandler() = default;

	BEGIN_COM_MAP(CFileDlgEventHandler)
		COM_INTERFACE_ENTRY(IFileDialogEvents)
		COM_INTERFACE_ENTRY(IFileDialogControlEvents)
	END_COM_MAP()

	// IFileDialogEvents
	STDMETHODIMP OnFileOk(IFileDialog* pfd) override{
		return S_OK;	// allow the dialog to close
	}
	STDMETHODIMP OnFolderChanging(IFileDialog* pfd, IShellItem* psiFolder) override{
		return S_OK;	// allow the change
	}
	STDMETHODIMP OnFolderChange(IFileDialog* pfd) override{
		return S_OK;
	}
	STDMETHODIMP OnSelectionChange(IFileDialog* pfd) override{
		return S_OK;
	}
	STDMETHODIMP OnShareViolation(IFileDialog* pfd, IShellItem* psi, FDE_SHAREVIOLATION_RESPONSE* pResponse) override{
		return S_OK;
	}
	STDMETHODIMP OnTypeChange(IFileDialog* pfd) override{
		return S_OK;
	}
	STDMETHODIMP OnOverwrite(IFileDialog* pfd, IShellItem* psi, FDE_OVERWRITE_RESPONSE* pResponse) override{
		return S_OK;
	}

	// IFileDialogControlEvents
	STDMETHODIMP OnItemSelected(IFileDialogCustomize* pfdc, DWORD dwIDCtl, DWORD dwIDItem) override{
		return S_OK;
	}
	STDMETHODIMP OnButtonClicked(IFileDialogCustomize* pfdc, DWORD dwIDCtl) override{
		return S_OK;
	}
	STDMETHODIMP OnCheckButtonToggled(IFileDialogCustomize* pfdc, DWORD dwIDCtl, BOOL bChecked) override{
		return S_OK;
	}
	STDMETHODIMP OnControlActivating(IFileDialogCustomize* pfdc, DWORD dwIDCtl) override{
		return S_OK;
	}
};



/**
 * \ingroup Utils
 * On Vista and later, the IFileDialog is used with the FOS_PICKFOLDERS flag.
 */
class CBrowseFolder
{
public:
	enum retVal
	{
		CANCEL = 0,		///< the user has pressed cancel
		NOPATH,			///< no folder was selected
		OK				///< everything went just fine
	};

	//constructor / deconstructor
	CBrowseFolder();
	~CBrowseFolder();

	DWORD m_style = 0;		///< styles of the dialog.
	/**
	 * Sets the info text of the dialog. Call this method before calling Show().
	 */
	void SetInfo(LPCWSTR title);
	/*
	 * Sets the text to show for the checkbox. If this method is not called,
	 * then no checkbox is added.
	 */
	void SetCheckBoxText(LPCWSTR checktext);
	void SetCheckBoxText2(LPCWSTR checktext);
	/**
	 * Shows the Dialog.
	 * \param parent [in] window handle of the parent window.
	 * \param path [out] the path to the folder which the user has selected
	 * \param sDefaultPath [in]
	 * \return one of CANCEL, NOPATH or OK
	 */
	CBrowseFolder::retVal Show(HWND parent, CString& path, const CString& sDefaultPath = CString());
	CBrowseFolder::retVal Show(HWND parent, LPWSTR path, size_t pathlen, LPCWSTR szDefaultPath = nullptr);

	/**
	 * If this is set to true, then the second checkbox gets disabled as soon as the first
	 * checkbox is checked. If the first checkbox is unchecked, then the second checkbox is enabled
	 * again.
	 */
	void DisableCheckBox2WhenCheckbox1IsEnabled(bool bSet = true) {m_DisableCheckbox2WhenCheckbox1IsChecked = bSet;}

	static BOOL m_bCheck;		///< state of the checkbox on closing the dialog
	static BOOL m_bCheck2;

protected:
	static CString m_sDefaultPath;
	wchar_t m_title[200]{};
	CString m_CheckText;
	CString m_CheckText2;
	bool m_DisableCheckbox2WhenCheckbox1IsChecked = false;
};


BOOL CBrowseFolder::m_bCheck = FALSE;
BOOL CBrowseFolder::m_bCheck2 = FALSE;
CString CBrowseFolder::m_sDefaultPath;

class BrowseFolderDlgEventHandler : public CFileDlgEventHandler
{
public:
	BrowseFolderDlgEventHandler() = default;

	bool m_DisableCheckbox2WhenCheckbox1IsChecked = false;

	STDMETHODIMP OnCheckButtonToggled(IFileDialogCustomize *pfdc, DWORD dwIDCtl, BOOL bChecked) override
	{
		if (m_DisableCheckbox2WhenCheckbox1IsChecked && dwIDCtl == 101)
		{
			if (bChecked)
				pfdc->SetControlState(102, CDCS_VISIBLE | CDCS_INACTIVE);
			else
				pfdc->SetControlState(102, CDCS_VISIBLE | CDCS_ENABLED);
		}
		return S_OK;
	}
};

CBrowseFolder::CBrowseFolder()
{
}

CBrowseFolder::~CBrowseFolder()
{
}

//show the dialog
CBrowseFolder::retVal CBrowseFolder::Show(HWND parent, LPWSTR path, size_t pathlen, LPCWSTR szDefaultPath /* = nullptr */)
{
	CString temp = path;
	CString sDefault;
	if (szDefaultPath)
		sDefault = szDefaultPath;
	const CBrowseFolder::retVal ret = Show(parent, temp, sDefault);
	wcscpy_s(path, pathlen, temp);
	return ret;
}
CBrowseFolder::retVal CBrowseFolder::Show(HWND parent, CString& path, const CString& sDefaultPath /* = CString() */)
{
	m_sDefaultPath = sDefaultPath;
	if (m_sDefaultPath.IsEmpty() && !path.IsEmpty())
	{
		while (!PathFileExists(path) && !path.IsEmpty())
		{
			CString p = path.Left(path.ReverseFind(L'\\'));
			if ((p.GetLength() == 2) && (p[1] == L':'))
			{
				p += L'\\';
				if (p.Compare(path) == 0)
					p.Empty();
			}
			if (p.GetLength() < 2)
				p.Empty();
			path = p;
		}
		// if the result path already contains a path, use that as the default path
		m_sDefaultPath = path;
	}

	// Create a new common open file dialog
	CComPtr<IFileOpenDialog> pfd;
	if (FAILED(pfd.CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER)))
		return CANCEL;

	// Set the dialog as a folder picker
	DWORD dwOptions;
	if (FAILED(pfd->GetOptions(&dwOptions)))
		return CANCEL;
	if (FAILED(pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST)))
		return CANCEL;

	// Set a title
	wchar_t* nl = wcschr(m_title, L'\n');
	if (nl)
		*nl = L'\0';
	pfd->SetTitle(m_title);

	// set the default folder
	CComPtr<IShellItem> psiDefault;
	if (FAILED(SHCreateItemFromParsingName(m_sDefaultPath, nullptr, IID_PPV_ARGS(&psiDefault))))
		return CANCEL;
	if (FAILED(pfd->SetFolder(psiDefault)))
		return CANCEL;

	CComObjectStackEx<BrowseFolderDlgEventHandler> cbk;
	cbk.m_DisableCheckbox2WhenCheckbox1IsChecked = m_DisableCheckbox2WhenCheckbox1IsChecked;
	CComQIPtr<IFileDialogEvents> pEvents = cbk.GetUnknown();

	if (!m_CheckText.IsEmpty())
	{
		CComPtr<IFileDialogCustomize> pfdCustomize;
		if (FAILED(pfd.QueryInterface(&pfdCustomize)))
			return CANCEL;

		pfdCustomize->StartVisualGroup(100, L"");
		pfdCustomize->AddCheckButton(101, m_CheckText, FALSE);
		if (!m_CheckText2.IsEmpty())
			pfdCustomize->AddCheckButton(102, m_CheckText2, FALSE);
		pfdCustomize->EndVisualGroup();
	}

	DWORD eventsCookie;
	if (FAILED(pfd->Advise(pEvents, &eventsCookie)))
		return CANCEL;
	SCOPE_EXIT { pfd->Unadvise(eventsCookie); };

	// Show the open file dialog
	if (FAILED(pfd->Show(parent)))
		return CANCEL;

	// Get the selection from the user
	CComPtr<IShellItem> psiResult;
	if (FAILED(pfd->GetResult(&psiResult)))
		return CANCEL;

	if (CComHeapPtr<WCHAR> pszPath; SUCCEEDED(psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
		path = pszPath;

	CComPtr<IFileDialogCustomize> pfdCustomize;
	if (SUCCEEDED(pfd.QueryInterface(&pfdCustomize)))
	{
		pfdCustomize->GetCheckButtonState(101, &m_bCheck);
		pfdCustomize->GetCheckButtonState(102, &m_bCheck2);
	}

	return OK;
}

void CBrowseFolder::SetInfo(LPCWSTR title)
{
	ASSERT(title);

	if (title)
		wcscpy_s(m_title, title);
}

void CBrowseFolder::SetCheckBoxText(LPCWSTR checktext)
{
	ASSERT(checktext);

	if (checktext)
		m_CheckText = checktext;
}

void CBrowseFolder::SetCheckBoxText2(LPCWSTR checktext)
{
	ASSERT(checktext);

	if (checktext)
		m_CheckText2 = checktext;
}
}

export using BrowseFolder::CBrowseFolder;