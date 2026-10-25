module;
#include "stdafx.h"
#include <afxtaskdialog.h>

#include "Resources/resource.h"
#include "Utils/dlgtemplate.h"
#include "Utils/cursor.h"
#include "Utils/DPIAware.h"
#include "../libgit2/Utils/LoadIconEx.h"

#define BTN_OFFSET 100 // use an offset in order to not interfere with IDYES and so on...

#define IDC_MESSAGEBOX_BUTTON1 101
#define IDC_MESSAGEBOX_BUTTON2 102
#define IDC_MESSAGEBOX_BUTTON3 103
#define IDC_MESSAGEBOX_CHECKBOX 104

#define MESSAGEBOX_BUTTONMARGIN 5
#define MESSAGEBOX_ICONMARGIN 15
#define MESSAGEBOX_BORDERMARGINX 10
#define MESSAGEBOX_BORDERMARGINY 10
#define MESSAGEBOX_TEXTBUTTONMARGIN 15
#define MESSAGEBOX_BUTTONCHECKMARGIN 5
#define MESSAGEBOX_BUTTONX 20
#define MESSAGEBOX_BUTTONY 5


export module MessageBox;
import DebugOutput;
import wil;
import str_utils;

/**
 * \ingroup Utils
 * Implements an enhanced MessageBox().\n
 *
 * You can use CMessageBox::Show() as a replacement for the Platform SDK version of
 * MessageBox(). Most of the flags are supported (param uType):\n
 * To indicate the buttons displayed in the message box, specify one of the following values:
 * <TABLE>
 * <TR VALIGN="top">
 * <TH align=left width=39%>Value</TH>
 * <TH align=left width=61%>Meaning</TH>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_ABORTRETRYIGNORE</TD>
 * <TD width=61%>The message box contains three push buttons: <B>Abort</B>, <B>Retry</B>, and <B>Ignore</B>.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_CANCELTRYCONTINUE</TD>
 * <TD width=61%>The message box contains three push buttons: <B>Cancel</B>, <B>Try Again</B>, <B>Continue</B>. Use this message box type instead of MB_ABORTRETRYIGNORE.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_OK</TD>
 * <TD width=61%>The message box contains one push button: <B>OK</B>. This is the default.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_OKCANCEL</TD>
 * <TD width=61%>The message box contains two push buttons: <B>OK</B> and <B>Cancel</B>.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_RETRYCANCEL</TD>
 * <TD width=61%>The message box contains two push buttons: <B>Retry</B> and <B>Cancel</B>.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_YESNO</TD>
 * <TD width=61%>The message box contains two push buttons: <B>Yes</B> and <B>No</B>.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_YESNOCANCEL</TD>
 * <TD width=61%>The message box contains three push buttons: <B>Yes</B>, <B>No</B>, and <B>Cancel</B>.</TD>
 * </TR>
 * </TABLE>
 * To display an icon in the message box, specify one of the following values
 * <TABLE>
 *
 * <TR VALIGN="top">
 * <TH align=left width=38%>Value</TH>
 * <TH align=left width=62%>Meaning</TH>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=38%>MB_ICONEXCLAMATION, <BR>
 * MB_ICONWARNING</TD>
 * <TD width=62%>An exclamation-point icon appears in the message box.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=38%>MB_ICONINFORMATION, MB_ICONASTERISK</TD>
 * <TD width=62%>An icon consisting of a lowercase letter <I>i</I> in a circle appears in the message box.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=38%>MB_ICONQUESTION</TD>
 * <TD width=62%>A question-mark icon appears in the message box.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=38%>MB_ICONSTOP, <BR>
 * MB_ICONERROR, <BR>
 * MB_ICONHAND</TD>
 * <TD width=62%>A stop-sign icon appears in the message box.</TD>
 * </TR>
 * </TABLE>
 * To indicate the default button, specify one of the following values
 * <TABLE>
 *
 * <TR VALIGN="top">
 * <TH align=left width=39%>Value</TH>
 * <TH align=left width=61%>Meaning</TH>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_DEFBUTTON1</TD>
 * <TD width=61%>The first button is the default button.
 * <P>MB_DEFBUTTON1 is the default unless MB_DEFBUTTON2, MB_DEFBUTTON3, or MB_DEFBUTTON4 is specified.</P>
 * </TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_DEFBUTTON2</TD>
 * <TD width=61%>The second button is the default button.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_DEFBUTTON3</TD>
 * <TD width=61%>The third button is the default button.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=39%>MB_DEFBUTTON4</TD>
 * <TD width=61%>The fourth button is the default button.</TD>
 * </TR>
 * </TABLE>
 *
 * <b>return values:</b>\n
 * <TABLE>
 *
 * <TR VALIGN="top">
 * <TH align=left width=22%>Value</TH>
 * <TH align=left width=78%>Meaning</TH>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=22%>IDABORT</TD>
 * <TD width=78%><B>Abort</B> button was selected.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=22%>IDCANCEL</TD>
 * <TD width=78%><B>Cancel</B> button was selected.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=22%>IDCONTINUE</TD>
 * <TD width=78%><B>Continue</B> button was selected.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=22%>IDIGNORE</TD>
 * <TD width=78%><B>Ignore</B> button was selected.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=22%>IDNO</TD>
 * <TD width=78%><B>No</B> button was selected.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=22%>IDOK</TD>
 * <TD width=78%><B>OK</B> button was selected.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=22%>IDRETRY</TD>
 * <TD width=78%><B>Retry</B> button was selected.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=22%>IDTRYAGAIN</TD>
 * <TD width=78%><B>Try Again</B> button was selected.</TD>
 * </TR>
 *
 * <TR VALIGN="top">
 * <TD width=22%>IDYES</TD>
 * <TD width=78%><B>Yes</B> button was selected.</TD>
 * </TR>
 * </TABLE>
 * If a message box has a <B>Cancel</B> button, the function returns the IDCANCEL value
 * if either the ESC key is pressed or the <B>Cancel</B> button is selected. If the
 * message box has no <B>Cancel</B> button, pressing ESC has no effect.
 *
 * To get I18L you can define the following strings in your resource string table:\n
 * - IDS_MSGBOX_ABORT
 * - IDS_MSGBOX_RETRY
 * - IDS_MSGBOX_IGNORE
 * - IDS_MSGBOX_CANCEL
 * - IDS_MSGBOX_TRYAGAIN
 * - IDS_MSGBOX_CONTINUE
 * - IDS_MSGBOX_OK
 * - IDS_MSGBOX_YES
 * - IDS_MSGBOX_NO
 * - IDS_MSGBOX_DONOTSHOWAGAIN
 *
 * It is also possible to set the button texts for each one of the three buttons.
 * Use the corresponding method provided for this.
 * \image html "CMessageBox_3.jpg"
 *
 * For the "Do not show again" functionality you can define the registry base path
 * for storing the return values with\n
 * \code
 * #define XMESSAGEBOX_APPREGPATH "Software\\MyApplication\\MsgDialogs\\"
 * \endcode
 * if you don't do that define then CMessageBox will use the default path "Software\\YourApplicationName\\"
 */
export class CMessageBox : public CDialog
{
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg void OnButton1();
	afx_msg void OnButton2();
	afx_msg void OnButton3();

public:
	CMessageBox() = default;
	~CMessageBox() override
	{
		if (m_bDestroyIcon)
			::DestroyIcon(m_hIcon);
	}
	/**
	 * Shows a message box. Use this as a replacement for the usual ::MessageBox() calls.
	 * Most of the flags of the Platform SDK version are supported. See the class descriptions
	 * for details.
	 * \param hWnd handle to the parent window or nullptr
	 * \param lpMessage the message string to show on the message box
	 * \param lpCaption the dialog title
	 * \param uType see class description for details
	 * \param sHelpPath if uType has MB_HELP, the path for the help file
	 * \return see class descriptions for details
	 */
	static UINT Show(HWND hWnd, LPCWSTR lpMessage, LPCWSTR lpCaption, UINT uType, LPCWSTR sHelpPath = nullptr);
	/**
	 * Shows a message box.
	 * \param hWnd handle to the parent window or nullptr
	 * \param nMessage resource ID of the message string
	 * \param nCaption resource ID of the title string
	 * \param uType see class description for details
	 * \param sHelpPath if uType has MB_HELP, this is the path of the help file to use
	 * \return see class description for details
	 */
	static UINT Show(HWND hWnd, UINT nMessage, UINT nCaption, UINT uType, LPCWSTR sHelpPath = nullptr);
	/**
	* Shows a message box.
	* \param hWnd handle to the parent window or nullptr
	* \param nMessage resource ID of the message string
	* \param nCaption resource ID of the title string
	* \param uType see class description for details
	* \param nHelpID if uType has MB_HELP, this is the help ID to use
	* \return see class description for details
	*/
	static UINT Show(HWND hWnd, UINT nMessage, UINT nCaption, UINT uType, UINT nHelpID);
	/**
	 * Shows a message box with a checkbox. If the user checks it then the next time
	 * the message box isn't shown anymore - the method simply returns the same value as
	 * the last time. Use this to give the user the possibility to ignore specific message
	 * boxes ("Do not show again"-checkbox).
	 * \param hWnd handle to the parent window or nullptr
	 * \param lpMessage the message string to show on the message box
	 * \param lpCaption the dialog title
	 * \param uType see class description for details
	 * \param lpRegistry a value string to store the return value of this specific message box. Put nullptr or empty if you do not use registry.
	 * Each one of your message boxes must have it's own value string! Examples for such values
	 * might be "WarnOverwrite", "InformAboutMissingMailSubject", ...
	 * \param lpCheckMessage the message to show on the checkbox label. If this parameter is omitted
	 * then it defaults to "do not show again" or the string with resource ID IDS_MSGBOX_DONOTSHOWAGAIN.
	 * \param bChecked get and set checkbox check state. This is optional.
	 * \return see class description for details
	 */
	static UINT ShowCheck(HWND hWnd, LPCWSTR lpMessage, LPCWSTR lpCaption, UINT uType, LPCWSTR lpRegistry, LPCWSTR lpCheckMessage = nullptr, BOOL* bChecked = nullptr);
	/**
	 * Shows a message box with a checkbox. If the user checks it then the next time
	 * the message box isn't shown anymore - the method simply returns the same value as
	 * the last time. Use this to give the user the possibility to ignore specific message
	 * boxes ("Do not show again"-checkbox).
	 * \param hWnd handle to the parent window or nullptr
	 * \param lpMessage the message string to show on the message box
	 * \param nCaption resource ID of the title string
	 * \param uType see class description for details
	 * \param lpRegistry a value string to store the return value of this specific message box. Put nullptr or empty if you do not use registry.
	 * Each one of your message boxes must have it's own value string! Examples for such values
	 * might be "WarnOverwrite", "InformAboutMissingMailSubject", ...
	 * \param nCheckMessage resource ID of the checkbox string
	 * \param bChecked get and set checkbox check state. This is optional.
	 * \return see class description for details
	 */
	static UINT ShowCheck(HWND hWnd, LPCWSTR lpMessage, UINT nCaption, UINT uType, LPCWSTR lpRegistry, UINT nCheckMessage, BOOL* bChecked = nullptr);
	/**
	 * Shows a message box with a checkbox. If the user checks it then the next time
	 * the message box isn't shown anymore - the method simply returns the same value as
	 * the last time. Use this to give the user the possibility to ignore specific message
	 * boxes ("Do not show again"-checkbox).
	 * \param hWnd handle to the parent window or nullptr
	 * \param nMessage resource ID of the message string
	 * \param nCaption resource ID of the title string
	 * \param uType see class description for details
	 * \param lpRegistry a value string to store the return value of this specific message box. Put nullptr or empty if you do not use registry.
	 * Each one of your message boxes must have it's own value string! Examples for such values
	 * might be "WarnOverwrite", "InformAboutMissingMailSubject", ...
	 * \param nCheckMessage resource ID of the checkbox string
	 * \param bChecked get and set checkbox check state. This is optional.
	 * \return see class description for details
	 */
	static UINT ShowCheck(HWND hWnd, UINT nMessage, UINT nCaption, UINT uType, LPCWSTR lpRegistry, UINT nCheckMessage, BOOL* bChecked = nullptr);

	/**
	 * Shows a message box with user defined button texts.
	 * \param hWnd handle to the parent window or nullptr
	 * \param lpMessage the message string
	 * \param lpCaption the title string
	 * \param nDef number of the default button (1,2 or 3)
	 * \param icon an icon loaded with MAKEINTRESOURCE() or one of
	 * the system default icons.
	 * \param lpButton1 text for the first button
	 * \param lpButton2 text for the second button
	 * \param lpButton3 text for the third button
	 * \return the number of the button pressed (1,2 or 3)
	 */
	static UINT Show(HWND hWnd, LPCWSTR lpMessage, LPCWSTR lpCaption, int nDef, LPCWSTR icon, LPCWSTR lpButton1, LPCWSTR lpButton2 = nullptr, LPCWSTR lpButton3 = nullptr);
	/**
	 * Shows a message box with user defined button texts.
	 * \param hWnd handle to the parent window or nullptr
	 * \param lpMessage the message string
	 * \param nCaption resource ID of the title string
	 * \param nDef number of the default button (1,2 or 3)
	 * \param icon an icon loaded with MAKEINTRESOURCE() or one of
	 * the system default icons.
	 * \param nButton1 resource ID of the text for the first button
	 * \param nButton2 resource ID of the text for the second button
	 * \param nButton3 resource ID of the text for the third button
	 * \return the number of the button pressed (1,2 or 3)
	 */
	static UINT Show(HWND hWnd, LPCWSTR lpMessage, UINT nCaption, int nDef, LPCWSTR icon, UINT nButton1, UINT nButton2 = NULL, UINT nButton3 = NULL);
	/**
	 * Shows a message box with user defined button texts.
	 * \param hWnd handle to the parent window or nullptr
	 * \param nMessage resource ID of the message string
	 * \param nCaption resource ID of the title string
	 * \param nDef number of the default button (1,2 or 3)
	 * \param icon an icon loaded with MAKEINTRESOURCE() or one of
	 * the system default icons.
	 * \param nButton1 resource ID of the text for the first button
	 * \param nButton2 resource ID of the text for the second button
	 * \param nButton3 resource ID of the text for the third button
	 * \return the number of the button pressed (1,2 or 3)
	 */
	static UINT Show(HWND hWnd, UINT nMessage, UINT nCaption, int nDef, LPCWSTR icon, UINT nButton1, UINT nButton2 = NULL, UINT nButton3 = NULL);
	/**
	 * Shows a message box with user defined button texts and a checkbox.
	 * \param hWnd handle to the parent window or nullptr
	 * \param lpMessage the message string
	 * \param lpCaption the title string
	 * \param nDef number of the default button (1,2 or 3)
	 * \param icon an icon loaded with MAKEINTRESOURCE() or one
	 * of the system default icons
	 * \param lpButton1 string for the first button
	 * \param lpButton2 string for the second button
	 * \param lpButton3 string for the third button
	 * \param lpRegistry  a value string to store the return value of this specific message box. Put nullptr or empty if you do not use registry.
	 * Each one of your message boxes must have it's own value string! Examples for such values
	 * might be "WarnOverwrite", "InformAboutMissingMailSubject", ...
	 * \param lpCheckMessage the message to show on the checkbox label. If this parameter is omitted
	 * then it defaults to "do not show again" or the string with resource ID IDS_MSGBOX_DONOTSHOWAGAIN.
	 * \param bChecked get and set checkbox check state. This is optional.
	 * \return the number of the button pressed (1,2 or 3)
	 */
	static UINT ShowCheck(HWND hWnd, LPCWSTR lpMessage, LPCWSTR lpCaption, int nDef, LPCWSTR icon, LPCWSTR lpButton1, LPCWSTR lpButton2, LPCWSTR lpButton3, LPCWSTR lpRegistry, LPCWSTR lpCheckMessage = nullptr, BOOL* bChecked = nullptr);
	/**
	 * Shows a message box with user defined button texts and a checkbox.
	 * \param hWnd handle to the parent window or nullptr
	 * \param lpMessage the message string
	 * \param nCaption resource ID of the title string
	 * \param nDef number of the default button (1,2 or 3)
	 * \param icon an icon loaded with MAKEINTRESOURCE() or one
	 * of the system default icons
	 * \param nButton1 resource ID of string for the first button
	 * \param nButton2 resource ID of string for the second button
	 * \param nButton3 resource ID of string for the third button
	 * \param lpRegistry  a value string to store the return value of this specific message box. Put nullptr or empty if you do not use registry.
	 * Each one of your message boxes must have it's own value string! Examples for such values
	 * might be "WarnOverwrite", "InformAboutMissingMailSubject", ...
	 * \param nCheckMessage resource ID of the checkbox string
	 * \param bChecked get and set checkbox check state. This is optional.
	 * \return the number of the button pressed (1,2 or 3)
	 */
	static UINT ShowCheck(HWND hWnd, LPCWSTR lpMessage, UINT nCaption, int nDef, LPCWSTR icon, UINT nButton1, UINT nButton2, UINT nButton3, LPCWSTR lpRegistry, UINT nCheckMessage = NULL, BOOL* bChecked = nullptr);

	/**
	 * Shows a message box with user defined button texts and a checkbox.
	 * \param hWnd handle to the parent window or nullptr
	 * \param nMessage resource ID of the message string
	 * \param nCaption resource ID of the title string
	 * \param nDef number of the default button (1,2 or 3)
	 * \param icon an icon loaded with MAKEINTRESOURCE() or one
	 * of the system default icons
	 * \param nButton1 resource ID of string for the first button
	 * \param nButton2 resource ID of string for the second button
	 * \param nButton3 resource ID of string for the third button
	 * \param lpRegistry  a value string to store the return value of this specific message box. Put nullptr or empty if you do not use registry.
	 * Each one of your message boxes must have it's own value string! Examples for such values
	 * might be "WarnOverwrite", "InformAboutMissingMailSubject", ...
	 * \param nCheckMessage resource ID of the checkbox string
	 * \param bChecked get and set checkbox check state. This is optional.
	 * \return the number of the button pressed (1,2 or 3)
	 */
	static UINT ShowCheck(HWND hWnd, UINT nMessage, UINT nCaption, int nDef, LPCWSTR icon, UINT nButton1, UINT nButton2, UINT nButton3, LPCWSTR lpRegistry, UINT nCheckMessage = NULL, BOOL* bChecked = nullptr);

	/**
	 * Removes the registry key
	 */
	static bool RemoveRegistryKey(LPCWSTR lpRegistry);

	/**
	 * Stores the value in the registry
	 * \param sValue the value name
	 * \param value DWORD to store
	 */
	static void SetRegistryValue(const CString& sValue, DWORD value);

	BOOL OnInitDialog() override;

	BOOL PreTranslateMessage(MSG* pMsg) override;

protected:
	void OnCancel() override;
	/**
	 * Shows the modal dialog
	 * \param pWnd handle to the parent window or nullptr
	 * \param title message box title
	 * \param msg message to show
	 * \param nDefaultButton number of the default button
	 * \return the value stored in the member variables for the buttons (m_uButtonXRet)
	 * of the button pressed
	 */
	UINT GoModal(CWnd * pWnd, const CString& title, const CString& msg, int nDefaultButton);

	/**
	 * Fills in the member variables according to the uType parameter
	 */
	int FillBoxStandard(UINT uType);

	/**
	 * Calculates the size of the string in pixels
	 * \param str the string to check the size
	 */
	CSize GetTextSize(const CString& str);
	/**
	 * Returns the size of the icon
	 */
	CSize GetIconSize(HICON hIcon);
	/**
	 * Returns the size of all four buttons (three pushbuttons and the checkbox).
	 * Also resizes the buttons accordingly and fills in m_szAllButtons and m_szButtons.
	 */
	CSize GetButtonSize();

	void SetHelpPath(LPCWSTR sHelpPath) {m_sHelpPath = sHelpPath;}

	LOGFONT m_LogFont{};
	CCursor m_Cursor;
	CString m_sMessage;
	HICON m_hIcon = nullptr;
	BOOL m_bDestroyIcon = FALSE;
	int m_nDefButton = 0;
	CString m_sButton1;
	CString m_sButton2;
	CString m_sButton3;
	CString m_sCheckbox;
	CString m_sHelpPath;
	UINT m_uButton1Ret = 1;
	UINT m_uButton2Ret = 2;
	UINT m_uButton3Ret = 3;
	UINT m_uCancelRet = 0;
	UINT m_uType = 0;
	CSize m_szIcon;
	CSize m_szAllButtons;
	CSize m_szButtons;
	CSize m_szText;

	BOOL m_bShowCheck = FALSE;
	CString m_sRegistryValue;

	CString m_i18l;			//only used if some strings are defined for internationalization
	BOOL m_bChecked = FALSE;
};

static HWND GetMainHWND(HWND hWnd)
{
	if (!hWnd || !IsWindow(hWnd))
		return CWnd::GetSafeOwner_(nullptr, nullptr);
	return hWnd;
}

UINT CMessageBox::ShowCheck(HWND hWnd, LPCWSTR lpMessage, UINT nCaption, int nDef, LPCWSTR icon, UINT nButton1, UINT nButton2, UINT nButton3, LPCWSTR lpRegistry, UINT nCheckMessage /* = nullptr*/, BOOL* bChecked)
{
	CString sButton1;
	CString sButton2;
	CString sButton3;
	CString sCaption;
	CString nCheckMsg;
	sButton1.LoadString(nButton1);
	sButton2.LoadString(nButton2);
	sButton3.LoadString(nButton3);
	sCaption.LoadString(nCaption);
	nCheckMsg.LoadString(nCheckMessage);
	return CMessageBox::ShowCheck(hWnd, lpMessage, sCaption, nDef, icon, sButton1, sButton2, sButton3, lpRegistry, nCheckMsg, bChecked);
}

UINT CMessageBox::ShowCheck(HWND hWnd, UINT nMessage, UINT nCaption, int nDef, LPCWSTR icon, UINT nButton1, UINT nButton2, UINT nButton3, LPCWSTR lpRegistry, UINT nCheckMessage/* = nullptr*/, BOOL* bChecked)
{
	CString sMessage;
	sMessage.LoadString(nMessage);
	return CMessageBox::ShowCheck(hWnd, sMessage, nCaption, nDef, icon, nButton1, nButton2, nButton3, lpRegistry, nCheckMessage, bChecked);
}

UINT CMessageBox::ShowCheck(HWND hWnd, LPCWSTR lpMessage, LPCWSTR lpCaption, int nDef, LPCWSTR icon, LPCWSTR lpButton1, LPCWSTR lpButton2, LPCWSTR lpButton3, LPCWSTR lpRegistry, LPCWSTR lpCheckMessage/* = nullptr*/, BOOL* bChecked)
{
	//check the registry if we have to show the box or just return with the last used return value
	//this would be the case if the user pressed "do not show again".
	DWORD dwRetVal;
	HKEY hKey;
	CString path;
#ifdef XMESSAGEBOX_APPREGPATH
	path = _T(XMESSAGEBOX_APPREGPATH);
#else
	path = L"Software\\TortoiseGit\\";
	path += AfxGetApp()->m_pszProfileName;
#endif
	if (lpRegistry && *lpRegistry && RegOpenKeyEx(HKEY_CURRENT_USER, path, 0, KEY_EXECUTE, &hKey)==ERROR_SUCCESS)
	{
		DWORD size = sizeof(dwRetVal);
		DWORD type;
		if (RegQueryValueEx(hKey, lpRegistry, nullptr, &type, reinterpret_cast<BYTE*>(&dwRetVal), &size) == ERROR_SUCCESS)
		{
			ASSERT(type==REG_DWORD);
			RegCloseKey(hKey);
			TracePrintf(_T(__FUNCTION__) L": Using stored value %ld for \"%s\"\n", dwRetVal, lpMessage);
			return static_cast<UINT>(dwRetVal); //return with the last saved value
		}
		else
		{
			RegCloseKey(hKey);
		}
	}

	if (CTaskDialog::IsSupported())
	{
		CTaskDialog taskdlg(lpMessage, L"", lpCaption, 0, TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW);
		taskdlg.AddCommandControl(BTN_OFFSET + 1, lpButton1);
		if (lpButton2 && *lpButton2)
			taskdlg.AddCommandControl(BTN_OFFSET + 2, lpButton2);
		if (lpButton3 && *lpButton3)
			taskdlg.AddCommandControl(BTN_OFFSET + 3, lpButton3);
		taskdlg.SetDefaultCommandControl(BTN_OFFSET + nDef);
		taskdlg.SetMainIcon(icon);
		if (!lpCheckMessage)
		{
#ifndef IDS_MSGBOX_DONOTSHOWAGAIN
			taskdlg.SetVerificationCheckboxText(L"do not show again");
#else
			CString m_i18l;
			m_i18l.LoadString(IDS_MSGBOX_DONOTSHOWAGAIN);
			taskdlg.SetVerificationCheckboxText(m_i18l);
#endif
		}
		else
			taskdlg.SetVerificationCheckboxText(lpCheckMessage);
		int result = static_cast<int>(taskdlg.DoModal(GetMainHWND(hWnd))) - BTN_OFFSET;
		if (bChecked)
			*bChecked = taskdlg.GetVerificationCheckboxState();
		if (lpRegistry && *lpRegistry && taskdlg.GetVerificationCheckboxState())
			SetRegistryValue(lpRegistry, result);

		return result;
	}

	CMessageBox box;
	box.m_bShowCheck = TRUE;
	box.m_sRegistryValue = lpRegistry;
	if (!lpCheckMessage)
	{
#ifndef IDS_MSGBOX_DONOTSHOWAGAIN
		box.m_sCheckbox = L"do not show again";
#else
		CString m_i18l;
		m_i18l.LoadString(IDS_MSGBOX_DONOTSHOWAGAIN);
		box.m_sCheckbox = m_i18l;
#endif
	}
	else
		box.m_sCheckbox = lpCheckMessage;
	box.m_bChecked = bChecked ? *bChecked : FALSE;
	box.m_sButton1 = lpButton1;
	box.m_sButton2 = lpButton2;
	box.m_sButton3 = lpButton3;
	box.m_hIcon = LoadIconEx(AfxGetResourceHandle(), icon, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
	if (!box.m_hIcon)
		box.m_hIcon = LoadIconEx(nullptr, icon, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
	else
		box.m_bDestroyIcon = TRUE;
	if (!IsWindow(hWnd))
		hWnd = nullptr;
	int result = box.GoModal(CWnd::FromHandle(hWnd), lpCaption, lpMessage, nDef);
	if (bChecked)
		*bChecked = box.m_bChecked;
	return result;
}

UINT CMessageBox::Show(HWND hWnd, LPCWSTR lpMessage, LPCWSTR lpCaption, int nDef, LPCWSTR icon, LPCWSTR lpButton1, LPCWSTR lpButton2/* = nullptr*/, LPCWSTR lpButton3/* = nullptr*/)
{
	if (CTaskDialog::IsSupported())
	{
		CTaskDialog taskdlg(lpMessage, L"", lpCaption, 0, TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW);
		taskdlg.AddCommandControl(BTN_OFFSET + 1, lpButton1);
		if (lpButton2 && *lpButton2)
			taskdlg.AddCommandControl(BTN_OFFSET + 2, lpButton2);
		if (lpButton3 && *lpButton3)
			taskdlg.AddCommandControl(BTN_OFFSET + 3, lpButton3);
		taskdlg.SetDefaultCommandControl(BTN_OFFSET + nDef);
		taskdlg.SetMainIcon(icon);
		return static_cast<UINT>(taskdlg.DoModal(GetMainHWND(hWnd))) - BTN_OFFSET;
	}
	CMessageBox box;
	box.m_sButton1 = lpButton1;
	box.m_sButton2 = lpButton2;
	box.m_sButton3 = lpButton3;
	box.m_hIcon = LoadIconEx(AfxGetResourceHandle(), icon, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
	if (!box.m_hIcon)
		box.m_hIcon = LoadIconEx(nullptr, icon, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
	else
		box.m_bDestroyIcon = TRUE;
	if (!IsWindow(hWnd))
		hWnd = nullptr;
	return box.GoModal(CWnd::FromHandle(hWnd), lpCaption, lpMessage, nDef);
}

UINT CMessageBox::Show(HWND hWnd, LPCWSTR lpMessage, UINT nCaption, int nDef, LPCWSTR icon, UINT nButton1, UINT nButton2, UINT nButton3)
{
	CString sButton1;
	CString sButton2;
	CString sButton3;
	CString sCaption;
	sButton1.LoadString(nButton1);
	sButton2.LoadString(nButton2);
	sButton3.LoadString(nButton3);
	sCaption.LoadString(nCaption);
	return CMessageBox::Show(hWnd, lpMessage, sCaption, nDef, icon, sButton1, sButton2, sButton3);
}

UINT CMessageBox::Show(HWND hWnd, UINT nMessage, UINT nCaption, int nDef, LPCWSTR icon, UINT nButton1, UINT nButton2, UINT nButton3)
{
	CString sMessage;
	sMessage.LoadString(nMessage);
	return CMessageBox::Show(hWnd, sMessage, nCaption, nDef, icon, nButton1, nButton2, nButton3);
}

UINT CMessageBox::ShowCheck(HWND hWnd, UINT nMessage, UINT nCaption, UINT uType, LPCWSTR lpRegistry, UINT nCheckMessage, BOOL* bChecked)
{
	CString sMessage;
	sMessage.LoadString(nMessage);
	return CMessageBox::ShowCheck(hWnd, sMessage, nCaption, uType, lpRegistry, nCheckMessage, bChecked);
}

UINT CMessageBox::ShowCheck(HWND hWnd, LPCWSTR lpMessage, UINT nCaption, UINT uType, LPCWSTR lpRegistry, UINT nCheckMessage, BOOL* bChecked /* = nullptr*/)
{
	CString sCaption;
	CString sCheckMsg;
	sCaption.LoadString(nCaption);
	sCheckMsg.LoadString(nCheckMessage);
	return CMessageBox::ShowCheck(hWnd, lpMessage, sCaption, uType, lpRegistry, sCheckMsg, bChecked);
}

UINT CMessageBox::ShowCheck(HWND hWnd, LPCWSTR lpMessage, LPCWSTR lpCaption, UINT uType, LPCWSTR lpRegistry, LPCWSTR lpCheckMessage, BOOL* bChecked)
{
	//check the registry if we have to show the box or just return with the last used return value
	//this would be the case if the user pressed "do not show again".
	DWORD dwRetVal;
	HKEY hKey;
	CString path;
#ifdef XMESSAGEBOX_APPREGPATH
	path = _T(XMESSAGEBOX_APPREGPATH);
#else
	path = L"Software\\TortoiseGit\\";
	path += AfxGetApp()->m_pszProfileName;
#endif
	if (lpRegistry && *lpRegistry && RegOpenKeyEx(HKEY_CURRENT_USER, path, 0, KEY_EXECUTE, &hKey)==ERROR_SUCCESS)
	{
		DWORD size = sizeof(dwRetVal);
		DWORD type;
		if (RegQueryValueEx(hKey, lpRegistry, nullptr, &type, reinterpret_cast<BYTE*>(&dwRetVal), &size) == ERROR_SUCCESS)
		{
			ASSERT(type==REG_DWORD);
			RegCloseKey(hKey);
			TracePrintf(_T(__FUNCTION__) L": Using stored value %ld for \"%s\"\n", dwRetVal, lpMessage);
			return static_cast<UINT>(dwRetVal); // return with the last saved value
		}
		else
		{
			RegCloseKey(hKey);
		}
	}
	if (CTaskDialog::IsSupported() && !(uType & MB_HELP) && !((uType & 0xf) == MB_ABORTRETRYIGNORE) && !((uType & 0xf) == MB_CANCELTRYCONTINUE))
	{
		CTaskDialog taskdlg(lpMessage, L"", lpCaption, 0,  TDF_POSITION_RELATIVE_TO_WINDOW);
		// set up icon
		switch (uType & 0xf0)
		{
		case MB_ICONEXCLAMATION:
			taskdlg.SetMainIcon(IDI_EXCLAMATION);
			break;
		case MB_ICONASTERISK:
			taskdlg.SetMainIcon(IDI_ASTERISK);
			break;
		case MB_ICONQUESTION:
			taskdlg.SetMainIcon(IDI_QUESTION);
			break;
		case MB_ICONHAND:
			taskdlg.SetMainIcon(IDI_HAND);
			break;
		}
		// set up the buttons
		switch (uType & 0xf)
		{
		case MB_ABORTRETRYIGNORE:
			ASSERT(FALSE);
			break;
		case MB_CANCELTRYCONTINUE:
			ASSERT(FALSE);
			break;
		case MB_OKCANCEL:
			taskdlg.SetCommonButtons(TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON);
			switch (uType & 0xf00)
			{
			case MB_DEFBUTTON2:
				taskdlg.SetDefaultCommandControl(IDCANCEL);
				break;
			}
			break;
		case MB_RETRYCANCEL:
			taskdlg.SetCommonButtons(TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON);
			switch (uType & 0xf00)
			{
			case MB_DEFBUTTON2:
				taskdlg.SetDefaultCommandControl(IDCANCEL);
				break;
			}
			break;
		case MB_YESNO:
			taskdlg.SetCommonButtons(TDCBF_YES_BUTTON | TDCBF_NO_BUTTON);
			switch (uType & 0xf00)
			{
			case MB_DEFBUTTON2:
				taskdlg.SetDefaultCommandControl(IDNO);
				break;
			}
			break;
		case MB_YESNOCANCEL:
			taskdlg.SetCommonButtons(TDCBF_YES_BUTTON | TDCBF_NO_BUTTON | TDCBF_CANCEL_BUTTON);
			switch (uType & 0xf00)
			{
			case MB_DEFBUTTON2:
				taskdlg.SetDefaultCommandControl(IDNO);
				break;
			case MB_DEFBUTTON3:
				taskdlg.SetDefaultCommandControl(IDCANCEL);
				break;
			}
			break;
		case MB_OK:
		default:
			taskdlg.SetCommonButtons(TDCBF_OK_BUTTON);
		}

		if (!lpCheckMessage)
		{
#ifndef IDS_MSGBOX_DONOTSHOWAGAIN
			taskdlg.SetVerificationCheckboxText(L"do not show again");
#else
			CString m_i18l;
			m_i18l.LoadString(IDS_MSGBOX_DONOTSHOWAGAIN);
			taskdlg.SetVerificationCheckboxText(m_i18l);
#endif
		}
		else
			taskdlg.SetVerificationCheckboxText(lpCheckMessage);
		int result = static_cast<int>(taskdlg.DoModal(GetMainHWND(hWnd)));
		if (bChecked)
			*bChecked = taskdlg.GetVerificationCheckboxState();
		if (lpRegistry && *lpRegistry && taskdlg.GetVerificationCheckboxState())
			SetRegistryValue(lpRegistry, result);

		return result;
	}

	CMessageBox box;
	box.m_bShowCheck = TRUE;
	box.m_sRegistryValue = lpRegistry;
	if (!lpCheckMessage)
	{
#ifndef IDS_MSGBOX_DONOTSHOWAGAIN
		box.m_sCheckbox = L"do not show again";
#else
		CString m_i18l;
		m_i18l.LoadString(IDS_MSGBOX_DONOTSHOWAGAIN);
		box.m_sCheckbox = m_i18l;
#endif
	}
	else
		box.m_sCheckbox = lpCheckMessage;
	box.m_bChecked = bChecked ? *bChecked : FALSE;
	if (!IsWindow(hWnd))
		hWnd = nullptr;
	int result = box.GoModal(CWnd::FromHandle(hWnd), lpCaption, lpMessage, box.FillBoxStandard(uType));
	if (bChecked)
		*bChecked = box.m_bChecked;
	return result;
}

UINT CMessageBox::Show(HWND hWnd, UINT nMessage, UINT nCaption, UINT uType, LPCWSTR sHelpPath)
{
	CString sMessage;
	CString sCaption;
	sMessage.LoadString(nMessage);
	sCaption.LoadString(nCaption);
	return CMessageBox::Show(hWnd, sMessage, sCaption, uType, sHelpPath);
}

UINT CMessageBox::Show(HWND hWnd, LPCWSTR lpMessage, LPCWSTR lpCaption, UINT uType, LPCWSTR sHelpPath)
{
	if (!IsWindow(hWnd))
		hWnd = nullptr;
	if (sHelpPath)
	{
		CMessageBox box;
		box.SetHelpPath(sHelpPath);
		return box.GoModal(CWnd::FromHandle(hWnd), lpCaption, lpMessage, box.FillBoxStandard(uType));
	}

	return ::MessageBox(GetMainHWND(hWnd), lpMessage, lpCaption, uType);
}

UINT CMessageBox::Show(HWND hWnd, UINT nMessage, UINT nCaption, UINT uType, UINT nHelpID)
{
	CString sMessage;
	CString sCaption;
	sMessage.LoadString(nMessage);
	sCaption.LoadString(nCaption);

	if (!IsWindow(hWnd))
		hWnd = nullptr;
	if (nHelpID)
	{
		CMessageBox box;
		box.SetHelpID(nHelpID);
		return box.GoModal(CWnd::FromHandle(hWnd), sCaption, sMessage, box.FillBoxStandard(uType));
	}

	return ::MessageBox(GetMainHWND(hWnd), sMessage, sCaption, uType);
}

bool CMessageBox::RemoveRegistryKey(LPCWSTR lpRegistry)
{
	HKEY hKey;
	CString path;
#ifdef XMESSAGEBOX_APPREGPATH
	path = _T(XMESSAGEBOX_APPREGPATH);
#else
	path = L"Software\\TortoiseGit\\";
	path += AfxGetApp()->m_pszProfileName;
#endif
	if (RegOpenKeyEx(HKEY_CURRENT_USER, path, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
	{
		bool ret = !!RegDeleteValue(hKey, lpRegistry);
		RegCloseKey(hKey);
		return ret;
	}
	return false;
}

int CMessageBox::FillBoxStandard(UINT uType)
{
	int ret = 1;
	m_uType = uType;
	m_uCancelRet = IDCANCEL;
	//load the icons according to uType
	switch (uType & 0xf0)
	{
	case MB_ICONEXCLAMATION:
		m_hIcon = LoadIconEx(nullptr, IDI_EXCLAMATION, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
		::MessageBeep(MB_ICONEXCLAMATION);
		break;
	case MB_ICONASTERISK:
		m_hIcon = LoadIconEx(nullptr, IDI_ASTERISK, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
		::MessageBeep(MB_ICONASTERISK);
		break;
	case MB_ICONQUESTION:
		m_hIcon = LoadIconEx(nullptr, IDI_QUESTION, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
		::MessageBeep(MB_ICONQUESTION);
		break;
	case MB_ICONHAND:
		m_hIcon = LoadIconEx(nullptr, IDI_HAND, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
		::MessageBeep(MB_ICONHAND);
		break;
	}
	//set up the button texts
	switch (uType & 0xf)
	{
	case MB_ABORTRETRYIGNORE:
#ifndef IDS_MSGBOX_ABORT
		m_sButton1 = L"&Abort";
#else
		m_i18l.LoadString(IDS_MSGBOX_ABORT);
		m_sButton1 = m_i18l;
#endif
		m_uButton1Ret = IDABORT;
#ifndef IDS_MSGBOX_RETRY
		m_sButton2 = L"&Retry";
#else
		m_i18l.LoadString(IDS_MSGBOX_RETRY);
		m_sButton2 = m_i18l;
#endif
		m_uButton2Ret = IDRETRY;
#ifndef IDS_MSGBOX_IGNORE
		m_sButton3 = L"&Ignore";
#else
		m_i18l.LoadString(IDS_MSGBOX_IGNORE);
		m_sButton3 = m_i18l;
#endif
		m_uButton3Ret = IDIGNORE;
		break;
	case MB_CANCELTRYCONTINUE:
#ifndef IDS_MSGBOX_CANCEL
		m_sButton1 = L"Cancel";
#else
		m_i18l.LoadString(IDS_MSGBOX_CANCEL);
		m_sButton1 = m_i18l;
#endif
		m_uButton1Ret = IDCANCEL;
#ifndef IDS_MSGBOX_TRYAGAIN
		m_sButton2 = L"&Try Again";
#else
		m_i18l.LoadString(IDS_MSGBOX_TRYAGAIN);
		m_sButton2 = m_i18l;
#endif
		m_uButton2Ret = IDTRYAGAIN;
#ifndef IDS_MSGBOX_CONTINUE
		m_sButton3 = L"&Continue";
#else
		m_i18l.LoadString(IDS_MSGBOX_CONTINUE);
		m_sButton3 = m_i18l;
#endif
		m_uButton3Ret = IDCONTINUE;
		break;
	case MB_OKCANCEL:
#ifndef IDS_MSGBOX_OK
		m_sButton1 = L"OK";
#else
		m_i18l.LoadString(IDS_MSGBOX_OK);
		m_sButton1 = m_i18l;
#endif
		m_uButton1Ret = IDOK;
#ifndef IDS_MSGBOX_CANCEL
		m_sButton2 = L"Cancel";
#else
		m_i18l.LoadString(IDS_MSGBOX_CANCEL);
		m_sButton2 = m_i18l;
#endif
		m_uButton2Ret = IDCANCEL;
		break;
	case MB_RETRYCANCEL:
#ifndef IDS_MSGBOX_RETRY
		m_sButton1 = L"&Retry";
#else
		m_i18l.LoadString(IDS_MSGBOX_RETRY);
		m_sButton1 = m_i18l;
#endif
		m_uButton1Ret = IDRETRY;
#ifndef IDS_MSGBOX_CANCEL
		m_sButton2 = L"Cancel";
#else
		m_i18l.LoadString(IDS_MSGBOX_CANCEL);
		m_sButton2 = m_i18l;
#endif
		m_uButton2Ret = IDCANCEL;
		break;
	case MB_YESNO:
#ifndef IDS_MSGBOX_YES
		m_sButton1 = L"&Yes";
#else
		m_i18l.LoadString(IDS_MSGBOX_YES);
		m_sButton1 = m_i18l;
#endif
		m_uButton1Ret = IDYES;
#ifndef IDS_MSGBOX_NO
		m_sButton2 = L"&No";
#else
		m_i18l.LoadString(IDS_MSGBOX_NO);
		m_sButton2 = m_i18l;
#endif
		m_uButton2Ret = IDNO;
		break;
	case MB_YESNOCANCEL:
#ifndef IDS_MSGBOX_YES
		m_sButton1 = L"&Yes";
#else
		m_i18l.LoadString(IDS_MSGBOX_YES);
		m_sButton1 = m_i18l;
#endif
		m_uButton1Ret = IDYES;
#ifndef IDS_MSGBOX_NO
		m_sButton2 = L"&No";
#else
		m_i18l.LoadString(IDS_MSGBOX_NO);
		m_sButton2 = m_i18l;
#endif
		m_uButton2Ret = IDNO;
#ifndef IDS_MSGBOX_CANCEL
		m_sButton3 = L"Cancel";
#else
		m_i18l.LoadString(IDS_MSGBOX_CANCEL);
		m_sButton3 = m_i18l;
#endif
		m_uButton3Ret = IDCANCEL;
		break;
	case MB_OK:
	default:
#ifndef IDS_MSGBOX_OK
		m_sButton1 = L"OK";
#else
		m_i18l.LoadString(IDS_MSGBOX_OK);
		m_sButton1 = m_i18l;
#endif
	}
	//now set the default button
	switch (uType & 0xf00)
	{
	case MB_DEFBUTTON2:
		ret = 2;
		break;
	case MB_DEFBUTTON3:
		ret = 3;
		break;
	}
	// do we need to add a help button?
	if (uType & MB_HELP)
	{
		CString sHelpText;
#ifndef IDS_MSGBOX_HELP
		sHelpText = L"Help";
#else
		m_i18l.LoadString(IDS_MSGBOX_HELP);
		sHelpText = m_i18l;
#endif
		if (m_sButton2.IsEmpty())
		{
			m_sButton2 = sHelpText;
			m_uButton2Ret = IDHELP;
		}
		else if (m_sButton3.IsEmpty())
		{
			m_sButton3 = sHelpText;
			m_uButton3Ret = IDHELP;
		}
	}
	return ret;
}

UINT CMessageBox::GoModal(CWnd * pWnd, const CString& title, const CString& msg, int nDefaultButton)
{
	NONCLIENTMETRICS ncm = { 0 };
	ncm.cbSize = sizeof(NONCLIENTMETRICS);
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);

	memcpy(&m_LogFont, &(ncm.lfMessageFont), sizeof(LOGFONT));

	//the problem with the LOGFONT lfHeight is that it is not in pixels,
	//but the dialog template needs the height in pixels.
	//We need to convert those values first:
	CDC * pDC;
	if (pWnd)
		pDC = pWnd->GetDC();
	else
		pDC = GetDesktopWindow()->GetDC();
	HDC hdc;
	if (pDC)
		hdc = pDC->m_hDC;
	else
		hdc = ::GetDC(nullptr);
	if (!hdc)
	{
		HWND hw = pWnd ? pWnd->m_hWnd : nullptr;
		int defButton = nDefaultButton == 1 ? MB_DEFBUTTON1 : nDefaultButton == 2 ? MB_DEFBUTTON2 : nDefaultButton == 3 ? MB_DEFBUTTON3 : 0;
		return ::MessageBox(hw, msg, title, m_uType | defButton);
	}

	int pix = -CDPIAware::Instance().PixelsToPointsY(GetSafeHwnd(), m_LogFont.lfHeight);
	CDlgTemplate dialogTemplate = CDlgTemplate(title, WS_CAPTION | DS_CENTER,
		0, 0, 0, 0, m_LogFont.lfFaceName, pix);
	dialogTemplate.AddButton(L"Button1", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | ((nDefaultButton == 1) ? BS_DEFPUSHBUTTON : 0), 0,
		2 + 3, 62, 56, 13, IDC_MESSAGEBOX_BUTTON1);
	dialogTemplate.AddButton(L"Button2", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | ((nDefaultButton == 2) ? BS_DEFPUSHBUTTON : 0), 0,
		2 + 3, 62, 56, 13, IDC_MESSAGEBOX_BUTTON2);
	dialogTemplate.AddButton(L"Button3", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | ((nDefaultButton == 3) ? BS_DEFPUSHBUTTON : 0), 0,
		2 + 3, 62, 56, 13, IDC_MESSAGEBOX_BUTTON3);
	dialogTemplate.AddButton(L"Checkbox", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 0,
		0, 0, 0, 0, IDC_MESSAGEBOX_CHECKBOX);

	m_nDefButton = nDefaultButton;
	m_sMessage = msg;
	InitModalIndirect(dialogTemplate, pWnd);

	return static_cast<UINT>(DoModal());
}

void CMessageBox::SetRegistryValue(const CString& sValue, DWORD value)
{
	if (sValue.IsEmpty())
		return;

	CString path;
#ifdef XMESSAGEBOX_APPREGPATH
	path = _T(XMESSAGEBOX_APPREGPATH);
#else
	path = L"Software\\TortoiseGit\\";
	path += AfxGetApp()->m_pszProfileName;
#endif
	DWORD disp;
	HKEY hKey;
	if (RegCreateKeyEx(HKEY_CURRENT_USER, path, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disp) != ERROR_SUCCESS)
	{
		return;
	}
	RegSetValueEx(hKey, sValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
	RegCloseKey(hKey);
}

enum class Command {
	None = 0,
	NewLine,
	Tabulation,
};

CSize DrawText(CDC* pDC, CRect rect, const CString& str, LOGFONT font, BOOL bCalculate = FALSE)
{
	CSize sz(0, 0);

	if (str.IsEmpty())
		return sz;

	CPoint pt = rect.TopLeft();
	CPoint ptCur = pt;

	LOGFONT lf;
	memcpy(&lf, &font, sizeof(LOGFONT));

	CFont tempFont;
	tempFont.CreateFontIndirect(&lf);

	CFont* pOldFont = pDC->SelectObject(&tempFont);

	TEXTMETRIC textMetric;
	pDC->GetTextMetrics(&textMetric);
	int nHeight = textMetric.tmHeight;
	int nWidth = textMetric.tmAveCharWidth;

	CString strText;

	//iterate through all characters of the string
	for (int i = 0; i <= str.GetLength(); ++i)
	{
		Command nCmd = Command::None;
		if (i < str.GetLength())
		{
			switch (str.GetAt(i))
			{
			case L'\n':
				nCmd = Command::NewLine;
				break;
			case L'\t':
				nCmd = Command::Tabulation;
				break;
			case L'\r':
				break;
			default:
				strText += str.GetAt(i);
				break;
			}
		}
		else // Immitates new line at the end of the string
			nCmd = Command::NewLine;

		if (nCmd != Command::None)
		{
			if (!strText.IsEmpty())
			{
				if (!bCalculate)
					pDC->TextOut(ptCur.x, ptCur.y, strText);
				CSize s = pDC->GetTextExtent(strText);
				ptCur.x += s.cx;
				strText.Empty();
			}

			// Executes command
			switch (nCmd)
			{
			case Command::NewLine:
				// New line
				sz.cx = std::max(sz.cx, ptCur.x - pt.x);
				ptCur.y += nHeight;
				ptCur.x = pt.x;
				break;
			case Command::Tabulation:
				// Tabulation
				int nTemp = (ptCur.x - pt.x) % (nWidth * 4);
				if (nTemp)
				{
					//aligns with tab
					ptCur.x += (nWidth * 4) - nTemp;
				}
				ptCur.x += nWidth * 4;
				break;
			}
		}
	}

	//Gets real height of the tooltip
	sz.cy = ptCur.y - pt.y;

	pDC->SelectObject(pOldFont);
	tempFont.DeleteObject();

	return sz;
}

CSize CMessageBox::GetTextSize(const CString& str)
{
	CRect rect;
	GetWindowRect(&rect);

	CDC * pDC = GetDC();

	CDC memDC;
	CBitmap bitmap;
	memDC.CreateCompatibleDC(pDC);
	bitmap.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
	CBitmap* pOldBitmap = memDC.SelectObject(&bitmap);

	//get the minimum size of the rectangle of the tooltip
	CSize sz = DrawText(&memDC, rect, str, m_LogFont, TRUE);

	memDC.SelectObject(pOldBitmap);
	memDC.DeleteDC();
	bitmap.DeleteObject();

	ReleaseDC(pDC);

	return sz;
}

CSize CMessageBox::GetIconSize(HICON hIcon)
{
	CSize sz (0, 0);

	if (hIcon)
	{
		ICONINFO ii = { 0 };
		//get icon dimensions
		if (::GetIconInfo(hIcon, &ii))
		{
			sz.cx = static_cast<DWORD>(ii.xHotspot * 2);
			sz.cy = static_cast<DWORD>(ii.yHotspot * 2);
			//release icon mask bitmaps
			if(ii.hbmMask)
				::DeleteObject(ii.hbmMask);
			if(ii.hbmColor)
				::DeleteObject(ii.hbmColor);
		}
	}
	m_szIcon = sz;
	return sz;
}

CSize CMessageBox::GetButtonSize()
{
	CSize sz;
	int nButtons = 0;		//number of buttons - 1

	SetDlgItemText(IDC_MESSAGEBOX_BUTTON1, m_sButton1);
	SetDlgItemText(IDC_MESSAGEBOX_BUTTON2, m_sButton2);
	//GetDlgItem(IDC_MESSAGEBOX_BUTTON2)->SendMessage(BM_SETSTYLE, BS_DEFPUSHBUTTON, 1);
	SetDlgItemText(IDC_MESSAGEBOX_BUTTON3, m_sButton3);
	SetDlgItemText(IDC_MESSAGEBOX_CHECKBOX, m_sCheckbox);

	CSize sz1 = GetTextSize(m_sButton1);
	CSize sz2 = GetTextSize(m_sButton2);
	CSize sz3 = GetTextSize(m_sButton3);

	sz1.cx += CDPIAware::Instance().ScaleX(GetSafeHwnd(), 2 * MESSAGEBOX_BUTTONX);
	sz1.cy += CDPIAware::Instance().ScaleY(GetSafeHwnd(), 2 * MESSAGEBOX_BUTTONY);

	if (sz2.cx)
	{
		sz2.cx += CDPIAware::Instance().ScaleX(GetSafeHwnd(), 2 * MESSAGEBOX_BUTTONX);
		sz2.cy += CDPIAware::Instance().ScaleY(GetSafeHwnd(), 2 * MESSAGEBOX_BUTTONY);
		nButtons++;
	}
	if (sz3.cx)
	{
		sz3.cx += CDPIAware::Instance().ScaleX(GetSafeHwnd(), 2 * MESSAGEBOX_BUTTONX);
		sz3.cy += CDPIAware::Instance().ScaleY(GetSafeHwnd(), 2 * MESSAGEBOX_BUTTONY);
		nButtons++;
	}

	GetDlgItem(IDC_MESSAGEBOX_BUTTON1)->MoveWindow(0, 0, sz1.cx, sz1.cy);
	GetDlgItem(IDC_MESSAGEBOX_BUTTON2)->MoveWindow(0, 0, sz2.cx, sz2.cy);
	GetDlgItem(IDC_MESSAGEBOX_BUTTON3)->MoveWindow(0, 0, sz3.cx, sz3.cy);

	sz.cx = sz1.cx + sz2.cx + sz3.cx + (nButtons * CDPIAware::Instance().ScaleX(GetSafeHwnd(), MESSAGEBOX_BUTTONMARGIN));
	sz.cy = std::max(sz1.cy, sz2.cy);
	sz.cy = std::max(sz.cy, sz3.cy);
	m_szButtons = sz;
	if (m_bShowCheck)
	{
		CSize szCheck = GetTextSize(m_sCheckbox);
		szCheck.cx += 2*GetSystemMetrics(SM_CXMENUCHECK);
		szCheck.cy += 2*MESSAGEBOX_BUTTONY;
		sz.cx = std::max(sz.cx, szCheck.cx);
		sz.cy += szCheck.cy + CDPIAware::Instance().ScaleY(GetSafeHwnd(), MESSAGEBOX_BUTTONCHECKMARGIN);
		GetDlgItem(IDC_MESSAGEBOX_CHECKBOX)->MoveWindow(0, 0, szCheck.cx, szCheck.cy);
	}
	m_szAllButtons = sz;
	return sz;
}

BEGIN_MESSAGE_MAP(CMessageBox, CDialog)
	ON_WM_PAINT()
	ON_BN_CLICKED(IDC_MESSAGEBOX_BUTTON1, OnButton1)
	ON_BN_CLICKED(IDC_MESSAGEBOX_BUTTON2, OnButton2)
	ON_BN_CLICKED(IDC_MESSAGEBOX_BUTTON3, OnButton3)
END_MESSAGE_MAP()

void CMessageBox::OnPaint()
{
	CPaintDC dc(this); // device context for painting

	CRect rect;
	CRect drawrect;
	GetClientRect(&rect);
	GetClientRect(&drawrect);

	//create a memory device-context. This is done to help reduce
	//screen flicker, since we will paint the entire control to the
	//off screen device context first.
	CDC memDC;
	CBitmap bitmap;
	memDC.CreateCompatibleDC(&dc);
	bitmap.CreateCompatibleBitmap(&dc, rect.Width(), rect.Height());
	CBitmap* pOldBitmap = memDC.SelectObject(&bitmap);

	memDC.BitBlt(rect.left, rect.top, rect.Width(), rect.Height(), &dc, 0,0, SRCCOPY);

	memDC.SetBkMode(TRANSPARENT);
	memDC.SetBkColor(GetSysColor(COLOR_WINDOW));
	memDC.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));

	//OnDrawBackground();
	drawrect.DeflateRect(CDPIAware::Instance().ScaleX(GetSafeHwnd(), MESSAGEBOX_BORDERMARGINX), CDPIAware::Instance().ScaleY(GetSafeHwnd(), MESSAGEBOX_BORDERMARGINY));
	if (m_hIcon)
	{
		DrawIconEx(memDC.m_hDC, drawrect.left, std::max(drawrect.top, drawrect.top +
			((drawrect.Height() - m_szAllButtons.cy - CDPIAware::Instance().ScaleY(GetSafeHwnd(), MESSAGEBOX_TEXTBUTTONMARGIN) - m_szIcon.cy) / 2)),
			m_hIcon, m_szIcon.cx, m_szIcon.cy, 0, nullptr, DI_NORMAL);

		drawrect.left += m_szIcon.cx + CDPIAware::Instance().ScaleX(GetSafeHwnd(), MESSAGEBOX_ICONMARGIN);
		if (m_szIcon.cy > m_szText.cy)
			drawrect.top += (m_szIcon.cy - m_szText.cy) / 2;
	}

	DrawText(&memDC, drawrect, m_sMessage, m_LogFont);

	//Copy the memory device context back into the original DC.
	dc.BitBlt(rect.left, rect.top, rect.Width(), rect.Height(), &memDC, 0,0, SRCCOPY);

	//Cleanup resources.
	memDC.SelectObject(pOldBitmap);
	memDC.DeleteDC();
	bitmap.DeleteObject();
}

static HWND ShowHtmlHelp(const HWND& owner, const LPCWSTR& path)
{
	wil::unique_hmodule hh{ AtlLoadSystemLibraryUsingFullPath(L"hhctrl.ocx") };
	if (!hh)
		return nullptr;
	auto pHtmlHelp = reinterpret_cast<decltype(::HtmlHelpW)*>(GetProcAddress(hh.get(), "HtmlHelpW"));
	return pHtmlHelp ? pHtmlHelp(owner, path, HH_DISPLAY_TOPIC, NULL) : nullptr;
}

void CMessageBox::OnButton1()
{
	if (GetDlgItem(IDC_MESSAGEBOX_CHECKBOX)->SendMessage(BM_GETCHECK, 0, 0)==BST_CHECKED)
	{
		m_bChecked = TRUE;
		SetRegistryValue(m_sRegistryValue, m_uButton1Ret);
	}
	else
		m_bChecked = FALSE;
	EndDialog(m_uButton1Ret);
}

void CMessageBox::OnButton2()
{
	if (GetDlgItem(IDC_MESSAGEBOX_CHECKBOX)->SendMessage(BM_GETCHECK, 0, 0)==BST_CHECKED)
	{
		m_bChecked = TRUE;
		SetRegistryValue(m_sRegistryValue, m_uButton2Ret);
	}
	else
		m_bChecked = FALSE;
	if ((m_uButton2Ret == IDHELP)&&(!m_sHelpPath.IsEmpty()))
	{
		auto&& hHelp = ShowHtmlHelp(m_hWnd, m_sHelpPath);
		if (!hHelp)
			::MessageBox(m_hWnd, L"could not show help file", L"Help", MB_ICONERROR);
	}
	else if (m_uButton2Ret == IDHELP)
		OnHelp();
	else
		EndDialog(m_uButton2Ret);
}

void CMessageBox::OnButton3()
{
	if (GetDlgItem(IDC_MESSAGEBOX_CHECKBOX)->SendMessage(BM_GETCHECK, 0, 0)==BST_CHECKED)
	{
		m_bChecked = TRUE;
		SetRegistryValue(m_sRegistryValue, m_uButton3Ret);
	}
	else
		m_bChecked = FALSE;
	if ((m_uButton3Ret == IDHELP)&&(!m_sHelpPath.IsEmpty()))
	{
		auto&& hHelp = ShowHtmlHelp(m_hWnd, m_sHelpPath);
		if (!hHelp)
			::MessageBox(m_hWnd, L"could not show help file", L"Help", MB_ICONERROR);
	}
	else if (m_uButton3Ret == IDHELP)
		OnHelp();
	else
		EndDialog(m_uButton3Ret);
}

void CMessageBox::OnCancel()
{
	if (m_uCancelRet == IDCANCEL)
		EndDialog(m_uCancelRet);
	//__super::OnCancel();
}

BOOL CMessageBox::OnInitDialog()
{
	__super::OnInitDialog();

	CRect rect(0, 0, 0, 0);

	//determine the required size of the message box
	m_szText = GetTextSize(m_sMessage);
	CSize szIcon = GetIconSize(m_hIcon);
	CSize szButtons = GetButtonSize();

	CSize szIconText;
	szIconText.cx = m_szText.cx + szIcon.cx + CDPIAware::Instance().ScaleX(GetSafeHwnd(), (szIcon.cx == 0) ? MESSAGEBOX_ICONMARGIN : (2 * MESSAGEBOX_ICONMARGIN));
	szIconText.cy = std::max(szIcon.cy, m_szText.cy);

	rect.right = std::max(szButtons.cx, szIconText.cx);
	rect.right += 2*GetSystemMetrics(SM_CXBORDER);
	rect.right += CDPIAware::Instance().ScaleX(GetSafeHwnd(), 2 * MESSAGEBOX_BORDERMARGINX);
	rect.bottom = szIconText.cy;
	rect.bottom += szButtons.cy;
	rect.bottom += CDPIAware::Instance().ScaleY(GetSafeHwnd(), 2 * MESSAGEBOX_BORDERMARGINY + MESSAGEBOX_TEXTBUTTONMARGIN);
	rect.bottom += GetSystemMetrics(SM_CYCAPTION);
	rect.bottom += 2 * GetSystemMetrics(SM_CYFIXEDFRAME);
	rect.bottom += 2*GetSystemMetrics(SM_CYBORDER);

	MoveWindow(rect);
	CenterWindow();

	GetClientRect(rect);

	//now size and position the buttons as we need them
	ASSERT(!m_sButton1.IsEmpty());		//at least the first button must be there!
	if (m_sButton2.IsEmpty())
	{
		//only one button
		CRect rt;
		GetDlgItem(IDC_MESSAGEBOX_BUTTON1)->GetWindowRect(rt);
		ScreenToClient(rt);
		rt.MoveToX(rect.left + ((rect.Width() - m_szButtons.cx)/2));
		rt.MoveToY(rect.bottom - CDPIAware::Instance().ScaleY(GetSafeHwnd(), MESSAGEBOX_BORDERMARGINY) - m_szButtons.cy);
		GetDlgItem(IDC_MESSAGEBOX_BUTTON1)->MoveWindow(rt);
		//hide the other two buttons
		GetDlgItem(IDC_MESSAGEBOX_BUTTON2)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_MESSAGEBOX_BUTTON3)->ShowWindow(SW_HIDE);
	}
	else if (m_sButton3.IsEmpty())
	{
		//two buttons
		CRect rt1;
		CRect rt2;
		GetDlgItem(IDC_MESSAGEBOX_BUTTON1)->GetWindowRect(rt1);
		ScreenToClient(rt1);
		GetDlgItem(IDC_MESSAGEBOX_BUTTON2)->GetWindowRect(rt2);
		ScreenToClient(rt2);
		rt1.MoveToX(rect.left + ((rect.Width() - m_szButtons.cx)/2));
		rt1.MoveToY(rect.bottom - CDPIAware::Instance().ScaleY(GetSafeHwnd(), MESSAGEBOX_BORDERMARGINY) - m_szButtons.cy);
		rt2.MoveToX(rt1.right + CDPIAware::Instance().ScaleX(GetSafeHwnd(), MESSAGEBOX_BUTTONMARGIN));
		rt2.MoveToY(rect.bottom - CDPIAware::Instance().ScaleY(GetSafeHwnd(), MESSAGEBOX_BORDERMARGINY) - m_szButtons.cy);
		GetDlgItem(IDC_MESSAGEBOX_BUTTON1)->MoveWindow(rt1);
		GetDlgItem(IDC_MESSAGEBOX_BUTTON2)->MoveWindow(rt2);
		//hide the third button
		GetDlgItem(IDC_MESSAGEBOX_BUTTON3)->ShowWindow(SW_HIDE);
	}
	else
	{
		//three buttons
		CRect buttonrect;
		GetDlgItem(IDC_MESSAGEBOX_BUTTON1)->GetWindowRect(buttonrect);
		CRect rt1;
		CRect rt2;
		CRect rt3;
		GetDlgItem(IDC_MESSAGEBOX_BUTTON1)->GetWindowRect(rt1);
		ScreenToClient(rt1);
		GetDlgItem(IDC_MESSAGEBOX_BUTTON2)->GetWindowRect(rt2);
		ScreenToClient(rt2);
		GetDlgItem(IDC_MESSAGEBOX_BUTTON3)->GetWindowRect(rt3);
		ScreenToClient(rt3);
		rt1.MoveToX(rect.left + ((rect.Width() - m_szButtons.cx)/2));
		rt1.MoveToY(rect.bottom - CDPIAware::Instance().ScaleY(GetSafeHwnd(), MESSAGEBOX_BORDERMARGINY) - m_szButtons.cy);
		rt2.MoveToX(rt1.right + CDPIAware::Instance().ScaleX(GetSafeHwnd(), MESSAGEBOX_BUTTONMARGIN));
		rt2.MoveToY(rect.bottom - CDPIAware::Instance().ScaleY(GetSafeHwnd(), MESSAGEBOX_BORDERMARGINY) - m_szButtons.cy);
		rt3.MoveToX(rt2.right + CDPIAware::Instance().ScaleX(GetSafeHwnd(), MESSAGEBOX_BUTTONMARGIN));
		rt3.MoveToY(rect.bottom - CDPIAware::Instance().ScaleY(GetSafeHwnd(), MESSAGEBOX_BORDERMARGINY) - m_szButtons.cy);
		GetDlgItem(IDC_MESSAGEBOX_BUTTON1)->MoveWindow(rt1);
		GetDlgItem(IDC_MESSAGEBOX_BUTTON2)->MoveWindow(rt2);
		GetDlgItem(IDC_MESSAGEBOX_BUTTON3)->MoveWindow(rt3);
	}
	if (m_bShowCheck)
	{
		CRect rt;
		GetDlgItem(IDC_MESSAGEBOX_CHECKBOX)->GetWindowRect(rt);
		ScreenToClient(rt);
		rt.MoveToX(rect.left + CDPIAware::Instance().ScaleX(GetSafeHwnd(), MESSAGEBOX_BORDERMARGINX) /*+ ((rect.Width() - szButtons.cx)/2)*/);
		rt.MoveToY(rect.bottom - CDPIAware::Instance().ScaleY(GetSafeHwnd(), MESSAGEBOX_BORDERMARGINY) - szButtons.cy);
		GetDlgItem(IDC_MESSAGEBOX_CHECKBOX)->MoveWindow(rt);
		GetDlgItem(IDC_MESSAGEBOX_CHECKBOX)->ShowWindow(SW_SHOW);
	}
	else
		GetDlgItem(IDC_MESSAGEBOX_CHECKBOX)->ShowWindow(SW_HIDE);

	SetWindowPos(&CWnd::wndTopMost,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
	SetForegroundWindow();
	SetFocus(); //Just playing safe

	if (m_nDefButton == 1)
		GetDlgItem(IDC_MESSAGEBOX_BUTTON1)->SetFocus();
	if (m_nDefButton == 2)
		GetDlgItem(IDC_MESSAGEBOX_BUTTON2)->SetFocus();
	if (m_nDefButton == 3)
		GetDlgItem(IDC_MESSAGEBOX_BUTTON3)->SetFocus();

	return FALSE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CMessageBox::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		switch (pMsg->wParam)
		{
		case 'C':
		case VK_INSERT:
			{
				if (GetAsyncKeyState(VK_CONTROL)&0x8000)
				{
					wil::SetClipboardText(tgit::str::StringView{ m_sMessage }, GetSafeHwnd());
					return TRUE;
				}
			}
			break;

		case VK_ESCAPE:
			{
				switch (m_uType & 0xf)
				{
				case MB_ABORTRETRYIGNORE:
					[[fallthrough]];
				case MB_CANCELTRYCONTINUE:
					EndDialog(m_uButton1Ret);
					break;
				case MB_OKCANCEL:
					[[fallthrough]];
				case MB_RETRYCANCEL:
					[[fallthrough]];
				case MB_YESNO:
					EndDialog(m_uButton2Ret);
					break;
				case MB_YESNOCANCEL:
					EndDialog(m_uButton3Ret);
					break;

				default:
					std::unreachable();
				}
			}
			break;

		default:
			std::unreachable();
		}
	}

	return __super::PreTranslateMessage(pMsg);
}

