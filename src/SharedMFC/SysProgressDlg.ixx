module;
#include "stdafx.h"


export module SysProgressDlg;
/**
 * \ingroup Utils
 * A wrapper class for the IProgressDialog interface. Thats
 * the dialog used by the shell/IE to show progress in e.g.
 * copying, downloading, ...
 *
 * \remark you need to call AfxOleInit() before using this class, preferably in
 * your app's InitInistance() method.
 */
export class CSysProgressDlg
{
public:
	CSysProgressDlg();
	~CSysProgressDlg();

	// prevent cloning
	CSysProgressDlg(const CSysProgressDlg&) = delete;
	CSysProgressDlg& operator=(const CSysProgressDlg&) = delete;

	/**
	 * sets the title of the progress dialog box.
	 * \param szTitle pointer to a nullptr-terminated string that contains the dialog box title
	 */
	void SetTitle(LPCWSTR szTitle);
	/**
	* sets the title of the progress dialog box to a string resource value.
	*/
	void SetTitle ( UINT idTitle);

	/**
	 * Displays a message.
	 * \param dwLine line number on which the text is to be displayed. There are three lines possible, two lines if SetCalculateTime() is set to true.
	 * \param szText nullptr-terminated string that contains the line text.
	 * \param bCompactPath set to true if you want the text to be compacted (if it is a path) to fit on the line.
	 *
	 * \remark This call should be made *after* the dialog has been shown - this allows
	 * the system to measure the space available for the text, and do path compaction properly
	 */
	void SetLine(DWORD dwLine, LPCWSTR szText, bool bCompactPath = false);

#ifdef _MFC_VER
	/**
	* Wrappers around set line, to do a CString::Format type operation
	* See SetLine for more details
	*
	* \remark These calls should be made *after* the dialog has been shown - this allows
	* the system to measure the space available for the text, and do path compaction properly
	*/
	void FormatPathLine ( DWORD dwLine, UINT idFormatText, ...);
	void FormatPathLine(DWORD dwLine, LPCWSTR FormatText, ...);
	void FormatNonPathLine ( DWORD dwLine, UINT idFormatText, ...);
	void FormatNonPathLine(DWORD dwLine, LPCWSTR FormatText, ...);
#endif
	/**
	 * Sets a message to be displayed if the user clicks the cancel button.
	 * \param szMessage pointer to a nullptr-terminated string that contains the message.
	 * \remark Even though the user clicks Cancel, the application cannot immediately
	 * call Stop() to close the dialog box. The application
	 * must wait until the next time it calls HasUserCancelled() to
	 * discover that the user has canceled the operation. Since this delay might be
	 * significant, the progress dialog box provides the user with immediate feedback
	 * by clearing text lines 1 and 2 and displaying the cancel message on line 3.
	 * The message is intended to let the user know that the delay is normal and that
	 * the progress dialog box will be closed shortly. It is typically is set to
	 * something like "Please wait while ...".
	 */
	void SetCancelMsg(LPCWSTR szMessage);
#ifdef _MFC_VER
	void SetCancelMsg ( UINT idMessage );
#endif

	/**
	 * Specifies that the progress dialog should have a line indicating the time remaining to complete.
	 * \param bCalculate false to deactivate the time remaining line.
	 */
	void SetTime ( bool bTime = true );

	/**
	 * Specifies if the progress bar should be shown or not.
	 */
	void SetShowProgressBar ( bool bShow = true );

	/**
	 * Resets the progress dialog box timer to zero.
	 * \remark the timer is used to estimate the remaining time. It is started when your
	 * application calls ShowModal() or ShowModeless(). Unless your application will
	 * start immediately, it should call ResetTimer() just before starting the operation.
	 * This practice ensures that the time estimates will be as accurate as possible.
	 * This method should not be called after the first call to UpdateProgress().
	 */
	void ResetTimer();

	/**
	 * Shows the progress dialog box modal.
	 */
#ifdef _MFC_VER
	HRESULT ShowModal ( CWnd* pwndParent, BOOL immediately = true );
#endif
	HRESULT ShowModal ( HWND hWndParent, BOOL immediately = true );
	/**
	 * Shows the progress dialog box modeless.
	 */
#ifdef _MFC_VER
	HRESULT ShowModeless ( CWnd* pwndParent, BOOL immediately = true );
#endif
	HRESULT ShowModeless ( HWND hWndParent, BOOL immediately = true );

	/**
	 * Stops the progress dialog box and removes it from the screen.
	 */
	void Stop();

	/**
	 * Updates the progress dialog box with the current state of the operation.
	 * \param dwProgress Application-defined value that indicates what proportion of the operation has been completed at the time the method was called
	 * \param dwMax Application-defined value that specifies what value dwCompleted will have when the operation is complete
	 */
	void SetProgress ( DWORD dwProgress, DWORD dwMax );
	/**
	 * Updates the progress dialog box with the current state of the operation.
	 * \param u64Progress Application-defined value that indicates what proportion of the operation has been completed at the time the method was called
	 * \param u64ProgressMax Application-defined value that specifies what value dwCompleted will have when the operation is complete
	 */
	void SetProgress64 ( ULONGLONG u64Progress, ULONGLONG u64ProgressMax );

	/**
	 * Checks whether the user has canceled the operation.
	 */
	bool HasUserCancelled();

	/**
	 * Checks whether this object was created successfully. If the return value is false then
	 * you MUST NOT use the current instance of this class.
	 */
	bool IsValid() const { return m_pIDlg != nullptr; }

	/**
	 * Checks whether the window is shown.
	 */
	bool IsVisible() const { return m_isVisible; }

	/**
	 * After a call to Stop() to hide the progress dialog,
	 * call EnsureValid() to recreate the dialog and fill in the
	 * data again.
	 */
	bool EnsureValid();

protected:
	ATL::CComPtr<IProgressDialog> m_pIDlg;
	bool				m_isVisible = false;
	DWORD				m_dwDlgFlags = PROGDLG_NORMAL;
	HWND m_hWndProgDlg = nullptr;
	HWND m_hWndParent = nullptr;
	HWND m_hWndFocus = nullptr;
};


CSysProgressDlg::CSysProgressDlg()
{
	EnsureValid();
}

CSysProgressDlg::~CSysProgressDlg()
{
	if (IsValid())
	{
		if (m_isVisible)			//still visible, so stop first before destroying
			m_pIDlg->StopProgressDialog();

		m_pIDlg.Release();
		m_hWndProgDlg = nullptr;
	}
}

bool CSysProgressDlg::EnsureValid()
{
	if(IsValid())
		return true;

	HRESULT hr = m_pIDlg.CoCreateInstance(CLSID_ProgressDialog, nullptr, CLSCTX_INPROC_SERVER);
	return (SUCCEEDED(hr));
}

void CSysProgressDlg::SetTitle(LPCWSTR szTitle)
{
	USES_CONVERSION;
	if (IsValid())
		m_pIDlg->SetTitle(T2COLE(szTitle));
}
void CSysProgressDlg::SetTitle ( UINT idTitle)
{
	SetTitle(CString(MAKEINTRESOURCE(idTitle)));
}

void CSysProgressDlg::SetLine(DWORD dwLine, LPCWSTR szText, bool bCompactPath /* = false */)
{
	USES_CONVERSION;
	if (IsValid())
		m_pIDlg->SetLine(dwLine, T2COLE(szText), bCompactPath, nullptr);
}

#ifdef _MFC_VER
void CSysProgressDlg::SetCancelMsg ( UINT idMessage )
{
	SetCancelMsg(CString(MAKEINTRESOURCE(idMessage)));
}
#endif // _MFC_VER

void CSysProgressDlg::SetCancelMsg(LPCWSTR szMessage)
{
	USES_CONVERSION;
	if (IsValid())
		m_pIDlg->SetCancelMsg(T2COLE(szMessage), nullptr);
}

void CSysProgressDlg::SetTime(bool bTime /* = true */)
{
	m_dwDlgFlags &= ~(PROGDLG_NOTIME | PROGDLG_AUTOTIME);

	if (bTime)
		m_dwDlgFlags |= PROGDLG_AUTOTIME;
	else
		m_dwDlgFlags |= PROGDLG_NOTIME;
}

void CSysProgressDlg::SetShowProgressBar(bool bShow /* = true */)
{
	if (bShow)
		m_dwDlgFlags &= ~PROGDLG_NOPROGRESSBAR;
	else
		m_dwDlgFlags |= PROGDLG_NOPROGRESSBAR;
}
#ifdef _MFC_VER
HRESULT CSysProgressDlg::ShowModal (CWnd* pwndParent, BOOL immediately /* = true */)
{
	EnsureValid();
	return ShowModal(pwndParent->GetSafeHwnd(), immediately);
}

HRESULT CSysProgressDlg::ShowModeless(CWnd* pwndParent, BOOL immediately)
{
	EnsureValid();
	return ShowModeless(pwndParent->GetSafeHwnd(), immediately);
}

void CSysProgressDlg::FormatPathLine ( DWORD dwLine, UINT idFormatText, ...)
{
	va_list args;
	va_start(args, idFormatText);

	CString sText;
	sText.FormatV(CString(MAKEINTRESOURCE(idFormatText)), args);
	SetLine(dwLine, sText, true);

	va_end(args);
}

void CSysProgressDlg::FormatPathLine(DWORD dwLine, LPCWSTR FormatText, ...)
{
	va_list args;
	va_start(args, FormatText);

	CString sText;
	sText.FormatV(FormatText, args);
	SetLine(dwLine, sText, true);

	va_end(args);
}

void CSysProgressDlg::FormatNonPathLine(DWORD dwLine, UINT idFormatText, ...)
{
	va_list args;
	va_start(args, idFormatText);

	CString sText;
	sText.FormatV(CString(MAKEINTRESOURCE(idFormatText)), args);
	SetLine(dwLine, sText, false);

	va_end(args);
}

void CSysProgressDlg::FormatNonPathLine(DWORD dwLine, LPCWSTR FormatText, ...)
{
	va_list args;
	va_start(args, FormatText);

	CString sText;
	sText.FormatV(FormatText, args);
	SetLine(dwLine, sText, false);

	va_end(args);
}

#endif
HRESULT CSysProgressDlg::ShowModal(HWND hWndParent, BOOL immediately /* = true */)
{
	EnsureValid();
	m_hWndProgDlg = nullptr;
	if (!IsValid())
		return E_FAIL;
	m_hWndParent = hWndParent;
	auto winId = GetWindowThreadProcessId(m_hWndParent, nullptr);
	auto threadId = GetCurrentThreadId();
	if (winId != threadId)
		AttachThreadInput(winId, threadId, TRUE);
	m_hWndFocus = GetFocus();
	if (winId != threadId)
		AttachThreadInput(winId, threadId, FALSE);
	HRESULT hr = m_pIDlg->StartProgressDialog(hWndParent, nullptr, m_dwDlgFlags | PROGDLG_MODAL, nullptr);
	if(FAILED(hr))
		return hr;

	ATL::CComPtr<IOleWindow> pOleWindow;
	HRESULT hr2 = m_pIDlg.QueryInterface(&pOleWindow);
	if(SUCCEEDED(hr2))
	{
		hr2 = pOleWindow->GetWindow(&m_hWndProgDlg);
		if(SUCCEEDED(hr2))
		{
			if(immediately)
				ShowWindow(m_hWndProgDlg, SW_SHOW);
		}
	}

	m_isVisible = true;
	return hr;
}

HRESULT CSysProgressDlg::ShowModeless(HWND hWndParent, BOOL immediately)
{
	EnsureValid();
	m_hWndProgDlg = nullptr;
	if (!IsValid())
		return E_FAIL;
	m_hWndParent = hWndParent;
	auto winId = GetWindowThreadProcessId(m_hWndParent, nullptr);
	auto threadId = GetCurrentThreadId();
	if (winId != threadId)
		AttachThreadInput(winId, threadId, TRUE);
	m_hWndFocus = GetFocus();
	if (winId != threadId)
		AttachThreadInput(winId, threadId, FALSE);
	HRESULT hr = m_pIDlg->StartProgressDialog(hWndParent, nullptr, m_dwDlgFlags, nullptr);
	if(FAILED(hr))
		return hr;

	ATL::CComPtr<IOleWindow> pOleWindow;
	HRESULT hr2 = m_pIDlg.QueryInterface(&pOleWindow);
	if(SUCCEEDED(hr2))
	{
		hr2 = pOleWindow->GetWindow(&m_hWndProgDlg);
		if(SUCCEEDED(hr2))
		{
			if (immediately)
				ShowWindow(m_hWndProgDlg, SW_SHOW);
		}
	}
	m_isVisible = true;
	return hr;
}

void CSysProgressDlg::SetProgress(DWORD dwProgress, DWORD dwMax)
{
	if (IsValid())
		m_pIDlg->SetProgress(dwProgress, dwMax);
}

void CSysProgressDlg::SetProgress64(ULONGLONG u64Progress, ULONGLONG u64ProgressMax)
{
	if (IsValid())
		m_pIDlg->SetProgress64(u64Progress, u64ProgressMax);
}

bool CSysProgressDlg::HasUserCancelled()
{
	if (!IsValid())
		return false;

	return (0 != m_pIDlg->HasUserCancelled());
}

void CSysProgressDlg::Stop()
{
	if ((m_isVisible)&&(IsValid()))
	{
		m_pIDlg->StopProgressDialog();
		// Sometimes the progress dialog sticks around after stopping it,
		// until the mouse pointer is moved over it or some other triggers.
		// We hide the window here immediately.
		if (m_hWndProgDlg)
		{
			// The progress dialog is handled on a separate thread, which means
			// even calling StopProgressDialog() will not stop it immediately.
			// Even destroying the progress window is not enough.
			// Which can cause problems with modality: if we stop the progress
			// dialog and immediately show e.g. a messagebox over the same
			// window the progress dialog has as its parent, then the messagebox
			// is modal first (deactivates the parent), but then a little bit
			// later the progress dialog finally closes properly, and by doing that
			// re-enables its parent window.
			// Which leads to the parent window being enabled even though
			// the modal messagebox is shown over the parent window.
			// This situation can even lead to the messagebox appearing *behind*
			// the parent window (race condition)
			//
			// So, to really ensure that the progress dialog is fully stopped
			// and destroyed, we have to attach to its UI thread and handle
			// all messages until there are no more messages: that's when
			// the progress dialog is really gone.
			AttachThreadInput(GetWindowThreadProcessId(m_hWndProgDlg, 0), GetCurrentThreadId(), TRUE);
			// StartProgressDialog creates a new thread to host the progress window.
			// When the window receives WM_DESTROY message StopProgressDialog() wrongly
			// attempts to re-enable the parent in the calling thread (our thread),
			// after the progress window is destroyed and the progress thread has died.
			// When the progress window dies, the system tries to assign a new foreground window.
			// It cannot assign to hwndParent because StartProgressDialog (w/PROGDLG_MODAL) disabled the parent window.
			// So the system hands the foreground activation to the next process that wants it in the
			// system foreground queue. Thus we lose our right to recapture the foreground window.
			// To fix this problem, we enable the parent window and set to focus to it here, after
			// we've attached to the window thread.
			ShowWindow(m_hWndProgDlg, SW_HIDE);
			EnableWindow(m_hWndParent, TRUE);
			if (m_hWndFocus)
				SetFocus(m_hWndFocus);
			else
				SetFocus(m_hWndParent);
			auto start = GetTickCount64();
			while (::IsWindow(m_hWndProgDlg) && ((GetTickCount64() - start) < 3000))
			{
				MSG msg = { 0 };
				while (PeekMessage(&msg, m_hWndProgDlg, 0, 0, PM_REMOVE))
				{
				}
			}
		}
		m_isVisible = false;
		m_pIDlg.Release();

		m_hWndProgDlg = nullptr;
	}
}

void CSysProgressDlg::ResetTimer()
{
	if (IsValid())
		m_pIDlg->Timer(PDTIMER_RESET, nullptr);
}
