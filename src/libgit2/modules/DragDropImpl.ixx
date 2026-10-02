module;
// IDataObjectImpl.h: interface for the CIDataObjectImpl class.
/**************************************************************************
   THIS CODE AND INFORMATION IS PROVIDED 'AS IS' WITHOUT WARRANTY OF
   ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
   THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
   PARTICULAR PURPOSE.
   Author: Leon Finker  1/2001
**************************************************************************/
#ifndef __DRAGDROPIMPL_H__
#define __DRAGDROPIMPL_H__
//#include <ShlDisp.h>
#include <cassert>
#include <shobjidl.h>
#include <vector>


export module DragDrop;


export namespace DragDrop
{
	///////////////////////////////////////////////////////////////////////////////////////////////
	class CDragSourceNotify : IDropSourceNotify
	{
	private:
		LONG refCount = 0;

	public:
		CDragSourceNotify() = default;
		virtual ~CDragSourceNotify() = default;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
		{
			if(!ppvObject)
				return E_POINTER;

			if (IsEqualIID(riid, IID_IUnknown))
				*ppvObject = static_cast<IUnknown*>(this);
			else if (IsEqualIID(riid, IID_IDropSourceNotify))
				*ppvObject = static_cast<IDropSourceNotify*>(this);
			else
			{
				*ppvObject = nullptr;
				return E_NOINTERFACE;
			}

			AddRef();
			return S_OK;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return InterlockedIncrement(&refCount);
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			ULONG ret = InterlockedDecrement(&refCount);
			if(!ret) {
				delete this;
			}
			return ret;
		}

		HRESULT STDMETHODCALLTYPE DragEnterTarget(HWND /*hWndTarget*/) override
		{
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE DragLeaveTarget() override
		{
			return S_OK;
		}
	};


	///////////////////////////////////////////////////////////////////////////////////////////////

	class CEnumFormatEtc : public IEnumFORMATETC
	{
	private:
		ULONG m_cRefCount = 0;
		std::vector<FORMATETC>  m_pFmtEtc;
		int m_iCur = 0;

	public:
		virtual ~CEnumFormatEtc() = default;

		CEnumFormatEtc(const std::vector<FORMATETC>& ArrFE) {
			assert("CEnumFormatEtc::CEnumFormatEtc()\n");
			for (int i = 0; i < ArrFE.size(); ++i)
				m_pFmtEtc.emplace_back(ArrFE[i]);
		}

		CEnumFormatEtc(const std::vector<FORMATETC*>& ArrFE) {
			for (int i = 0; i < ArrFE.size(); ++i)
				m_pFmtEtc.emplace_back(*ArrFE[i]);
		}

		//IUnknown members
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
		ULONG STDMETHODCALLTYPE AddRef() override;
		ULONG STDMETHODCALLTYPE Release() override;

		//IEnumFORMATETC members
		STDMETHODIMP Next(ULONG, LPFORMATETC, ULONG*) override;
		STDMETHODIMP Skip(ULONG) override;
		STDMETHODIMP Reset() override;
		STDMETHODIMP Clone(IEnumFORMATETC**) override;
	};

	///////////////////////////////////////////////////////////////////////////////////////////////
	class CIDropSource : public IDropSource
	{
		long m_cRefCount;
	public:
		bool m_bDropped;
		IDataObject * m_pIDataObj;
		CDragSourceNotify* pDragSourceNotify;


		CIDropSource()
		: m_cRefCount(0)
		, m_bDropped(false)
		, m_pIDataObj(nullptr)
		{
			pDragSourceNotify = new CDragSourceNotify();
			pDragSourceNotify->AddRef();
		}
		virtual ~CIDropSource()
		{
			if (m_pIDataObj)
			{
				m_pIDataObj->Release();
				m_pIDataObj = nullptr;
			}
			if (pDragSourceNotify)
			{
				pDragSourceNotify->Release();
				pDragSourceNotify = nullptr;
			}
		}
		//IUnknown
		HRESULT STDMETHODCALLTYPE QueryInterface(
				/* [in] */ REFIID riid,
				/* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject) override;
		ULONG STDMETHODCALLTYPE AddRef() override;
		ULONG STDMETHODCALLTYPE Release() override;
		//IDropSource
		HRESULT STDMETHODCALLTYPE QueryContinueDrag(
			/* [in] */ BOOL fEscapePressed,
			/* [in] */ DWORD grfKeyState) override;

		HRESULT STDMETHODCALLTYPE GiveFeedback(
			/* [in] */ DWORD dwEffect) override;
	};

	///////////////////////////////////////////////////////////////////////////////////////////////
	class CIDataObject : public IDataObject//,public IAsyncOperation
	{
		CIDropSource* m_pDropSource = nullptr;
		long m_cRefCount = 0;
		std::vector<FORMATETC*> m_ArrFormatEtc;
		std::vector<STGMEDIUM*> m_StgMedium;
	public:
		CIDataObject(CIDropSource* pDropSource) : m_pDropSource(pDropSource) {}

		virtual ~CIDataObject() {
			for (int i = 0; i < m_StgMedium.size(); ++i) {
				ReleaseStgMedium(m_StgMedium[i]);
				delete m_StgMedium[i];
			}
			for (int j = 0; j < m_ArrFormatEtc.size(); ++j)
				delete m_ArrFormatEtc[j];
		}

		void CopyMedium(STGMEDIUM* pMedDest, STGMEDIUM* pMedSrc, FORMATETC* pFmtSrc);
		//IUnknown
		HRESULT STDMETHODCALLTYPE QueryInterface(
				/* [in] */ REFIID riid,
				/* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject) override;
		ULONG STDMETHODCALLTYPE AddRef() override;
		ULONG STDMETHODCALLTYPE Release() override;

		//IDataObject
		/* [local] */ HRESULT STDMETHODCALLTYPE GetData(
			/* [unique][in] */ FORMATETC __RPC_FAR *pformatetcIn,
			/* [out] */ STGMEDIUM __RPC_FAR *pmedium) override;

		/* [local] */ HRESULT STDMETHODCALLTYPE GetDataHere(
			/* [unique][in] */ FORMATETC __RPC_FAR *pformatetc,
			/* [out][in] */ STGMEDIUM __RPC_FAR *pmedium) override;

		HRESULT STDMETHODCALLTYPE QueryGetData(
			/* [unique][in] */ FORMATETC __RPC_FAR *pformatetc) override;

		HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(
			/* [unique][in] */ FORMATETC __RPC_FAR *pformatectIn,
			/* [out] */ FORMATETC __RPC_FAR *pformatetcOut) override;

		/* [local] */ HRESULT STDMETHODCALLTYPE SetData(
			/* [unique][in] */ FORMATETC __RPC_FAR *pformatetc,
			/* [unique][in] */ STGMEDIUM __RPC_FAR *pmedium,
			/* [in] */ BOOL fRelease) override;

		HRESULT STDMETHODCALLTYPE EnumFormatEtc(
			/* [in] */ DWORD dwDirection,
			/* [out] */ IEnumFORMATETC __RPC_FAR *__RPC_FAR *ppenumFormatEtc) override;

		HRESULT STDMETHODCALLTYPE DAdvise(
			/* [in] */ FORMATETC __RPC_FAR *pformatetc,
			/* [in] */ DWORD advf,
			/* [unique][in] */ IAdviseSink __RPC_FAR *pAdvSink,
			/* [out] */ DWORD __RPC_FAR *pdwConnection) override;

		HRESULT STDMETHODCALLTYPE DUnadvise(
			/* [in] */ DWORD dwConnection) override;

		HRESULT STDMETHODCALLTYPE EnumDAdvise(
			/* [out] */ IEnumSTATDATA __RPC_FAR *__RPC_FAR *ppenumAdvise) override;

		//IAsyncOperation
		//virtual HRESULT STDMETHODCALLTYPE SetAsyncMode(
		//    /* [in] */ BOOL fDoOpAsync)
		//{
		//	return E_NOTIMPL;
		//}
		//
		//virtual HRESULT STDMETHODCALLTYPE GetAsyncMode(
		//    /* [out] */ BOOL __RPC_FAR *pfIsOpAsync)
		//{
		//	return E_NOTIMPL;
		//}
		//
		//virtual HRESULT STDMETHODCALLTYPE StartOperation(
		//    /* [optional][unique][in] */ IBindCtx __RPC_FAR *pbcReserved)
		//{
		//	return E_NOTIMPL;
		//}
		//
		//virtual HRESULT STDMETHODCALLTYPE InOperation(
		//    /* [out] */ BOOL __RPC_FAR *pfInAsyncOp)
		//{
		//	return E_NOTIMPL;
		//}
		//
		//virtual HRESULT STDMETHODCALLTYPE EndOperation(
		//    /* [in] */ HRESULT hResult,
		//    /* [unique][in] */ IBindCtx __RPC_FAR *pbcReserved,
		//    /* [in] */ DWORD dwEffects)
		//{
		//	return E_NOTIMPL;
		//}


		// helper function
		HRESULT SetDropDescription(DROPIMAGETYPE image, LPCWSTR format, LPCWSTR insert);
	};

	///////////////////////////////////////////////////////////////////////////////////////////////
	class CIDropTarget : public IDropTarget
	{
		DWORD m_cRefCount = 0;
		bool m_bAllowDrop = false;
		IDropTargetHelper* m_pDropTargetHelper = nullptr;
		std::vector<FORMATETC> m_formatetc;
		FORMATETC* m_pSupportedFrmt = nullptr;
	protected:
		HWND m_hTargetWnd = nullptr;
		IDataObject* m_pIDataObject = nullptr;
	public:

		CIDropTarget(HWND m_hTargetWnd);
		virtual ~CIDropTarget();
		void AddSuportedFormat(FORMATETC& ftetc) { m_formatetc.emplace_back(ftetc); }

		//return values: true - release the medium. false - don't release the medium
		virtual bool OnDrop(FORMATETC* pFmtEtc, STGMEDIUM& medium,DWORD *pdwEffect, POINTL pt) = 0;

		HRESULT STDMETHODCALLTYPE QueryInterface(
			/* [in] */ REFIID riid,
			/* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject) override;
		ULONG STDMETHODCALLTYPE AddRef() override { assert("CIDropTarget::AddRef\n"); return ++m_cRefCount; }
		ULONG STDMETHODCALLTYPE Release() override;

		bool QueryDrop(DWORD grfKeyState, LPDWORD pdwEffect);
		HRESULT STDMETHODCALLTYPE DragEnter(
			/* [unique][in] */ IDataObject __RPC_FAR *pDataObj,
			/* [in] */ DWORD grfKeyState,
			/* [in] */ POINTL pt,
			/* [out][in] */ DWORD __RPC_FAR *pdwEffect) override;
		HRESULT STDMETHODCALLTYPE DragOver(
			/* [in] */ DWORD grfKeyState,
			/* [in] */ POINTL pt,
			/* [out][in] */ DWORD __RPC_FAR *pdwEffect) override;
		HRESULT STDMETHODCALLTYPE DragLeave() override;
		HRESULT STDMETHODCALLTYPE Drop(
			/* [unique][in] */ IDataObject __RPC_FAR *pDataObj,
			/* [in] */ DWORD grfKeyState,
			/* [in] */ POINTL pt,
			/* [out][in] */ DWORD __RPC_FAR *pdwEffect) override;

		// helper function
		HRESULT SetDropDescription(DROPIMAGETYPE image, LPCWSTR format, LPCWSTR insert);
	};

	class CDragSourceHelper
	{
		IDragSourceHelper2* pDragSourceHelper2 = nullptr;
		IDragSourceHelper* pDragSourceHelper = nullptr;

	public:
		CDragSourceHelper()
		{
			if(FAILED(CoCreateInstance(CLSID_DragDropHelper,
							nullptr,
							CLSCTX_INPROC_SERVER,
							IID_IDragSourceHelper2,
							reinterpret_cast<void**>(&pDragSourceHelper2))))
			{
				pDragSourceHelper2 = nullptr;
				if(FAILED(CoCreateInstance(CLSID_DragDropHelper,
					nullptr,
					CLSCTX_INPROC_SERVER,
					IID_IDragSourceHelper,
					reinterpret_cast<void**>(&pDragSourceHelper))))
					pDragSourceHelper = nullptr;
			}
		}
		virtual ~CDragSourceHelper()
		{
			if (pDragSourceHelper2)
			{
				pDragSourceHelper2->Release();
				pDragSourceHelper2 = nullptr;
			}
			if (pDragSourceHelper)
			{
				pDragSourceHelper->Release();
				pDragSourceHelper = nullptr;
			}
		}

		// IDragSourceHelper
		HRESULT InitializeFromBitmap(HBITMAP hBitmap,
			const POINT& pt,  // cursor position in client coords of the window
			const RECT& rc,   // selected item's bounding rect
			IDataObject* pDataObject,
			BOOL allowDropDescription=TRUE,
			COLORREF crColorKey=GetSysColor(COLOR_WINDOW)// color of the window used for transparent effect.
			)
		{
			if (!pDragSourceHelper && !pDragSourceHelper2)
				return E_FAIL;

			if ((allowDropDescription)&&(pDragSourceHelper2))
				pDragSourceHelper2->SetFlags(DSH_ALLOWDROPDESCRIPTIONTEXT);

			SHDRAGIMAGE di;
			BITMAP      bm;
			GetObject(hBitmap, sizeof(bm), &bm);
			di.sizeDragImage.cx = bm.bmWidth;
			di.sizeDragImage.cy = bm.bmHeight;
			di.hbmpDragImage = hBitmap;
			di.crColorKey = crColorKey;
			di.ptOffset.x = pt.x - rc.left;
			di.ptOffset.y = pt.y - rc.top;
			if (pDragSourceHelper2)
				return pDragSourceHelper2->InitializeFromBitmap(&di, pDataObject);
			if (!pDragSourceHelper)
				return E_FAIL;
			return pDragSourceHelper->InitializeFromBitmap(&di, pDataObject);
		}
		HRESULT InitializeFromWindow(HWND hwnd, POINT& pt, IDataObject* pDataObject, BOOL allowDropDescription=TRUE)
		{
			if (!pDragSourceHelper && !pDragSourceHelper2)
				return E_FAIL;
			if ((allowDropDescription)&&(pDragSourceHelper2))
				pDragSourceHelper2->SetFlags(DSH_ALLOWDROPDESCRIPTIONTEXT);
			if (pDragSourceHelper2)
				return pDragSourceHelper2->InitializeFromWindow(hwnd, &pt, pDataObject);
			if (!pDragSourceHelper)
				return E_FAIL;
			return pDragSourceHelper->InitializeFromWindow(hwnd, &pt, pDataObject);
		}
	};
#endif //__DRAGDROPIMPL_H__


	/// A DROPDESCRIPTION in the HGLOBAL form SetData takes. A null \a format or
	/// \a insert leaves that field empty.
	static wil::unique_hglobal MakeDropDescription(const DROPIMAGETYPE image, const LPCWSTR format, const LPCWSTR insert)
	{
		DROPDESCRIPTION description{};
		description.type = image;
		if (insert)
			tgit::wstr::CopyTruncating(description.szInsert, insert);
		if (format)
			tgit::wstr::CopyTruncating(description.szMessage, format);
		return wil::make_moveable_hglobal(std::as_bytes(std::span(&description, 1)));
	}

	//////////////////////////////////////////////////////////////////////
	// CIDataObject Class
	//////////////////////////////////////////////////////////////////////

	STDMETHODIMP CIDataObject::QueryInterface(/* [in] */ REFIID riid,
	/* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject)
	{
		if (!ppvObject)
			return E_POINTER;
		*ppvObject = nullptr;
		if (IsEqualIID(IID_IUnknown, riid) || IsEqualIID(IID_IDataObject, riid))
			*ppvObject = static_cast<IDataObject*>(this);
		/*else if(riid == IID_IAsyncOperation)
			*ppvObject = static_cast<IAsyncOperation*>(this);*/
		else
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}

	STDMETHODIMP_(ULONG) CIDataObject::AddRef( void)
	{
		return ++m_cRefCount;
	}

	STDMETHODIMP_(ULONG) CIDataObject::Release( void)
	{
		long nTemp = --m_cRefCount;
		if(nTemp==0)
			delete this;
		return nTemp;
	}

	STDMETHODIMP CIDataObject::GetData(
		/* [unique][in] */ FORMATETC __RPC_FAR *pformatetcIn,
		/* [out] */ STGMEDIUM __RPC_FAR *pmedium)
	{
		if (!pformatetcIn)
			return E_INVALIDARG;
		if (!pmedium)
			return E_POINTER;
		pmedium->hGlobal = nullptr;

		assert(m_StgMedium.size() == m_ArrFormatEtc.size());
		for(int i=0; i < m_ArrFormatEtc.size(); ++i)
		{
			if(pformatetcIn->tymed & m_ArrFormatEtc[i]->tymed &&
				pformatetcIn->dwAspect == m_ArrFormatEtc[i]->dwAspect &&
				pformatetcIn->cfFormat == m_ArrFormatEtc[i]->cfFormat)
			{
				CopyMedium(pmedium, m_StgMedium[i], m_ArrFormatEtc[i]);
				return S_OK;
			}
		}
		return DV_E_FORMATETC;
	}

	STDMETHODIMP CIDataObject::GetDataHere(
		/* [unique][in] */ FORMATETC __RPC_FAR * /*pformatetc*/,
		/* [out][in] */ STGMEDIUM __RPC_FAR * /*pmedium*/)
	{
		return E_NOTIMPL;
	}

	STDMETHODIMP CIDataObject::QueryGetData(
		/* [unique][in] */ FORMATETC __RPC_FAR *pformatetc)
	{
		if (!pformatetc)
			return E_INVALIDARG;

		//support others if needed DVASPECT_THUMBNAIL  //DVASPECT_ICON   //DVASPECT_DOCPRINT
		if (!(DVASPECT_CONTENT & pformatetc->dwAspect))
			return (DV_E_DVASPECT);
		HRESULT  hr = DV_E_TYMED;
		for(int i = 0; i < m_ArrFormatEtc.size(); ++i)
		{
			if(pformatetc->tymed & m_ArrFormatEtc[i]->tymed)
			{
				if(pformatetc->cfFormat == m_ArrFormatEtc[i]->cfFormat)
					return S_OK;
				else
					hr = DV_E_CLIPFORMAT;
			}
			else
				hr = DV_E_TYMED;
		}
		return hr;
	}

	STDMETHODIMP CIDataObject::GetCanonicalFormatEtc(
		/* [unique][in] */ FORMATETC __RPC_FAR * /*pformatectIn*/,
		/* [out] */ FORMATETC __RPC_FAR *pformatetcOut)
	{
		if (!pformatetcOut)
			return E_INVALIDARG;
		return DATA_S_SAMEFORMATETC;
	}

	STDMETHODIMP CIDataObject::SetData(
		/* [unique][in] */ FORMATETC __RPC_FAR *pformatetc,
		/* [unique][in] */ STGMEDIUM __RPC_FAR *pmedium,
		/* [in] */ BOOL fRelease)
	{
		if (!pformatetc || !pmedium)
			return E_INVALIDARG;

		assert(pformatetc->tymed == pmedium->tymed);
		FORMATETC* fetc = new (std::nothrow) FORMATETC;
		STGMEDIUM* pStgMed = new (std::nothrow) STGMEDIUM;

		if (!fetc || !pStgMed)
		{
			delete fetc;
			delete pStgMed;
			return E_OUTOFMEMORY;
		}

		SecureZeroMemory(fetc,sizeof(FORMATETC));
		SecureZeroMemory(pStgMed,sizeof(STGMEDIUM));

		// do we already store this format?
		for (int i=0; i<m_ArrFormatEtc.size(); ++i)
		{
			if ((pformatetc->tymed == m_ArrFormatEtc[i]->tymed) &&
				(pformatetc->dwAspect == m_ArrFormatEtc[i]->dwAspect) &&
				(pformatetc->cfFormat == m_ArrFormatEtc[i]->cfFormat))
			{
				// yes, this format is already in our object:
				// we have to replace the existing format. To do that, we
				// remove the format we already have
				delete m_ArrFormatEtc[i];
				m_ArrFormatEtc.erase(m_ArrFormatEtc.begin() + i);
				ReleaseStgMedium(m_StgMedium[i]);
				delete m_StgMedium[i];
				m_StgMedium.erase(m_StgMedium.begin() + i);
				break;
			}
		}

		*fetc = *pformatetc;
		m_ArrFormatEtc.emplace_back(fetc);

		if(fRelease)
			*pStgMed = *pmedium;
		else
		{
			CopyMedium(pStgMed, pmedium, pformatetc);
		}
		m_StgMedium.emplace_back(pStgMed);

		return S_OK;
	}

	void CIDataObject::CopyMedium(STGMEDIUM* pMedDest, STGMEDIUM* pMedSrc, FORMATETC* pFmtSrc)
	{
		switch(pMedSrc->tymed)
		{
		case TYMED_HGLOBAL:
			pMedDest->hGlobal = static_cast<HGLOBAL>(OleDuplicateData(pMedSrc->hGlobal, pFmtSrc->cfFormat, 0));
			break;
		case TYMED_GDI:
			pMedDest->hBitmap = static_cast<HBITMAP>(OleDuplicateData(pMedSrc->hBitmap,pFmtSrc->cfFormat, 0));
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

	STDMETHODIMP CIDataObject::EnumFormatEtc(
		/* [in] */ DWORD dwDirection,
		/* [out] */ IEnumFORMATETC __RPC_FAR *__RPC_FAR *ppenumFormatEtc)
	{
		if (!ppenumFormatEtc)
			return E_POINTER;

		*ppenumFormatEtc = nullptr;
		switch (dwDirection)
		{
		case DATADIR_GET:
			*ppenumFormatEtc = new (std::nothrow) CEnumFormatEtc(m_ArrFormatEtc);
			if (!*ppenumFormatEtc)
				return E_OUTOFMEMORY;
			(*ppenumFormatEtc)->AddRef();
			break;
		case DATADIR_SET:
		default:
			return E_NOTIMPL;
			break;
		}
		return S_OK;
	}

	STDMETHODIMP CIDataObject::DAdvise(
		/* [in] */ FORMATETC __RPC_FAR * /*pformatetc*/,
		/* [in] */ DWORD /*advf*/,
		/* [unique][in] */ IAdviseSink __RPC_FAR * /*pAdvSink*/,
		/* [out] */ DWORD __RPC_FAR * /*pdwConnection*/)
	{
		return OLE_E_ADVISENOTSUPPORTED;
	}

	STDMETHODIMP CIDataObject::DUnadvise(
		/* [in] */ DWORD /*dwConnection*/)
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE CIDataObject::EnumDAdvise(
		/* [out] */ IEnumSTATDATA __RPC_FAR *__RPC_FAR * /*ppenumAdvise*/)
	{
		return OLE_E_ADVISENOTSUPPORTED;
	}


	HRESULT CIDataObject::SetDropDescription(DROPIMAGETYPE image, LPCWSTR format, LPCWSTR insert)
	{
		if (!format || !insert)
			return E_INVALIDARG;

		FORMATETC fetc = {0};
		fetc.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_DROPDESCRIPTION));
		fetc.dwAspect = DVASPECT_CONTENT;
		fetc.lindex = -1;
		fetc.tymed = TYMED_HGLOBAL;

		auto block = MakeDropDescription(image, format, insert);
		if (!block)
			return E_OUTOFMEMORY;
		STGMEDIUM medium = {};
		medium.tymed = TYMED_HGLOBAL; // SetData asserts it matches fetc.tymed; this used to be left TYMED_NULL
		medium.hGlobal = block.get();
		const HRESULT hr = SetData(&fetc, &medium, TRUE);
		if (SUCCEEDED(hr))
			block.release(); // the data object owns it now; on failure we still do
		return hr;
	}

	//////////////////////////////////////////////////////////////////////
	// CIDropSource Class
	//////////////////////////////////////////////////////////////////////

	STDMETHODIMP CIDropSource::QueryInterface(/* [in] */ REFIID riid,
											 /* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject)
	{
		if (!ppvObject)
		{
			return E_POINTER;
		}

		if (IsEqualIID(riid, IID_IUnknown))
		{
			*ppvObject = static_cast<IDropSource*>(this);
		}
		else if (IsEqualIID(riid, IID_IDropSource))
		{
			*ppvObject = static_cast<IDropSource*>(this);
		}
		else if (IsEqualIID(riid, IID_IDropSourceNotify) && pDragSourceNotify)
		{
			return pDragSourceNotify->QueryInterface(riid, ppvObject);
		}
		else
		{
			*ppvObject = nullptr;
			return E_NOINTERFACE;
		}

		AddRef();
		return S_OK;
	}

	STDMETHODIMP_(ULONG) CIDropSource::AddRef( void)
	{
		return ++m_cRefCount;
	}

	STDMETHODIMP_(ULONG) CIDropSource::Release( void)
	{
		long nTemp = --m_cRefCount;
		assert(nTemp >= 0);
		if(nTemp==0)
			delete this;
		return nTemp;
	}

	STDMETHODIMP CIDropSource::QueryContinueDrag(
		/* [in] */ BOOL fEscapePressed,
		/* [in] */ DWORD grfKeyState)
	{
		if(fEscapePressed)
			return DRAGDROP_S_CANCEL;
		if(!(grfKeyState & (MK_LBUTTON|MK_RBUTTON)))
		{
			m_bDropped = true;
			return DRAGDROP_S_DROP;
		}

		return S_OK;
	}

	STDMETHODIMP CIDropSource::GiveFeedback(
		/* [in] */ DWORD /*dwEffect*/)
	{
		if (m_pIDataObj)
		{
			FORMATETC fetc = {0};
			fetc.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormat(L"DragWindow"));
			fetc.dwAspect = DVASPECT_CONTENT;
			fetc.lindex = -1;
			fetc.tymed = TYMED_HGLOBAL;
			if (m_pIDataObj->QueryGetData(&fetc) == S_OK)
			{
				STGMEDIUM medium;
				if (m_pIDataObj->GetData(&fetc, &medium) == S_OK)
				{
					auto hWndDragWindow = *static_cast<HWND*>(GlobalLock(medium.hGlobal));
					GlobalUnlock(medium.hGlobal);
#define WM_INVALIDATEDRAGIMAGE (WM_USER + 3)
					SendMessage(hWndDragWindow, WM_INVALIDATEDRAGIMAGE, 0, 0);
					ReleaseStgMedium(&medium);
				}
			}
		}
		return DRAGDROP_S_USEDEFAULTCURSORS;
	}

	//////////////////////////////////////////////////////////////////////
	// CEnumFormatEtc Class
	//////////////////////////////////////////////////////////////////////


	STDMETHODIMP CEnumFormatEtc::QueryInterface(REFIID refiid, void FAR* FAR* ppv)
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

	STDMETHODIMP_(ULONG) CEnumFormatEtc::AddRef()
	{
		return ++m_cRefCount;
	}

	STDMETHODIMP_(ULONG) CEnumFormatEtc::Release()
	{
		long nTemp = --m_cRefCount;
		assert(nTemp >= 0);
		if(nTemp == 0)
			delete this;

		return nTemp;
	}

	STDMETHODIMP CEnumFormatEtc::Next( ULONG celt,LPFORMATETC lpFormatEtc, ULONG FAR *pceltFetched)
	{
		if(celt <= 0)
			return E_INVALIDARG;
		if (!pceltFetched && celt != 1) // pceltFetched can be nullptr only for 1 item request
			return E_POINTER;
		if (!lpFormatEtc)
			return E_POINTER;

		if (pceltFetched)
			*pceltFetched = 0;
		if (m_iCur >= m_pFmtEtc.size())
			return S_FALSE;

		ULONG cReturn = celt;
		while (m_iCur < m_pFmtEtc.size() && cReturn > 0)
		{
			*lpFormatEtc++ = m_pFmtEtc[m_iCur++];
			--cReturn;
		}
		if (pceltFetched)
			*pceltFetched = celt - cReturn;

		return (cReturn == 0) ? S_OK : S_FALSE;
	}

	STDMETHODIMP CEnumFormatEtc::Skip(ULONG celt)
	{
		if((m_iCur + int(celt)) >= m_pFmtEtc.size())
			return S_FALSE;
		m_iCur += celt;
		return S_OK;
	}

	STDMETHODIMP CEnumFormatEtc::Reset()
	{
		m_iCur = 0;
		return S_OK;
	}

	STDMETHODIMP CEnumFormatEtc::Clone(IEnumFORMATETC FAR * FAR*ppCloneEnumFormatEtc)
	{
		if (!ppCloneEnumFormatEtc)
			return E_POINTER;

		CEnumFormatEtc *newEnum = new (std::nothrow) CEnumFormatEtc(m_pFmtEtc);
		if (!newEnum)
			return E_OUTOFMEMORY;
		newEnum->AddRef();
		newEnum->m_iCur = m_iCur;
		*ppCloneEnumFormatEtc = newEnum;
		return S_OK;
	}

	//////////////////////////////////////////////////////////////////////
	// CIDropTarget Class
	//////////////////////////////////////////////////////////////////////
	CIDropTarget::CIDropTarget(HWND hTargetWnd)
		: m_hTargetWnd(hTargetWnd)
	{
		if (FAILED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_IDropTargetHelper, reinterpret_cast<LPVOID*>(&m_pDropTargetHelper))))
			m_pDropTargetHelper = nullptr;
	}

	CIDropTarget::~CIDropTarget()
	{
		if (m_pDropTargetHelper)
		{
			m_pDropTargetHelper->Release();
			m_pDropTargetHelper = nullptr;
		}
	}

	HRESULT STDMETHODCALLTYPE CIDropTarget::QueryInterface( /* [in] */ REFIID riid,
							/* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject)
	{
		if (!ppvObject)
			return E_POINTER;
		*ppvObject = nullptr;
		if (IID_IUnknown == riid || IID_IDropTarget == riid)
			*ppvObject = static_cast<IDropTarget*>(this);
		else
			return E_NOINTERFACE;

		AddRef();
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE CIDropTarget::Release( void)
	{
		long nTemp = --m_cRefCount;
		assert(nTemp >= 0);
		if(nTemp==0)
			delete this;
		return nTemp;
	}

	bool CIDropTarget::QueryDrop(DWORD grfKeyState, LPDWORD pdwEffect)
	{
		DWORD dwOKEffects = *pdwEffect;

		if(!m_bAllowDrop)
		{
			*pdwEffect = DROPEFFECT_NONE;
			return false;
		}
		//CTRL+SHIFT  -- DROPEFFECT_LINK
		//CTRL        -- DROPEFFECT_COPY
		//SHIFT       -- DROPEFFECT_MOVE
		//no modifier -- DROPEFFECT_MOVE or whatever is allowed by src
		*pdwEffect = (grfKeyState & MK_CONTROL) ?
					( (grfKeyState & MK_SHIFT) ? DROPEFFECT_LINK : DROPEFFECT_COPY ):
					( (grfKeyState & MK_SHIFT) ? DROPEFFECT_MOVE : 0 );
		if(*pdwEffect == 0)
		{
			// No modifier keys used by user while dragging.
			if (DROPEFFECT_MOVE & dwOKEffects)
				*pdwEffect = DROPEFFECT_MOVE;
			else if (DROPEFFECT_COPY & dwOKEffects)
				*pdwEffect = DROPEFFECT_COPY;
			else if (DROPEFFECT_LINK & dwOKEffects)
				*pdwEffect = DROPEFFECT_LINK;
			else
			{
				*pdwEffect = DROPEFFECT_NONE;
			}
		}
		else
		{
			// Check if the drag source application allows the drop effect desired by user.
			// The drag source specifies this in DoDragDrop
			if(!(*pdwEffect & dwOKEffects))
				*pdwEffect = DROPEFFECT_NONE;
		}

		return (DROPEFFECT_NONE == *pdwEffect)?false:true;
	}

	HRESULT STDMETHODCALLTYPE CIDropTarget::DragEnter(
		/* [unique][in] */ IDataObject __RPC_FAR *pDataObj,
		/* [in] */ DWORD grfKeyState,
		/* [in] */ POINTL pt,
		/* [out][in] */ DWORD __RPC_FAR *pdwEffect)
	{
		if (!pDataObj)
			return E_INVALIDARG;
		if (!pdwEffect)
			return E_POINTER;

		pDataObj->AddRef();
		m_pIDataObject = pDataObj;

		if(m_pDropTargetHelper)
			m_pDropTargetHelper->DragEnter(m_hTargetWnd, pDataObj, reinterpret_cast<LPPOINT>(&pt), *pdwEffect);

		m_pSupportedFrmt = nullptr;
		for(int i =0; i<m_formatetc.size(); ++i)
		{
			m_bAllowDrop = (pDataObj->QueryGetData(&m_formatetc[i]) == S_OK);
			if(m_bAllowDrop)
			{
				m_pSupportedFrmt = &m_formatetc[i];
				break;
			}
		}

		QueryDrop(grfKeyState, pdwEffect);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE CIDropTarget::DragOver(
			/* [in] */ DWORD grfKeyState,
			/* [in] */ POINTL pt,
			/* [out][in] */ DWORD __RPC_FAR *pdwEffect)
	{
		if (!pdwEffect)
			return E_POINTER;
		if(m_pDropTargetHelper)
			m_pDropTargetHelper->DragOver(reinterpret_cast<LPPOINT>(&pt), *pdwEffect);
		QueryDrop(grfKeyState, pdwEffect);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE CIDropTarget::DragLeave( void)
	{
		if(m_pDropTargetHelper)
			m_pDropTargetHelper->DragLeave();

		m_bAllowDrop = false;
		m_pSupportedFrmt = nullptr;
		m_pIDataObject->Release();
		m_pIDataObject = nullptr;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE CIDropTarget::Drop(
		/* [unique][in] */ IDataObject __RPC_FAR *pDataObj,
		/* [in] */ DWORD grfKeyState, /* [in] */ POINTL pt,
		/* [out][in] */ DWORD __RPC_FAR *pdwEffect)
	{
		if (!pDataObj)
			return E_INVALIDARG;
		if (!pdwEffect)
			return E_POINTER;

		if(m_pDropTargetHelper)
			m_pDropTargetHelper->Drop(pDataObj, reinterpret_cast<LPPOINT>(&pt), *pdwEffect);

		if(QueryDrop(grfKeyState, pdwEffect))
		{
			if (m_bAllowDrop && m_pSupportedFrmt)
			{
				STGMEDIUM medium;
				if(pDataObj->GetData(m_pSupportedFrmt, &medium) == S_OK)
				{
					if(OnDrop(m_pSupportedFrmt, medium, pdwEffect, pt)) //does derive class wants us to free medium?
						ReleaseStgMedium(&medium);
				}
			}
		}
		m_bAllowDrop=false;
		*pdwEffect = DROPEFFECT_NONE;
		m_pSupportedFrmt = nullptr;
		if (m_pIDataObject) // just in case we get a drop without first receiving DragEnter()
			m_pIDataObject->Release();
		m_pIDataObject = nullptr;
		return S_OK;
	}

	HRESULT CIDropTarget::SetDropDescription(DROPIMAGETYPE image, LPCWSTR format, LPCWSTR insert)
	{
		HRESULT hr = E_OUTOFMEMORY;

		FORMATETC fetc = {0};
		fetc.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_DROPDESCRIPTION));
		fetc.dwAspect = DVASPECT_CONTENT;
		fetc.lindex = -1;
		fetc.tymed = TYMED_HGLOBAL;

		auto block = MakeDropDescription(image, format, insert);
		if (!block)
			return hr;
		STGMEDIUM medium = {};
		medium.tymed = TYMED_HGLOBAL;
		medium.hGlobal = block.get();
		hr = m_pIDataObject->SetData(&fetc, &medium, TRUE);
		if (SUCCEEDED(hr))
			block.release(); // the data object owns it now; on failure we still do
		return hr;
	}


	//////////////////////////////////////////////////////////////////////
	// CIDragSourceHelper Class
	//////////////////////////////////////////////////////////////////////
}