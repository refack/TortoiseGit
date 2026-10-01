module;
#include <stddef.h> // offsetof: a macro, so import std does not provide it
#include <atlstr.h>
#include <Shldisp.h>
#include <shlobj_core.h>
#include <sanity.h>


export module GitDataObject;
import std;
import git_h;
import wil;
import wstr;
import TGitPath;
import TempFile;
import GitHash;

using TGitPath::CTGitPath;
using TGitPath::CTGitPathList;
using TGitHash::CGitHash;



namespace GitDataObject
{
constexpr auto GITDATAOBJECT_NUMFORMATS = 9;

/// Lays out one of Win32's variable-length structs - a header whose last member
/// is a one-element array standing in for N (FILEGROUPDESCRIPTOR::fgd,
/// FILE_ATTRIBUTES_ARRAY::rgdwFileAttributes) - as bytes: the header up to
/// \a arrayOffset, then \a elements, zero-padded to at least sizeof(Header) as
/// the one-element declaration promises.
///
/// This is what replaces the SizeTMult/SizeTAdd chains that sized these by hand:
/// the elements already exist, so as_bytes() cannot overflow, and the header is
/// a handful of bytes on top of something no larger than PTRDIFF_MAX.
template <typename Header, typename Element>
std::vector<std::byte> FlexibleStruct(const Header& header, const std::size_t arrayOffset, const std::span<const Element> elements)
{
	const auto headerBytes = std::as_bytes(std::span(&header, 1)).first(arrayOffset);
	const auto elementBytes = std::as_bytes(elements);
	std::vector<std::byte> bytes(headerBytes.begin(), headerBytes.end());
	bytes.insert(bytes.end(), elementBytes.begin(), elementBytes.end());
	if (bytes.size() < sizeof(Header))
		bytes.resize(sizeof(Header));
	return bytes;
}

/// Puts \a block into \a medium as its HGLOBAL, which hands ownership to the
/// caller of GetData. E_OUTOFMEMORY when the allocation had failed.
HRESULT GiveToMedium(STGMEDIUM* const medium, wil::unique_hglobal block)
{
	if (!block)
		return E_OUTOFMEMORY;
	medium->tymed = TYMED_HGLOBAL;
	medium->hGlobal = block.release();
	medium->pUnkForRelease = nullptr;
	return S_OK;
}

/**
 * \ingroup Git
 * Represents a Git path as an IDataObject.
 * This can be used for drag and drop operations to other applications like
 * the shell itself.
 */
class GitDataObject : public IDataObject, public IDataObjectAsyncCapability
{
public:
	/**
	 * Constructs the GitDataObject.
	 * \param gitpaths a list of paths which must be inside a working tree.
	 * \param rev      the revision
	 * \param stripLength      trim?
	 */
	GitDataObject(const CTGitPathList& gitpaths, const CGitHash& rev, int stripLength = -1);
	virtual ~GitDataObject();

	//IUnknown
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;

	//IDataObject
	HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override;
	HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* pformatetc, STGMEDIUM* pmedium) override;
	HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pformatetc) override;
	HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* pformatectIn, FORMATETC* pformatetcOut) override;
	HRESULT STDMETHODCALLTYPE SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) override;
	HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) override;
	HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) override;
	HRESULT STDMETHODCALLTYPE DUnadvise(DWORD dwConnection) override;
	HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** ppenumAdvise) override;

	//IDataObjectAsyncCapability
	HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL fDoOpAsync) override;
	HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL* pfIsOpAsync) override;
	HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx* pbcReserved) override;
	HRESULT STDMETHODCALLTYPE InOperation(BOOL* pfInAsyncOp) override;
	HRESULT STDMETHODCALLTYPE EndOperation(HRESULT hResult, IBindCtx* pbcReserved, DWORD dwEffects) override;

	HRESULT SetDropDescription(DROPIMAGETYPE image, LPCWSTR format, LPCWSTR insert);

private:
	void CopyMedium(STGMEDIUM* pMedDest, STGMEDIUM* pMedSrc, FORMATETC* pFmtSrc);

	CTGitPathList				m_gitPaths;
	CGitHash					m_revision;
	std::vector<CTGitPath>		m_allPaths;
	int							m_iStripLength = 0;
	long						m_cRefCount = 0;
	bool						m_containsExistingFiles = false;
	BOOL						m_bInOperation = FALSE;
	BOOL						m_bIsAsync = TRUE;
	std::vector<FORMATETC*>		m_vecFormatEtc;
	std::vector<STGMEDIUM*>		m_vecStgMedium;
};


/**
 * Helper class for the GitDataObject class: implements the enumerator
 * for the supported clipboard formats of the GitDataObject class.
 */
class CGitEnumFormatEtc : public IEnumFORMATETC
{
public:
	virtual ~CGitEnumFormatEtc() = default;
	CGitEnumFormatEtc(const std::vector<FORMATETC*>& vec, bool localonly, bool containsExistingFiles);
	CGitEnumFormatEtc(const std::vector<FORMATETC>& vec, bool localonly, bool containsExistingFiles);
	//IUnknown members
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;

	//IEnumFORMATETC members
	STDMETHODIMP Next(ULONG, LPFORMATETC, ULONG*) override;
	STDMETHODIMP Skip(ULONG) override;
	STDMETHODIMP Reset() override;
	STDMETHODIMP Clone(IEnumFORMATETC**) override;

private:
	void						Init(bool localonly, bool containsExistingFiles);

	std::vector<FORMATETC>		m_vecFormatEtc;
	FORMATETC					m_formats[GITDATAOBJECT_NUMFORMATS];
	ULONG						m_cRefCount = 0;
	size_t						m_iCur = 0;
	bool						m_localonly = false;
	bool						m_containsExistingFiles = false;
};




CLIPFORMAT CF_FILECONTENTS = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_FILECONTENTS));
CLIPFORMAT CF_FILEDESCRIPTOR = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR));
CLIPFORMAT CF_PREFERREDDROPEFFECT = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT));
CLIPFORMAT CF_INETURL = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_INETURL));
CLIPFORMAT CF_SHELLURL = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_SHELLURL));
CLIPFORMAT CF_FILE_ATTRIBUTES_ARRAY = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_FILE_ATTRIBUTES_ARRAY));

GitDataObject::GitDataObject(const CTGitPathList& gitpaths, const CGitHash& rev, int stripLength)
	: m_gitPaths(gitpaths)
	, m_revision(rev)
	, m_iStripLength(stripLength)
{
	Expects((m_revision.IsEmpty() && m_iStripLength == -1) || (!m_revision.IsEmpty() && m_iStripLength >= -1)); // m_iStripLength only possible if rev is set
	for (int i = 0; i < m_gitPaths.GetCount(); ++i)
	{
		if ((m_gitPaths[i].m_Action == 0 || (m_gitPaths[i].m_Action & ~(CTGitPath::LOGACTIONS_MISSING | CTGitPath::LOGACTIONS_DELETED))) && !m_gitPaths[i].IsDirectory())
		{
			m_containsExistingFiles = true;
			break;
		}
	}
}

GitDataObject::~GitDataObject()
{
	for (size_t i = 0; i < m_vecStgMedium.size(); ++i)
	{
		ReleaseStgMedium(m_vecStgMedium[i]);
		delete m_vecStgMedium[i];
	}

	for (size_t j = 0; j < m_vecFormatEtc.size(); ++j)
		delete m_vecFormatEtc[j];
}

//////////////////////////////////////////////////////////////////////////
// IUnknown
//////////////////////////////////////////////////////////////////////////

STDMETHODIMP GitDataObject::QueryInterface(REFIID riid, void** ppvObject)
{
	if (!ppvObject)
		return E_POINTER;
	*ppvObject = NULL;
	if (IsEqualIID(IID_IUnknown, riid) || IsEqualIID(IID_IDataObject, riid))
		*ppvObject = static_cast<IDataObject*>(this);
	else if (IsEqualIID(riid, IID_IDataObjectAsyncCapability))
		*ppvObject = static_cast<IDataObjectAsyncCapability*>(this);
	else
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) GitDataObject::AddRef()
{
	return ++m_cRefCount;
}

STDMETHODIMP_(ULONG) GitDataObject::Release()
{
	--m_cRefCount;
	if (m_cRefCount == 0)
	{
		delete this;
		return 0;
	}
	return m_cRefCount;
}

//////////////////////////////////////////////////////////////////////////
// IDataObject
//////////////////////////////////////////////////////////////////////////
STDMETHODIMP GitDataObject::GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium)
{
	if (!pformatetcIn)
		return E_INVALIDARG;
	if (!pmedium)
		return E_POINTER;
	pmedium->hGlobal = nullptr;

	if ((pformatetcIn->tymed & TYMED_ISTREAM) && (pformatetcIn->dwAspect == DVASPECT_CONTENT) && (pformatetcIn->cfFormat == CF_FILECONTENTS))
	{
		// supports the IStream format.
		// The lindex param is the index of the file to return
		CString filepath;
		IStream* pIStream = nullptr;

		// Note: we don't get called for directories since those are simply created and don't
		// need to be fetched.

		// Note2: It would be really nice if we could get a stream from the subversion library
		// from where we could read the file contents. But the Subversion lib is not implemented
		// to *read* from a remote stream but so that the library *writes* to a stream we pass.
		// Since we can't get such a read stream, we have to fetch the file in whole first to
		// a temp location and then pass the shell an IStream for that temp file.

		if (m_revision.IsEmpty())
		{
			if (pformatetcIn->lindex >= 0 && pformatetcIn->lindex < static_cast<LONG>(m_allPaths.size()))
				filepath = g_Git.CombinePath(m_allPaths[pformatetcIn->lindex]);
		}
		else
		{
			filepath = CTempFiles::Instance().GetTempFilePath(true).GetWinPathString().c_str();
			if (pformatetcIn->lindex >= 0 && pformatetcIn->lindex < static_cast<LONG>(m_allPaths.size()))
			{
				if (g_Git.GetOneFile(m_revision.ToString().data(), m_allPaths[pformatetcIn->lindex], filepath))
				{
					DeleteFile(filepath);
					return STG_E_ACCESSDENIED;
				}
			}
		}

		HRESULT res = SHCreateStreamOnFileEx(filepath, STGM_READ, FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &pIStream);
		if (res == S_OK)
		{
			// http://blogs.msdn.com/b/oldnewthing/archive/2014/09/18/10558763.aspx
			LARGE_INTEGER liZero = { 0, 0 };
			pIStream->Seek(liZero, STREAM_SEEK_END, nullptr);

			pmedium->pstm = pIStream;
			pmedium->tymed = TYMED_ISTREAM;
			return S_OK;
		}
		return res;
	}
	else if ((pformatetcIn->tymed & TYMED_HGLOBAL) && (pformatetcIn->dwAspect == DVASPECT_CONTENT) && (pformatetcIn->cfFormat == CF_FILEDESCRIPTOR))
	{
		for (int i = 0; i < m_gitPaths.GetCount(); ++i)
		{
			if (m_gitPaths[i].m_Action & (CTGitPath::LOGACTIONS_MISSING | CTGitPath::LOGACTIONS_DELETED) || m_gitPaths[i].IsDirectory())
				continue;
			m_allPaths.push_back(m_gitPaths[i]);
		}

		std::vector<FILEDESCRIPTOR> descriptors;
		descriptors.reserve(m_allPaths.size());
		for (const auto& path : m_allPaths)
		{
			const std::wstring name(m_iStripLength > 0 ? tgit::wstr::Mid(path.GetWinPathString(), m_iStripLength + 1) : (m_iStripLength == 0 ? path.GetWinPathString() : path.GetUIFileOrDirectoryName()));
			FILEDESCRIPTOR& descriptor = descriptors.emplace_back(); // value-initialized, as GMEM_ZEROINIT was
			if (!tgit::wstr::CopyTruncating(descriptor.cFileName, name))
			{
				// a name that does not fit is left out rather than truncated
				descriptors.pop_back();
				continue;
			}
			descriptor.dwFlags = FD_ATTRIBUTES | FD_PROGRESSUI | FD_FILESIZE | FD_LINKUI;
			descriptor.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

			// Always leave the file size at 0 even if we 'know' the file size (infodata.size64).
			// Because for text files, the file size is too low due to the EOLs getting converted
			// to CRLF (from LF as stored in the repository). And a too low file size set here
			// prevents the shell from reading the full file later - it only reads the stream up
			// to the number of bytes specified here. Which means we would end up with a truncated
			// text file (binary files are still ok though).
		}

		FILEGROUPDESCRIPTOR header{};
		header.cItems = static_cast<UINT>(descriptors.size());
		const auto bytes = FlexibleStruct(header, offsetof(FILEGROUPDESCRIPTOR, fgd), std::span<const FILEDESCRIPTOR>(descriptors));
		return GiveToMedium(pmedium, wil::make_moveable_hglobal(bytes));
	}
	// handling CF_PREFERREDDROPEFFECT is necessary to tell the shell that it should *not* ask for the
	// CF_FILEDESCRIPTOR until the drop actually occurs. If we don't handle CF_PREFERREDDROPEFFECT, the shell
	// will ask for the file descriptor for every object (file/folder) the mouse pointer hovers over and is
	// a potential drop target.
	else if ((pformatetcIn->tymed & TYMED_HGLOBAL) && (pformatetcIn->cfFormat == CF_PREFERREDDROPEFFECT))
	{
		const DWORD effect = DROPEFFECT_COPY;
		return GiveToMedium(pmedium, wil::make_moveable_hglobal(std::as_bytes(std::span(&effect, 1))));
	}
	else if ((pformatetcIn->tymed & TYMED_HGLOBAL) && (pformatetcIn->dwAspect == DVASPECT_CONTENT) && (pformatetcIn->cfFormat == CF_TEXT))
	{
		// caller wants text
		// create the string from the path list
		// create a single string where the paths are separated by newlines
		std::wstring text;
		for (const auto& path : m_gitPaths)
		{
			text += path.GetWinPathString();
			text += L"\r\n";
		}
		return GiveToMedium(pmedium, wil::make_moveable_hglobal(CUnicodeUtils::StdGetUTF8(text)));
	}
	else if ((pformatetcIn->tymed & TYMED_HGLOBAL) && (pformatetcIn->dwAspect == DVASPECT_CONTENT) && ((pformatetcIn->cfFormat == CF_UNICODETEXT) || (pformatetcIn->cfFormat == CF_INETURL) || (pformatetcIn->cfFormat == CF_SHELLURL)))
	{
		// caller wants Unicode text
		// create the string from the path list
		// create a single string where the paths (or, for the URL formats, the absolute paths) are separated by newlines
		std::wstring text;
		for (const auto& path : m_gitPaths)
		{
			if (pformatetcIn->cfFormat == CF_UNICODETEXT)
				text += path.GetWinPathString();
			else
				text += g_Git.CombinePath(path).GetString();
			text += L"\r\n";
		}
		return GiveToMedium(pmedium, wil::make_moveable_hglobal(text));
	}
	else if ((pformatetcIn->tymed & TYMED_HGLOBAL) && (pformatetcIn->dwAspect == DVASPECT_CONTENT) && (pformatetcIn->cfFormat == CF_HDROP))
	{
		// DROPFILES, then the file names, each NUL-terminated, then one more NUL
		std::wstring names;
		for (const auto& path : m_gitPaths)
		{
			if (path.m_Action & (CTGitPath::LOGACTIONS_MISSING | CTGitPath::LOGACTIONS_DELETED) || path.IsDirectory())
				continue;
			names += g_Git.CombinePath(path).GetString();
			names += L'\0';
		}
		names += L'\0';

		DROPFILES header{}; // pt and fNC used to be left uninitialized
		header.pFiles = sizeof(DROPFILES);
		header.fWide = TRUE;
		const auto bytes = FlexibleStruct(header, sizeof(DROPFILES), std::span<const wchar_t>(names));
		return GiveToMedium(pmedium, wil::make_moveable_hglobal(bytes));
	}
	else if ((pformatetcIn->tymed & TYMED_HGLOBAL) && (pformatetcIn->dwAspect == DVASPECT_CONTENT) && (pformatetcIn->cfFormat == CF_FILE_ATTRIBUTES_ARRAY))
	{
		const std::vector<DWORD> attributes(static_cast<size_t>(m_gitPaths.GetCount()), FILE_ATTRIBUTE_NORMAL);
		FILE_ATTRIBUTES_ARRAY header{};
		header.cItems = static_cast<UINT>(attributes.size());
		header.dwProductFileAttributes = attributes.empty() ? std::numeric_limits<DWORD>::max() : FILE_ATTRIBUTE_NORMAL; // AND over all items, starting from all-ones
		header.dwSumFileAttributes = attributes.empty() ? 0 : FILE_ATTRIBUTE_NORMAL;             // OR over all items
		const auto bytes = FlexibleStruct(header, offsetof(FILE_ATTRIBUTES_ARRAY, rgdwFileAttributes), std::span<const DWORD>(attributes));
		return GiveToMedium(pmedium, wil::make_moveable_hglobal(bytes));
	}

	for (size_t i = 0; i < m_vecFormatEtc.size(); ++i)
	{
		if ((pformatetcIn->tymed == m_vecFormatEtc[i]->tymed) &&
			(pformatetcIn->dwAspect == m_vecFormatEtc[i]->dwAspect) &&
			(pformatetcIn->cfFormat == m_vecFormatEtc[i]->cfFormat))
		{
			CopyMedium(pmedium, m_vecStgMedium[i], m_vecFormatEtc[i]);
			return S_OK;
		}
	}

	return DV_E_FORMATETC;
}

STDMETHODIMP GitDataObject::GetDataHere(FORMATETC* /*pformatetc*/, STGMEDIUM* /*pmedium*/)
{
	return E_NOTIMPL;
}

STDMETHODIMP GitDataObject::QueryGetData(FORMATETC* pformatetc)
{
	if (!pformatetc)
		return E_INVALIDARG;

	if (!(DVASPECT_CONTENT & pformatetc->dwAspect))
		return DV_E_DVASPECT;

	if ((pformatetc->tymed & TYMED_ISTREAM) &&
		(pformatetc->dwAspect == DVASPECT_CONTENT) &&
		(pformatetc->cfFormat == CF_FILECONTENTS) &&
		m_containsExistingFiles)
	{
		return S_OK;
	}
	if ((pformatetc->tymed & TYMED_HGLOBAL) &&
		(pformatetc->dwAspect == DVASPECT_CONTENT) &&
		((pformatetc->cfFormat == CF_TEXT) || (pformatetc->cfFormat == CF_UNICODETEXT) || (pformatetc->cfFormat == CF_PREFERREDDROPEFFECT)))
	{
		return S_OK;
	}
	if ((pformatetc->tymed & TYMED_HGLOBAL) &&
		(pformatetc->dwAspect == DVASPECT_CONTENT) &&
		(pformatetc->cfFormat == CF_FILEDESCRIPTOR) &&
		!m_revision.IsEmpty() &&
		m_containsExistingFiles)
	{
		return S_OK;
	}
	if ((pformatetc->tymed & TYMED_HGLOBAL) &&
		(pformatetc->dwAspect == DVASPECT_CONTENT) &&
		((pformatetc->cfFormat == CF_HDROP) || (pformatetc->cfFormat == CF_INETURL) || (pformatetc->cfFormat == CF_SHELLURL)) &&
		m_revision.IsEmpty() &&
		m_containsExistingFiles)
	{
		return S_OK;
	}
	if ((pformatetc->tymed & TYMED_HGLOBAL) &&
		(pformatetc->dwAspect == DVASPECT_CONTENT) &&
		(pformatetc->cfFormat == CF_FILE_ATTRIBUTES_ARRAY) &&
		m_containsExistingFiles)
	{
		return S_OK;
	}

	for (size_t i = 0; i < m_vecFormatEtc.size(); ++i)
	{
		if ((pformatetc->tymed == m_vecFormatEtc[i]->tymed) &&
			(pformatetc->dwAspect == m_vecFormatEtc[i]->dwAspect) &&
			(pformatetc->cfFormat == m_vecFormatEtc[i]->cfFormat))
			return S_OK;
	}

	return DV_E_TYMED;
}

STDMETHODIMP GitDataObject::GetCanonicalFormatEtc(FORMATETC* /*pformatectIn*/, FORMATETC* pformatetcOut)
{
	if (!pformatetcOut)
		return E_INVALIDARG;
	return DATA_S_SAMEFORMATETC;
}

STDMETHODIMP GitDataObject::SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease)
{
	if (!pformatetc || !pmedium)
		return E_INVALIDARG;

	FORMATETC* fetc = new (std::nothrow) FORMATETC;
	STGMEDIUM* pStgMed = new (std::nothrow) STGMEDIUM;

	if (!fetc || !pStgMed)
	{
		delete fetc;
		delete pStgMed;
		return E_OUTOFMEMORY;
	}
	SecureZeroMemory(fetc, sizeof(FORMATETC));
	SecureZeroMemory(pStgMed, sizeof(STGMEDIUM));

	// do we already store this format?
	for (size_t i = 0; i < m_vecFormatEtc.size(); ++i)
	{
		if ((pformatetc->tymed == m_vecFormatEtc[i]->tymed) &&
			(pformatetc->dwAspect == m_vecFormatEtc[i]->dwAspect) &&
			(pformatetc->cfFormat == m_vecFormatEtc[i]->cfFormat))
		{
			// yes, this format is already in our object:
			// we have to replace the existing format. To do that, we
			// remove the format we already have
			delete m_vecFormatEtc[i];
			m_vecFormatEtc.erase(m_vecFormatEtc.begin() + i);
			ReleaseStgMedium(m_vecStgMedium[i]);
			delete m_vecStgMedium[i];
			m_vecStgMedium.erase(m_vecStgMedium.begin() + i);
			break;
		}
	}

	*fetc = *pformatetc;
	m_vecFormatEtc.push_back(fetc);

	if (fRelease)
		*pStgMed = *pmedium;
	else
		CopyMedium(pStgMed, pmedium, pformatetc);
	m_vecStgMedium.push_back(pStgMed);

	return S_OK;
}

STDMETHODIMP GitDataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc)
{
	if (!ppenumFormatEtc)
		return E_POINTER;

	*ppenumFormatEtc = nullptr;
	switch (dwDirection)
	{
	case DATADIR_GET:
		*ppenumFormatEtc = new (std::nothrow) CGitEnumFormatEtc(m_vecFormatEtc, m_revision.IsEmpty(), m_containsExistingFiles);
		if (!*ppenumFormatEtc)
			return E_OUTOFMEMORY;
		(*ppenumFormatEtc)->AddRef();
		break;
	default:
		return E_NOTIMPL;
	}
	return S_OK;
}

STDMETHODIMP GitDataObject::DAdvise(FORMATETC* /*pformatetc*/, DWORD /*advf*/, IAdviseSink* /*pAdvSink*/, DWORD* /*pdwConnection*/)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP GitDataObject::DUnadvise(DWORD /*dwConnection*/)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE GitDataObject::EnumDAdvise(IEnumSTATDATA** /*ppenumAdvise*/)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

void GitDataObject::CopyMedium(STGMEDIUM* pMedDest, STGMEDIUM* pMedSrc, FORMATETC* pFmtSrc)
{
	switch (pMedSrc->tymed)
	{
	case TYMED_HGLOBAL:
		pMedDest->hGlobal = static_cast<HGLOBAL>(OleDuplicateData(pMedSrc->hGlobal, pFmtSrc->cfFormat, 0));
		break;
	case TYMED_GDI:
		pMedDest->hBitmap = static_cast<HBITMAP>(OleDuplicateData(pMedSrc->hBitmap, pFmtSrc->cfFormat, 0));
		break;
	case TYMED_MFPICT:
		pMedDest->hMetaFilePict = static_cast<HMETAFILEPICT>(OleDuplicateData(pMedSrc->hMetaFilePict, pFmtSrc->cfFormat, 0));
		break;
	case TYMED_ENHMF:
		pMedDest->hEnhMetaFile = static_cast<HENHMETAFILE>(OleDuplicateData(pMedSrc->hEnhMetaFile, pFmtSrc->cfFormat, 0));
		break;
	case TYMED_FILE:
		pMedSrc->lpszFileName = static_cast<LPOLESTR>(OleDuplicateData(pMedSrc->lpszFileName, pFmtSrc->cfFormat, 0));
		break;
	case TYMED_ISTREAM:
		pMedDest->pstm = pMedSrc->pstm;
		pMedSrc->pstm->AddRef();
		break;
	case TYMED_ISTORAGE:
		pMedDest->pstg = pMedSrc->pstg;
		pMedSrc->pstg->AddRef();
		break;
	case TYMED_NULL:
	default:
		break;
	}
	pMedDest->tymed = pMedSrc->tymed;
	pMedDest->pUnkForRelease = nullptr;
	if (pMedSrc->pUnkForRelease)
	{
		pMedDest->pUnkForRelease = pMedSrc->pUnkForRelease;
		pMedSrc->pUnkForRelease->AddRef();
	}
}


//////////////////////////////////////////////////////////////////////////
// IAsyncOperation
//////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE GitDataObject::SetAsyncMode(BOOL fDoOpAsync)
{
	m_bIsAsync = fDoOpAsync;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE GitDataObject::GetAsyncMode(BOOL* pfIsOpAsync)
{
	if (!pfIsOpAsync)
		return E_FAIL;

	*pfIsOpAsync = m_bIsAsync;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE GitDataObject::StartOperation(IBindCtx* /*pbcReserved*/)
{
	m_bInOperation = TRUE;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE GitDataObject::InOperation(BOOL* pfInAsyncOp)
{
	if (!pfInAsyncOp)
		return E_FAIL;

	*pfInAsyncOp = m_bInOperation;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE GitDataObject::EndOperation(HRESULT /*hResult*/, IBindCtx* /*pbcReserved*/, DWORD /*dwEffects*/)
{
	m_bInOperation = FALSE;
	return S_OK;
}

HRESULT GitDataObject::SetDropDescription(DROPIMAGETYPE image, LPCWSTR format, LPCWSTR insert)
{
	if (!format || !insert)
		return E_INVALIDARG;

	FORMATETC fetc = { 0 };
	fetc.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_DROPDESCRIPTION));
	fetc.dwAspect = DVASPECT_CONTENT;
	fetc.lindex = -1;
	fetc.tymed = TYMED_HGLOBAL;

	DROPDESCRIPTION description{};
	description.type = image;
	tgit::wstr::CopyTruncating(description.szInsert, insert);
	tgit::wstr::CopyTruncating(description.szMessage, format);

	auto block = wil::make_moveable_hglobal(std::as_bytes(std::span(&description, 1)));
	if (!block)
		return E_OUTOFMEMORY;
	STGMEDIUM medium = {};
	medium.tymed = TYMED_HGLOBAL;
	medium.hGlobal = block.get();
	const HRESULT hr = SetData(&fetc, &medium, TRUE);
	if (SUCCEEDED(hr))
		block.release(); // the data object owns it now; on failure we still do
	return hr;
}

void CGitEnumFormatEtc::Init(bool localonly, bool containsExistingFiles)
{
	int index = 0;
	m_formats[index].cfFormat = CF_UNICODETEXT;
	m_formats[index].dwAspect = DVASPECT_CONTENT;
	m_formats[index].lindex = -1;
	m_formats[index].ptd = nullptr;
	m_formats[index].tymed = TYMED_HGLOBAL;
	index++;

	m_formats[index].cfFormat = CF_TEXT;
	m_formats[index].dwAspect = DVASPECT_CONTENT;
	m_formats[index].lindex = -1;
	m_formats[index].ptd = nullptr;
	m_formats[index].tymed = TYMED_HGLOBAL;
	index++;

	m_formats[index].cfFormat = CF_PREFERREDDROPEFFECT;
	m_formats[index].dwAspect = DVASPECT_CONTENT;
	m_formats[index].lindex = -1;
	m_formats[index].ptd = nullptr;
	m_formats[index].tymed = TYMED_HGLOBAL;
	index++;

	if (containsExistingFiles && localonly)
	{
		m_formats[index].cfFormat = CF_INETURL;
		m_formats[index].dwAspect = DVASPECT_CONTENT;
		m_formats[index].lindex = -1;
		m_formats[index].ptd = nullptr;
		m_formats[index].tymed = TYMED_HGLOBAL;
		index++;

		m_formats[index].cfFormat = CF_SHELLURL;
		m_formats[index].dwAspect = DVASPECT_CONTENT;
		m_formats[index].lindex = -1;
		m_formats[index].ptd = nullptr;
		m_formats[index].tymed = TYMED_HGLOBAL;
		index++;
	}

	m_formats[index].cfFormat = CF_FILE_ATTRIBUTES_ARRAY;
	m_formats[index].dwAspect = DVASPECT_CONTENT;
	m_formats[index].lindex = -1;
	m_formats[index].ptd = nullptr;
	m_formats[index].tymed = TYMED_HGLOBAL;
	index++;

	if (containsExistingFiles && localonly)
	{
		m_formats[index].cfFormat = CF_HDROP;
		m_formats[index].dwAspect = DVASPECT_CONTENT;
		m_formats[index].lindex = -1;
		m_formats[index].ptd = nullptr;
		m_formats[index].tymed = TYMED_HGLOBAL;
		index++;
	}
	else if (containsExistingFiles)
	{
		m_formats[index].cfFormat = CF_FILECONTENTS;
		m_formats[index].dwAspect = DVASPECT_CONTENT;
		m_formats[index].lindex = -1;
		m_formats[index].ptd = nullptr;
		m_formats[index].tymed = TYMED_ISTREAM;
		index++;

		m_formats[index].cfFormat = CF_FILEDESCRIPTOR;
		m_formats[index].dwAspect = DVASPECT_CONTENT;
		m_formats[index].lindex = -1;
		m_formats[index].ptd = nullptr;
		m_formats[index].tymed = TYMED_HGLOBAL;
		index++;
	}
	// clear possible leftovers
	while (index < GITDATAOBJECT_NUMFORMATS)
	{
		m_formats[index].cfFormat = 0;
		m_formats[index].dwAspect = 0;
		m_formats[index].lindex = -1;
		m_formats[index].ptd = nullptr;
		m_formats[index].tymed = 0;
		index++;
	}
}

CGitEnumFormatEtc::CGitEnumFormatEtc(const std::vector<FORMATETC>& vec, bool localonly, bool containsExistingFiles)
	: m_localonly(localonly)
	, m_containsExistingFiles(containsExistingFiles)
{
	for (size_t i = 0; i < vec.size(); ++i)
		m_vecFormatEtc.push_back(vec[i]);
	Init(localonly, containsExistingFiles);
}

CGitEnumFormatEtc::CGitEnumFormatEtc(const std::vector<FORMATETC*>& vec, bool localonly, bool containsExistingFiles)
	: m_localonly(localonly)
	, m_containsExistingFiles(containsExistingFiles)
{
	for (size_t i = 0; i < vec.size(); ++i)
		m_vecFormatEtc.push_back(*vec[i]);
	Init(localonly, containsExistingFiles);
}

STDMETHODIMP  CGitEnumFormatEtc::QueryInterface(REFIID refiid, void** ppv)
{
	if (!ppv)
		return E_POINTER;
	*ppv = nullptr;
	if (IID_IUnknown == refiid || IID_IEnumFORMATETC == refiid)
		*ppv = static_cast<IEnumFORMATETC*>(this);
	else
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) CGitEnumFormatEtc::AddRef()
{
	return ++m_cRefCount;
}

STDMETHODIMP_(ULONG) CGitEnumFormatEtc::Release()
{
	--m_cRefCount;
	if (m_cRefCount == 0)
	{
		delete this;
		return 0;
	}
	return m_cRefCount;
}

STDMETHODIMP CGitEnumFormatEtc::Next(ULONG celt, LPFORMATETC lpFormatEtc, ULONG* pceltFetched)
{
	if (celt <= 0)
		return E_INVALIDARG;
	if (!pceltFetched && celt != 1) // pceltFetched can be NULL only for 1 item request
		return E_POINTER;
	if (!lpFormatEtc)
		return E_POINTER;

	if (pceltFetched)
		*pceltFetched = 0;

	if (m_iCur >= GITDATAOBJECT_NUMFORMATS)
		return S_FALSE;

	ULONG cReturn = celt;

	while (m_iCur < (GITDATAOBJECT_NUMFORMATS + m_vecFormatEtc.size()) && cReturn > 0)
	{
		if (m_iCur < GITDATAOBJECT_NUMFORMATS)
			*lpFormatEtc++ = m_formats[m_iCur++];
		else
			*lpFormatEtc++ = m_vecFormatEtc[m_iCur++ - GITDATAOBJECT_NUMFORMATS];
		--cReturn;
	}

	if (pceltFetched)
		*pceltFetched = celt - cReturn;

	return (cReturn == 0) ? S_OK : S_FALSE;
}

STDMETHODIMP CGitEnumFormatEtc::Skip(ULONG celt)
{
	if ((m_iCur + int(celt)) >= (GITDATAOBJECT_NUMFORMATS + m_vecFormatEtc.size()))
		return S_FALSE;
	m_iCur += celt;
	return S_OK;
}

STDMETHODIMP CGitEnumFormatEtc::Reset()
{
	m_iCur = 0;
	return S_OK;
}

STDMETHODIMP CGitEnumFormatEtc::Clone(IEnumFORMATETC** ppCloneEnumFormatEtc)
{
	if (!ppCloneEnumFormatEtc)
		return E_POINTER;

	CGitEnumFormatEtc *newEnum = new (std::nothrow) CGitEnumFormatEtc(m_vecFormatEtc, m_localonly, m_containsExistingFiles);
	if (!newEnum)
		return E_OUTOFMEMORY;

	newEnum->AddRef();
	newEnum->m_iCur = m_iCur;
	*ppCloneEnumFormatEtc = newEnum;
	return S_OK;
}

}

export using namespace GitDataObject;
