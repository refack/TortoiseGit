export module ItemIDList;


export class ItemIDList
{
public:
	ItemIDList(PCUITEMID_CHILD item, PCUIDLIST_RELATIVE parent);
	ItemIDList(PCIDLIST_ABSOLUTE item);

	int size() const;
	LPCSHITEMID get(int index) const;
	virtual ~ItemIDList();

	std::wstring toString(bool resolveLibraries = true);

	PCUITEMID_CHILD operator& ();
private:
	PCUITEMID_CHILD item_ = nullptr;
	PCUIDLIST_RELATIVE parent_ = nullptr;
	mutable int count_ = -1;
};


ItemIDList::ItemIDList(PCUITEMID_CHILD item, PCUIDLIST_RELATIVE parent)
	: item_(item)
	, parent_(parent)
{
}

ItemIDList::ItemIDList(PCIDLIST_ABSOLUTE item)
	: item_(static_cast<PCUITEMID_CHILD>(item))
{
}

ItemIDList::~ItemIDList()
{
}

int ItemIDList::size() const
{
	if (count_ == -1)
	{
		count_ = 0;
		if (item_)
		{
			LPCSHITEMID ptr = &item_->mkid;
			while (ptr && ptr->cb != 0)
			{
				++count_;
				LPCBYTE byte = reinterpret_cast<LPCBYTE>(ptr);
				byte += ptr->cb;
				ptr = reinterpret_cast<LPCSHITEMID>(byte);
			}
		}
	}
	return count_;
}

LPCSHITEMID ItemIDList::get(int index) const
{
	int count = 0;

	if (!item_)
		return nullptr;
	LPCSHITEMID ptr = &item_->mkid;
	if (!ptr)
		return nullptr;
	while (ptr->cb != 0)
	{
		if (count == index)
			break;

		++count;
		LPCBYTE byte = reinterpret_cast<LPCBYTE>(ptr);
		byte += ptr->cb;
		ptr = reinterpret_cast<LPCSHITEMID>(byte);
	}
	return ptr;
}

std::wstring ItemIDList::toString(bool resolveLibraries /*= true*/)
{
	CComPtr<IShellFolder> shellFolder;
	CComPtr<IShellFolder> parentFolder;
	std::wstring ret;

	if (FAILED(::SHGetDesktopFolder(&shellFolder)))
		return ret;
	if (!parent_ || FAILED(shellFolder->BindToObject(parent_, 0, IID_IShellFolder, reinterpret_cast<void**>(&parentFolder))))
		parentFolder = shellFolder;

	if (parentFolder && item_ != 0)
	{
		STRRET name;
		if (FAILED(parentFolder->GetDisplayNameOf(item_, SHGDN_NORMAL | SHGDN_FORPARSING, &name)))
			return ret;
		CComHeapPtr<wchar_t> szDisplayName;
		if (FAILED(StrRetToStr(&name, item_, &szDisplayName)) || !szDisplayName)
			return ret;
		ret = szDisplayName;
	}
	else
		return ret;

	if (!(resolveLibraries && ret.starts_with(L"::{")))
		return ret;

	CComPtr<IShellLibrary> plib;
	if (FAILED(plib.CoCreateInstance(CLSID_ShellLibrary, nullptr, CLSCTX_INPROC_SERVER)))
		return ret;

	CComPtr<IShellItem> psiLibrary;
	if (FAILED(SHCreateItemFromParsingName(ret.c_str(), nullptr, IID_PPV_ARGS(&psiLibrary))))
		return ret;

	if (FAILED(plib->LoadLibraryFromItem(psiLibrary, STGM_READ | STGM_SHARE_DENY_NONE)))
		return ret;

	CComPtr<IShellItem> psiSaveLocation;
	if (FAILED(plib->GetDefaultSaveFolder(DSFT_DETECT, IID_PPV_ARGS(&psiSaveLocation))))
		return ret;

	if (CComHeapPtr<WCHAR> pszName; SUCCEEDED(psiSaveLocation->GetDisplayName(SIGDN_FILESYSPATH, &pszName)))
		return std::wstring(pszName);

	return ret;
}

PCUITEMID_CHILD ItemIDList::operator& ()
{
	return item_;
}
