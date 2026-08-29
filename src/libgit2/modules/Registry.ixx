module;
#include <windows.h>
#include <Shlwapi.h>
#include "sanity.h"


export module Registry;
import std;
import gsl;
import FormatMessageWrapper;

#ifndef QWORD
using QWORD = std::uint64_t;
#endif
using reg_string_t = std::wstring;

export namespace Registry {

/**
 * \ingroup Utils
 * Base class for the registry classes.
 *
 * \par requirements
 * - win98 or later, win2k or later, win95 with IE4 or later, winNT4 with IE4 or later
 * - import library Shlwapi.lib
 */

template<class S>
class CRegBaseCommon
{
protected:
	/**
	 * String type specific operations.
	 */

	[[nodiscard]] virtual LPCWSTR GetPlainString(const S& s) const = 0;
	[[nodiscard]] virtual DWORD GetLength(const S& s) const = 0;

public:
	virtual ~CRegBaseCommon() = default;
	//methods

	/** Default constructor.
	 */
	CRegBaseCommon();
	/**
	 * Constructor.
	 * \param key the path to the key, including the key. example: "Software\\Company\\SubKey\\MyValue"
	 * \param force set to TRUE if no cache should be used, i.e. always read and write directly from/to registry
	 * \param base a predefined base key like HKEY_LOCAL_MACHINE. see the SDK documentation for more information.
	 * \param sam
	 */
	CRegBaseCommon(const S& key, bool force, HKEY base = HKEY_CURRENT_USER, REGSAM sam = 0);

	/**
	 * Removes the whole registry key including all values. So if you set the registry
	 * entry to be HKCU\Software\Company\Product\key\value there will only be
	 * HKCU\Software\Company\Product key in the registry.
	 * \return ERROR_SUCCESS or an nonzero error code. Use FormatMessage() to get an error description.
	 */
	DWORD removeKey();
	/**
	 * Removes the value of the registry object. If you set the registry entry to
	 * be HKCU\Software\Company\Product\key\value there will only be
	 * HKCU\Software\Company\Product\key\ in the registry.
	 * \return ERROR_SUCCESS or an nonzero error code. Use FormatMessage() to get an error description.
	 */
	LONG removeValue();

	/**
	 * Returns the string of the last error occurred.
	 */
	virtual S getErrorString()
	{
		return CFormatMessageWrapper{m_lastError}.c_str();
	}

	/// get failure info for last operation

	[[nodiscard]] LONG GetLastError() const
	{
		return m_lastError;
	}

	/// used in subclass templates to specify the correct string type

	using StringT = S;

protected:
	//members
	HKEY m_base;        ///< handle to the registry base
	S m_key;            ///< the name of the value
	S m_path;           ///< the path to the key
	LONG m_lastError;   ///< the value of the last error occurred
	REGSAM m_sam;       ///< the security attributes to pass to the registry command

	bool m_read;        ///< indicates if the value has already been attempted read from the registry
	bool m_force;       ///< indicates if no cache should be used, i.e. always read and write directly from registry
	bool m_exists;      ///< true, if the registry value actually exists
};

// implement CRegBaseCommon<> members

template<class S>
CRegBaseCommon<S>::CRegBaseCommon()
	: m_base(HKEY_CURRENT_USER)
	, m_key()
	, m_path()
	, m_lastError(ERROR_SUCCESS)
	, m_sam(0)
	, m_read(false)
	, m_force(false)
	, m_exists(false)
{
}

template<class S>
CRegBaseCommon<S>::CRegBaseCommon(const S& key, const bool force, const HKEY base, const REGSAM sam)
	: m_base(base)
	, m_key(key)
	, m_path()
	, m_lastError(ERROR_SUCCESS)
	, m_sam(sam)
	, m_read(false)
	, m_force(force)
	, m_exists(false)
{
}

template<class S>
DWORD CRegBaseCommon<S>::removeKey()
{
	m_exists = false;
	m_read = true;

	HKEY hKey = nullptr;
	RegOpenKeyEx(m_base, GetPlainString(m_path), 0, KEY_WRITE | m_sam, &hKey);
	auto ret = SHDeleteKey(m_base, GetPlainString(m_path));
	RegCloseKey(hKey);
	return ret;
}

template<class S>
LONG CRegBaseCommon<S>::removeValue()
{
	m_exists = false;
	m_read = true;

	HKEY hKey = nullptr;
	RegOpenKeyEx(m_base, GetPlainString(m_path), 0, KEY_WRITE | m_sam, &hKey);
	auto ret = RegDeleteValue(hKey, GetPlainString(m_key));
	RegCloseKey(hKey);
	return ret;
}

/**
 * \ingroup Utils
 * Base class for STL string type registry classes.
 */

class CRegBase : public CRegBaseCommon<reg_string_t>
{
protected:
	/**
	 * String type specific operations.
	 */

	[[nodiscard]] LPCWSTR GetPlainString(const reg_string_t& s) const override { return s.c_str(); }
	[[nodiscard]] DWORD GetLength(const reg_string_t& s) const override { return static_cast<DWORD>(s.size()); }

public: //methods
	using size_type = reg_string_t::size_type;

	CRegBase() {}

	CRegBase(const reg_string_t& key, const bool force, const HKEY base, const REGSAM sam)
		: CRegBaseCommon(key, force, base, sam)
	{
		const size_type pos = key.find_last_of(L'\\');
		m_path = key.substr(0, pos);
		m_key = key.substr(pos + 1);
	}
};

/**
 * \ingroup Utils
 * DWORD value in registry. with this class you can use DWORD values in registry
 * like normal DWORD variables in your program.
 * Usage:
 * in your header file, declare your registry DWORD variable:
 * \code
 * CRegDWORD regvalue;
 * \endcode
 * next initialize the variable e.g. in the constructor of your class:
 * \code
 * regvalue = CRegDWORD("Software\\Company\\SubKey\\MyValue", 100);
 * \endcode
 * this will set the registry value "MyValue" under HKEY_CURRENT_USER with path
 * "Software\Company\SubKey" to the variable. If the key does not yet exist or
 * an error occurred during read from the registry, a default
 * value of 100 is used when accessing the variable.
 * now the variable can be used like any other DWORD variable:
 * \code
 * regvalue = 200;                      //stores the value 200 in the registry
 * int temp = regvalue + 300;           //temp has value 500 now
 * regvalue += 300;                     //now the registry has the value 500 too
 * \endcode
 * to avoid too much access to the registry the value is cached inside the object.
 * once the value is read, no more read accesses to the registry will be made.
 * this means the variable will contain a wrong value if the corresponding registry
 * entry is changed by anything else than this variable! If you think that could happen
 * then use
 * \code
 * regvalue.read();
 * \endcode
 * to force a refresh of the variable with the registry.
 * a write to the registry is only made if the new value assigned with the variable
 * is different than the last assigned value.
 * to force a write use the method write();
 * another option to force reads and writes to the registry is to specify TRUE as the
 * third parameter in the constructor.
 */
template<class T, class Base>
class CRegTypedBase : public Base
{
private:
	T m_value;                  ///< the cached value of the registry
	T m_defaultvalue;           ///< the default value to use

	/**
	 * time stamp of the last registry lookup, i.e \ref read() call
	 */

	ULONGLONG lastRead;

	/**
	 * \ref read() will be called, if \ref lastRead differs from the
	 * current time stamp by more than this.
	 * ULONGLONG(-1) -> no automatic refresh.
	 */

	ULONGLONG lookupInterval;

	/**
	 * Check time stamps etc.
	 * If the current data is out-dated, reset the \ref m_read flag.
	 */

	void HandleAutoRefresh();

	/**
	 * sub-classes must provide type-specific code to extract data from
	 * and write data to an open registry key.
	 */

	virtual void InternalRead(HKEY hKey, T& value) = 0;
	virtual void InternalWrite(HKEY hKey, const T& value) = 0;

public:
	/**
	 * Make the value type accessible to others.
	 */

	using ValueT = T;

	/**
	 * Constructor.
	 * We use this instead of a default constructor because not all
	 * data types may provide an adequate default constructor.
	 */
	CRegTypedBase(const T& def);

	/**
	 * Constructor.
	 * \param key the path to the key, including the key. example: "Software\\Company\\SubKey\\MyValue"
	 * \param def the default value used when the key does not exist or a read error occurred
	 * \param force set to TRUE if no cache should be used, i.e. always read and write directly from/to registry
	 * \param base a predefined base key like HKEY_LOCAL_MACHINE. see the SDK documentation for more information.
	 * \param sam
	 */
	CRegTypedBase(const Base::StringT& key, const T& def, bool force = FALSE, HKEY base = HKEY_CURRENT_USER, REGSAM sam = 0);

	/**
	 * Constructor.
	 * \param lookupInterval time in msec between registry lookups caused by operator const T&
	 * \param key the path to the key, including the key. example: "Software\\Company\\SubKey\\MyValue"
	 * \param def the default value used when the key does not exist or a read error occurred
	 * \param force set to TRUE if no cache should be used, i.e. always read and write directly from/to registry
	 * \param base a predefined base key like HKEY_LOCAL_MACHINE. see the SDK documentation for more information.
	 * \param sam
	 */
	CRegTypedBase(DWORD lookupInterval, const Base::StringT& key, const T& def, bool force = FALSE, HKEY base = HKEY_CURRENT_USER, REGSAM sam = 0);

	/**
	 * reads the assigned value from the registry. Use this method only if you think the registry
	 * value could have been altered without using the CRegDWORD object.
	 * \return the read value
	 */
	void    read();                     ///< reads the value from the registry
	void    write();                    ///< writes the value to the registry

	bool    exists();                   ///< test whether registry entry exits
	const T& defaultValue() const;      ///< return the default passed to the constructor

	/**
	 * Data access.
	 */

	operator const T&();
	virtual CRegTypedBase<T, Base>& operator=(const T& d);
};

// implement CRegTypedBase<> members

template<class T, class Base>
void CRegTypedBase<T, Base>::HandleAutoRefresh()
{
	if (Base::m_read && lookupInterval != static_cast<DWORD>(-1))
	{
		const ULONGLONG currentTime = GetTickCount64();
		if ((currentTime < lastRead) || (currentTime > lastRead + lookupInterval))
			Base::m_read = false;
	}
}

template<class T, class Base>
CRegTypedBase<T, Base>::CRegTypedBase(const T& def)
	: m_value(def)
	, m_defaultvalue(def)
	, lastRead(0)
	, lookupInterval(static_cast<ULONGLONG>(-1))
{
}

template<class T, class Base>
CRegTypedBase<T, Base>::CRegTypedBase(const typename Base::StringT& key, const T& def, bool force, HKEY base, REGSAM sam)
	: Base(key, force, base, sam)
	, m_value(def)
	, m_defaultvalue(def)
	, lastRead(0)
	, lookupInterval(static_cast<DWORD>(-1))
{
}

template<class T, class Base>
CRegTypedBase<T, Base>::CRegTypedBase(const DWORD lookupInterval, const typename Base::StringT& key, const T& def, bool force, HKEY base, REGSAM sam)
	: Base(key, force, base, sam)
	, m_value(def)
	, m_defaultvalue(def)
	, lastRead(0)
	, lookupInterval(lookupInterval)
{
}

template<class T, class Base>
void CRegTypedBase<T, Base>::read()
{
	m_value = m_defaultvalue;
	Base::m_exists = false;

	HKEY hKey = nullptr;
	if ((Base::m_lastError = RegOpenKeyEx(Base::m_base, Base::GetPlainString(Base::m_path), 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | Base::m_sam, &hKey)) == ERROR_SUCCESS)
	{
		T value = m_defaultvalue;
		InternalRead(hKey, value);

		if (Base::m_lastError == ERROR_SUCCESS)
		{
			Base::m_exists = true;
			m_value = value;
		}

		Base::m_lastError = RegCloseKey(hKey);
	}

	Base::m_read = true;
	lastRead = GetTickCount64();
}

template<class T, class Base>
void CRegTypedBase<T, Base>::write()
{
	HKEY hKey = nullptr;

	DWORD disp = 0;
	if ((Base::m_lastError = RegCreateKeyEx(Base::m_base, Base::GetPlainString(Base::m_path), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE | Base::m_sam, NULL, &hKey, &disp)) != ERROR_SUCCESS)
	{
		return;
	}

	InternalWrite(hKey, m_value);
	if (Base::m_lastError == ERROR_SUCCESS)
	{
		Base::m_read = true;
		Base::m_exists = true;
	}
	Base::m_lastError = RegCloseKey(hKey);

	lastRead = GetTickCount64();
}

template<class T, class Base>
bool CRegTypedBase<T, Base>::exists()
{
	if (!Base::m_read && (Base::m_lastError == ERROR_SUCCESS))
		read();

	return Base::m_exists;
}

template<class T, class Base>
const T& CRegTypedBase<T, Base>::defaultValue() const
{
	return m_defaultvalue;
}

template<class T, class Base>
CRegTypedBase<T, Base>::operator const T&()
{
	HandleAutoRefresh();
	if ((Base::m_read) && (!Base::m_force))
	{
		Base::m_lastError = ERROR_SUCCESS;
	}
	else
	{
		read();
	}

	return m_value;
}

template<class T, class Base>
CRegTypedBase<T, Base>& CRegTypedBase<T, Base>::operator=(const T& d)
{
	if (Base::m_read && (d == m_value) && !Base::m_force)
	{
		//no write to the registry required, its the same value
		Base::m_lastError = ERROR_SUCCESS;
		return *this;
	}
	m_value = d;
	write();
	return *this;
}

/**
 * \ingroup Utils
 * DWORD value in registry. with this class you can use DWORD values in registry
 * like normal DWORD variables in your program.
 * Usage:
 * in your header file, declare your registry DWORD variable:
 * \code
 * CRegDWORD regvalue;
 * \endcode
 * next initialize the variable e.g. in the constructor of your class:
 * \code
 * regvalue = CRegDWORD("Software\\Company\\SubKey\\MyValue", 100);
 * \endcode
 * this will set the registry value "MyValue" under HKEY_CURRENT_USER with path
 * "Software\Company\SubKey" to the variable. If the key does not yet exist or
 * an error occurred during read from the registry, a default
 * value of 100 is used when accessing the variable.
 * now the variable can be used like any other DWORD variable:
 * \code
 * regvalue = 200;                      //stores the value 200 in the registry
 * int temp = regvalue + 300;           //temp has value 500 now
 * regvalue += 300;                     //now the registry has the value 500 too
 * \endcode
 * to avoid too much access to the registry the value is cached inside the object.
 * once the value is read, no more read accesses to the registry will be made.
 * this means the variable will contain a wrong value if the corresponding registry
 * entry is changed by anything else than this variable! If you think that could happen
 * then use
 * \code
 * regvalue.read();
 * \endcode
 * to force a refresh of the variable with the registry.
 * a write to the registry is only made if the new value assigned with the variable
 * is different than the last assigned value.
 * to force a write use the method write();
 * another option to force reads and writes to the registry is to specify TRUE as the
 * third parameter in the constructor.
 */
template<class Base>
class CRegDWORDCommon : public CRegTypedBase<DWORD, Base>
{
private:
	/**
	 * provide type-specific code to extract data from and write data to an open registry key.
	 */

	void InternalRead(HKEY hKey, DWORD& value) override;
	void InternalWrite(HKEY hKey, const DWORD& value) override;

public:
	CRegDWORDCommon();
	/**
	 * Constructor.
	 * \param key the path to the key, including the key. example: "Software\\Company\\SubKey\\MyValue"
	 * \param def the default value used when the key does not exist or a read error occurred
	 * \param force set to TRUE if no cache should be used, i.e. always read and write directly from/to registry
	 * \param base a predefined base key like HKEY_LOCAL_MACHINE. see the SDK documentation for more information.
	 * \param sam
	 */
	CRegDWORDCommon(const Base::StringT& key, DWORD def = 0, bool force = false, HKEY base = HKEY_CURRENT_USER, REGSAM sam = 0);
	CRegDWORDCommon(DWORD lookupInterval, const Base::StringT& key, DWORD def = 0, bool force = false, HKEY base = HKEY_CURRENT_USER, REGSAM sam = 0);

	CRegDWORDCommon& operator=(DWORD rhs)
	{
		CRegTypedBase<DWORD, Base>::operator=(rhs);
		return *this;
	}
	CRegDWORDCommon& operator+=(DWORD d) { return *this = *this + d; }
	CRegDWORDCommon& operator-=(DWORD d) { return *this = *this - d; }
	CRegDWORDCommon& operator*=(DWORD d) { return *this = *this * d; }
	CRegDWORDCommon& operator/=(DWORD d) { return *this = *this / d; }
	CRegDWORDCommon& operator%=(DWORD d) { return *this = *this % d; }
	CRegDWORDCommon& operator<<=(DWORD d) { return *this = *this << d; }
	CRegDWORDCommon& operator>>=(DWORD d) { return *this = *this >> d; }
	CRegDWORDCommon& operator&=(DWORD d) { return *this = *this & d; }
	CRegDWORDCommon& operator|=(DWORD d) { return *this = *this | d; }
	CRegDWORDCommon& operator^=(DWORD d) { return *this = *this ^ d; }
};

// implement CRegDWORDCommon<> methods

template<class Base>
CRegDWORDCommon<Base>::CRegDWORDCommon()
	: CRegTypedBase<DWORD, Base>(0)
{
}

template<class Base>
CRegDWORDCommon<Base>::CRegDWORDCommon(const typename Base::StringT& key, DWORD def, bool force, HKEY base, REGSAM sam)
	: CRegTypedBase<DWORD, Base>(key, def, force, base, sam)
{
}

template<class Base>
CRegDWORDCommon<Base>::CRegDWORDCommon(DWORD lookupInterval, const typename Base::StringT& key, DWORD def, bool force, HKEY base, REGSAM sam)
	: CRegTypedBase<DWORD, Base>(lookupInterval, key, def, force, base, sam)
{
}

template<class Base>
void CRegDWORDCommon<Base>::InternalRead(HKEY hKey, DWORD& value)
{
	DWORD size = sizeof(value);
	DWORD type = 0;
	Base::m_lastError = RegQueryValueEx(hKey, Base::GetPlainString(Base::m_key), nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
	Implies(Base::m_lastError == ERROR_SUCCESS, type == REG_DWORD);
}

template<class Base>
void CRegDWORDCommon<Base>::InternalWrite(HKEY hKey, const DWORD& value)
{
	Base::m_lastError = RegSetValueEx(hKey, Base::GetPlainString(Base::m_key), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

template <class Base>
class CRegQWORDCommon : public CRegTypedBase<QWORD, Base>
{
private:
	/**
	 * provide type-specific code to extract data from and write data to an open registry key.
	 */

	void InternalRead(HKEY hKey, QWORD& value) override;
	void InternalWrite(HKEY hKey, const QWORD& value) override;

public:
	CRegQWORDCommon();
	/**
	 * Constructor.
	 * \param key the path to the key, including the key. example: "Software\\Company\\SubKey\\MyValue"
	 * \param def the default value used when the key does not exist or a read error occurred
	 * \param force set to TRUE if no cache should be used, i.e. always read and write directly from/to registry
	 * \param base a predefined base key like HKEY_LOCAL_MACHINE. see the SDK documentation for more information.
	 * \param sam
	 */
	CRegQWORDCommon(const Base::StringT& key, QWORD def = 0, bool force = false, HKEY base = HKEY_CURRENT_USER, REGSAM sam = 0);
	CRegQWORDCommon(QWORD lookupInterval, const Base::StringT& key, QWORD def = 0, bool force = false, HKEY base = HKEY_CURRENT_USER, REGSAM sam = 0);

	CRegQWORDCommon& operator=(QWORD rhs)
	{
		CRegTypedBase<QWORD, Base>::operator=(rhs);
		return *this;
	}
	CRegQWORDCommon& operator+=(QWORD d) { return *this = *this + d; }
	CRegQWORDCommon& operator-=(QWORD d) { return *this = *this - d; }
	CRegQWORDCommon& operator*=(QWORD d) { return *this = *this * d; }
	CRegQWORDCommon& operator/=(QWORD d) { return *this = *this / d; }
	CRegQWORDCommon& operator%=(QWORD d) { return *this = *this % d; }
	CRegQWORDCommon& operator<<=(QWORD d) { return *this = *this << d; }
	CRegQWORDCommon& operator>>=(QWORD d) { return *this = *this >> d; }
	CRegQWORDCommon& operator&=(QWORD d) { return *this = *this & d; }
	CRegQWORDCommon& operator|=(QWORD d) { return *this = *this | d; }
	CRegQWORDCommon& operator^=(QWORD d) { return *this = *this ^ d; }
};

// implement CRegQWORDCommon<> methods

template <class Base>
CRegQWORDCommon<Base>::CRegQWORDCommon()
	: CRegTypedBase<QWORD, Base>(0)
{
}

template <class Base>
CRegQWORDCommon<Base>::CRegQWORDCommon(const typename Base::StringT& key, QWORD def, bool force, HKEY base, REGSAM sam)
	: CRegTypedBase<QWORD, Base>(key, def, force, base, sam)
{
}

template <class Base>
CRegQWORDCommon<Base>::CRegQWORDCommon(QWORD lookupInterval, const typename Base::StringT& key, QWORD def, bool force, HKEY base, REGSAM sam)
	: CRegTypedBase<QWORD, Base>(lookupInterval, key, def, force, base, sam)
{
}

template <class Base>
void CRegQWORDCommon<Base>::InternalRead(HKEY hKey, QWORD& value)
{
	DWORD size = sizeof(value);
	DWORD type = 0;
	Base::m_lastError = RegQueryValueEx(hKey, Base::GetPlainString(Base::m_key), nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
	Implies(Base::m_lastError == ERROR_SUCCESS, type == REG_QWORD);
}

template <class Base>
void CRegQWORDCommon<Base>::InternalWrite(HKEY hKey, const QWORD& value)
{
	Base::m_lastError = RegSetValueEx(hKey, Base::GetPlainString(Base::m_key), 0, REG_QWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

/**
 * \ingroup Utils
 * CString value in registry. with this class you can use CString values in registry
 * almost like normal CString variables in your program.
 * Usage:
 * in your header file, declare your registry CString variable:
 * \code
 * CRegString regvalue;
 * \endcode
 * next initialize the variable e.g. in the constructor of your class:
 * \code
 * regvalue = CRegString("Software\\Company\\SubKey\\MyValue", "default");
 * \endcode
 * this will set the registry value "MyValue" under HKEY_CURRENT_USER with path
 * "Software\Company\SubKey" to the variable. If the key does not yet exist or
 * an error occurred during read from the registry, a default
 * value of "default" is used when accessing the variable.
 * now the variable can be used like any other CString variable:
 * \code
 * regvalue = "some string";            //stores the value "some string" in the registry
 * CString temp = regvalue + "!!";      //temp has value "some string!!" now
 * \endcode
 * to use the normal methods of the CString class, just typecast the CRegString to a CString
 * and do whatever you want with the string:
 * \code
 * static_cast<CString>(regvalue).GetLength();
 * static_cast<CString>(regvalue).Trim();
 * \endcode
 * please be aware that in the second line the change in the string won't be written
 * to the registry! To force a write use the write() method. A write() is only needed
 * if you change the String with Methods not overloaded by CRegString.
 * to avoid too much access to the registry the value is cached inside the object.
 * once the value is read, no more read accesses to the registry will be made.
 * this means the variable will contain a wrong value if the corresponding registry
 * entry is changed by anything else than this variable! If you think that could happen
 * then use
 * \code
 * regvalue.read();
 * \endcode
 * to force a refresh of the variable with the registry.
 * a write to the registry is only made if the new value assigned with the variable
 * is different than the last assigned value.
 * to force a write use the method write();
 * another option to force reads and writes to the registry is to specify TRUE as the
 * third parameter in the constructor.
 */
template<class Base>
class CRegStringCommon : public CRegTypedBase<typename Base::StringT, Base>
{
	/**
	 * provide type-specific code to extract data from and write data to an open registry key.
	 */

	void InternalRead(HKEY hKey, Base::StringT& value) override;
	void InternalWrite(HKEY hKey, const Base::StringT& value) override;

public:
	CRegStringCommon();
	/**
	 * Constructor.
	 * \param key the path to the key, including the key. example: "Software\\Company\\SubKey\\MyValue"
	 * \param def the default value used when the key does not exist or a read error occurred
	 * \param force set to TRUE if no cache should be used, i.e. always read and write directly from/to registry
	 * \param base a predefined base key like HKEY_LOCAL_MACHINE. see the SDK documentation for more information.
	 * \param sam
	 */
	CRegStringCommon(const Base::StringT& key, const Base::StringT& def = L"", bool force = false, HKEY base = HKEY_CURRENT_USER, REGSAM sam = 0);
	CRegStringCommon(DWORD lookupInterval, const Base::StringT& key, const Base::StringT& def = L"", bool force = false, HKEY base = HKEY_CURRENT_USER, REGSAM sam = 0);

	CRegStringCommon& operator=(const Base::StringT& rhs) override
	{
		CRegTypedBase<typename Base::StringT, Base>::operator=(rhs);
		return *this;
	}
	CRegStringCommon& operator+=(const Base::StringT& s) { return *this = reinterpret_cast<Base::StringT>(*this) + s; }
};

// implement CRegDWORD<> methods

template<class Base>
CRegStringCommon<Base>::CRegStringCommon()
	: CRegTypedBase<typename Base::StringT, Base>(typename Base::StringT())
{
}

template<class Base>
CRegStringCommon<Base>::CRegStringCommon(const typename Base::StringT& key, const typename Base::StringT& def, bool force, HKEY base, REGSAM sam)
	: CRegTypedBase<typename Base::StringT, Base>(key, def, force, base, sam)
{
}

template<class Base>
CRegStringCommon<Base>::CRegStringCommon(DWORD lookupInterval, const typename Base::StringT& key, const typename Base::StringT& def, bool force, HKEY base, REGSAM sam)
	: CRegTypedBase<typename Base::StringT, Base>(lookupInterval, key, def, force, base, sam)
{
}

template<class Base>
void CRegStringCommon<Base>::InternalRead(HKEY hKey, typename Base::StringT& value)
{
	DWORD size = 0;
	DWORD type = 0;
	Base::m_lastError = RegQueryValueEx(hKey, Base::GetPlainString(Base::m_key), nullptr, &type, nullptr, &size);

	if (Base::m_lastError == ERROR_SUCCESS)
	{
		const auto pStr = std::make_unique<wchar_t[]>(size);
		Base::m_lastError = RegQueryValueEx(hKey, Base::GetPlainString(Base::m_key), nullptr, &type, reinterpret_cast<BYTE*>(pStr.get()), &size);
		if (Implies(Base::m_lastError == ERROR_SUCCESS, type == REG_SZ || type == REG_EXPAND_SZ))
			value = Base::StringT(pStr.get());
	}
}

template<class Base>
void CRegStringCommon<Base>::InternalWrite(HKEY hKey, const typename Base::StringT& value)
{
	Base::m_lastError = RegSetValueEx(hKey, Base::GetPlainString(Base::m_key), 0, REG_SZ, reinterpret_cast<const BYTE*>(static_cast<LPCWSTR>(Base::GetPlainString(value))), (Base::GetLength(value) + 1) * sizeof (wchar_t));
}


class CRegistryKey
{
public: //methods
	/**
	 * Constructor.
	 * \param key the path to the key, including the key. example: "Software\\Company\\SubKey"
	 * \param base a predefined base key like HKEY_LOCAL_MACHINE. see the SDK documentation for more information.
	 * \param sam
	 */
	CRegistryKey(const reg_string_t& key, HKEY base = HKEY_CURRENT_USER, REGSAM sam = 0);
	~CRegistryKey();

	/**
	 * Creates the registry key if it does not already exist.
	 * \return ERROR_SUCCESS or an nonzero error code. Use FormatMessage() to get an error description.
	 */
	DWORD createKey();
	/**
	 * Removes the whole registry key including all values. So if you set the registry
	 * entry to be HKCU\Software\Company\Product\key there will only be
	 * HKCU\Software\Company\Product key in the registry.
	 * \return ERROR_SUCCESS or an nonzero error code. Use FormatMessage() to get an error description.
	 */
	DWORD removeKey();

	bool getValues(std::vector<reg_string_t>& values);        ///< returns the list of values
	bool getSubKeys(std::vector<reg_string_t>& subkeys);      ///< returns the list of sub keys

public: //members
	HKEY m_base = nullptr; ///< handle to the registry base
	HKEY m_hKey = nullptr; ///< handle to the open registry key
	REGSAM m_sam = 0;      ///< the security attributes to pass to the registry command
	reg_string_t m_path;     ///< the path to the key
};

template<class T>
class CKeyList
{
	/// constructor parameters

	T::StringT key;
	T::ValueT defaultValue;
	HKEY base;

	/// per-index defaults

	using TDefaults = std::map<int, typename T::ValueT>;
	TDefaults defaults{};

	/// the indices accessed so far

	using TElements = std::map<int, T*>;
	mutable TElements elements{};

	/// auto-insert

	const T::ValueT& GetDefault(int index) const;
	T& GetAt(int index) const;

public:
	/// construction

	CKeyList(const T::StringT& key, const T::ValueT& defaultValue, const HKEY base = HKEY_CURRENT_USER)
		: key(key)
		, defaultValue(defaultValue)
		, base(base)
	{}

	/// destruction: delete all elements

	~CKeyList()
	{
		for (auto iter = elements.begin(), end = elements.end(); iter != end; ++iter)
		{
			delete iter->second;
		}
	}

	/// data access

	const T& operator[](const int index) const
	{
		return GetAt(index);
	}

	T& operator[](const int index)
	{
		return GetAt(index);
	}

	[[nodiscard]] const TDefaults& GetDefaults() const
	{
		return defaults;
	}

	TDefaults& GetDefaults()
	{
		return defaults;
	}

	const T::ValueT& GetDefault() const
	{
		return defaultValue;
	}
};

/// auto-insert

template<class T>
const T::ValueT& CKeyList<T>::GetDefault(int index) const
{
	auto iter = defaults.find(index);
	return iter == defaults.end() ? defaultValue : iter->second;
}

template<class T>
T& CKeyList<T>::GetAt(int index) const
{
	auto iter = elements.find(index);
	if (iter == elements.end())
	{
		wchar_t buffer[10];
		_itow_s(index, buffer, 10);
		typename T::StringT indexKey = key + L'\\' + buffer;

		T* newElement = new T(indexKey, GetDefault(index), false, base);
		iter = elements.emplace(index, newElement).first;
	}

	return *iter->second;
};


/**
 * Instantiate templates for common (data type, string type) combinations.
 */

using CRegDWORD = CRegDWORDCommon<CRegBase>;
// using CRegQWORD = CRegQWORDCommon<CRegBase>;
// using CRegString = CRegStringCommon<CRegBase>;

using CRegStdDWORD = CRegDWORDCommon<CRegBase>;
using CRegStdQWORD = CRegQWORDCommon<CRegBase>;
using CRegStdString = CRegStringCommon<CRegBase>;

// using CRegDWORDList = CKeyList<CRegDWORD>;
// using CRegQWORDList = CKeyList<CRegQWORD>;
// using CRegStringList = CKeyList<CRegString>;

using CRegStdDWORDList = CKeyList<CRegStdDWORD>;
using CRegStdQWORDList = CKeyList<CRegStdQWORD>;
using CRegStdStringList = CKeyList<CRegStdString>;

}

export using namespace Registry;
