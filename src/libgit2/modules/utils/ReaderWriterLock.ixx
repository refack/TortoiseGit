/*********************************************************************
CReaderWriterLock: A simple and fast reader-writer lock class in C++
has characters of .NET ReaderWriterLock class
Copyright (C) 2006 Quynh Nguyen Huu

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

Email questions, comments or suggestions to quynhnguyenhuu@gmail.com
*********************************************************************/

/*********************************************************************
Introduction:
This implementation is inspired by System.Threading.ReaderWriterLock in
.NET framework. Following are some important statements I excerpted
(with some words modified) from .NET document.

http://msdn.microsoft.com/library/default.asp?url=/library/en-us/cpref/html/frlrfSystemThreadingReaderWriterLockClassTopic.asp

"ReaderWriterLock is used to synchronize access to a resource.
At any given time, it allows either concurrent read access for
multiple threads, or write access for a single thread.
In a situation where a resource is changed infrequently, a
ReaderWriterLock provides better throughput than a simple
one-at-a-time lock, such as CriticalSection or Mutex.

This library works best where most accesses are reads, while
writes are infrequent and of short duration.

While a writer is waiting for active reader locks to be released,
threads requesting new reader locks will have to wait in the reader
queue. Their requests are not granted, even though they could share
concurrent access with existing reader-lock holders; this helps
protect writers against indefinite blockage by readers..."
*********************************************************************/
module;
#include <map>
#include <windows.h>
#include <crtdbg.h>

#if (_WIN32_WINNT >= 0x0403)
//////////////////////////////////////////////////////////////////
// On multiprocessor systems, this value define number of times
// that a thread tries to spin before actually performing a wait
// operation (see InitializeCriticalSectionAndSpinCount API)
#ifndef READER_WRITER_SPIN_COUNT
#define READER_WRITER_SPIN_COUNT 400
#endif READER_WRITER_SPIN_COUNT
#endif _WIN32_WINNT

/////////////////////////////////////////////////////////////////
// Following macros make this file can be used in non-MFC project
#ifndef ASSERT
// Define ASSERT macro
#   define ASSERT _ASSERT

// Define VERIFY macro
#   ifdef _DEBUG
#       define VERIFY ASSERT
#       define DEBUG_ONLY(f) ((void)(f))
#   else
#       define VERIFY(f) ((void)(f))
#       define DEBUG_ONLY(f)
#   endif
#endif



export module ReaderWriterLock;

// Forward reference
export class CReaderWriterLock;

//////////////////////////////////////////////////////////////////
// CReaderWriterLockNonReentrance class
// NOTE: This class doesn't support reentrance & lock escalation.
// May be deadlock in one of following situations:
//  1) Call AcquireReaderLock twice (reentrance)
//     --> Revise execution flow.
//  2) Call AcquireWriterLock twice (reentrance)
//     --> Revise execution flow.
//  3) Call AcquireReaderLock then AcquireWriterLock (lock escalation)
//     --> Use ReleaseReaderAndAcquireWriterLock method
//  4) Call AcquireWriterLock then AcquireReaderLock (lock deescalation)
//     --> Use DowngradeFromWriterLock method
export class CReaderWriterLockNonReentrance
{
public:
	CReaderWriterLockNonReentrance() noexcept;
	~CReaderWriterLockNonReentrance() noexcept;
	bool AcquireReaderLock(DWORD dwTimeout = INFINITE) noexcept;
	void ReleaseReaderLock() noexcept;
	bool AcquireWriterLock(DWORD dwTimeout = INFINITE) noexcept;
	void ReleaseWriterLock() noexcept;
	void DowngradeFromWriterLock() noexcept;

	// When a thread calls UpgradeToWriterLock, the reader lock is released,
	// and the thread goes to the end of the writer queue. Thus, other threads
	// might write to resources before this method returns
	bool UpgradeToWriterLock(DWORD dwTimeout = INFINITE) noexcept;
protected:
	// A critical section to guard all the other members
	mutable CRITICAL_SECTION m_cs;
	// Auto-reset event, will be dynamically created/destroyed on demand
	volatile HANDLE m_hSafeToWriteEvent = nullptr;
	// Manual-reset event, will be dynamically created/destroyed on demand
	volatile HANDLE m_hSafeToReadEvent = nullptr;
	// Total number of writers on this object
	volatile INT m_iNumOfWriter = 0;
	// Total number of readers have already owned this object
	volatile INT m_iNumOfReaderEntered = 0;
	// Total number of readers are waiting to be owners of this object
	volatile INT m_iNumOfReaderWaiting = 0;
	// Internal/Real implementation
	void EnterCS() const noexcept;
	void LeaveCS() const noexcept;
	bool _ReaderWait(ULONGLONG dwTimeout) noexcept;
	bool _WriterWaitAndLeaveCSIfSuccess(ULONGLONG dwTimeout) noexcept;
	bool _UpgradeToWriterLockAndLeaveCS(ULONGLONG dwTimeout) noexcept;
	void _ReaderRelease() noexcept;
	void _WriterRelease(bool blDowngrade) noexcept;

	friend CReaderWriterLock;
};

//////////////////////////////////////////////////////////////////
// CReaderWriterLock class
// This class supports reentrance & lock escalation
export class CReaderWriterLock
{
public:
	CReaderWriterLock();
	~CReaderWriterLock();

	bool AcquireReaderLock(DWORD dwTimeout = INFINITE) noexcept;
	void ReleaseReaderLock() noexcept;

	// If current thread was already a reader
	// it will be upgraded to be writer automatically.
	// BE CAREFUL! Other threads might write to the resource
	// before current thread is successfully upgraded.
	bool AcquireWriterLock(DWORD dwTimeout = INFINITE) noexcept;
	void ReleaseWriterLock() noexcept;

	// Regardless of how many times current thread acquired reader
	// or writer locks, a call to this method will release all locks.
	// After that, any call to ReleaseWriterLock or ReleaseReaderLock
	// will raise exception in DEBUG mode.
	void ReleaseAllLocks() noexcept;

	// Query thread's status
	DWORD GetCurrentThreadStatus() const noexcept;
	void GetCurrentThreadStatus(DWORD* lpdwReaderLockCounter,
		DWORD* lpdwWriterLockCounter) const noexcept;
protected:
	CReaderWriterLockNonReentrance m_impl;

	using CMapThreadToState = std::map<DWORD,DWORD>;
	CMapThreadToState m_map;
};

//////////////////////////////////////////////////////////////////
// CAutoReadLockT & CAutoWriteLockT classes
// Couple of template helper classes which would let one acquire a lock
// in a body of code and not have to worry about explicitly releasing
// that lock if an exception is encountered in that piece of code or
// if there are multiple return points out of that piece.

template<typename T>
class CAutoReadLockT
{
public:
	CAutoReadLockT(T& objLock) noexcept : m_lock(objLock)
	{
		m_lock.AcquireReaderLock();
	}
	~CAutoReadLockT() noexcept
	{
		m_lock.ReleaseReaderLock();
	}
	CAutoReadLockT(CAutoReadLockT&) = delete;
	CAutoReadLockT(CAutoReadLockT&& that) = default;
	CAutoReadLockT& operator=(const CAutoReadLockT&) = delete;
protected:
	T& m_lock;
};

template<typename T>
class CAutoWriteLockT
{
public :
	CAutoWriteLockT(T& objLock) noexcept : m_lock(objLock)
	{
		m_lock.AcquireWriterLock();
	}
	~CAutoWriteLockT() noexcept
	{
		m_lock.ReleaseWriterLock();
	}
	CAutoWriteLockT(CAutoWriteLockT&) = delete;
	CAutoWriteLockT(CAutoWriteLockT&&) = default;
	CAutoWriteLockT& operator=(const CAutoWriteLockT&) = delete;
protected:
	T& m_lock;
};

template<typename T>
class CAutoReadWeakLockT
{
public:
	CAutoReadWeakLockT(T& objLock, DWORD timeout = 1) noexcept : m_lock(objLock)
	{
		isAcquired = m_lock.AcquireReaderLock(timeout);
	}
	~CAutoReadWeakLockT() noexcept
	{
		if (isAcquired)
			m_lock.ReleaseReaderLock();
	}
	[[nodiscard]] bool IsAcquired() const
	{
		return isAcquired;
	}
	CAutoReadWeakLockT(CAutoReadWeakLockT&) = delete;
	CAutoReadWeakLockT(CAutoReadWeakLockT&&) = default;
	CAutoReadWeakLockT& operator=(const CAutoReadWeakLockT&) = delete;
protected:
	T& m_lock;
	bool isAcquired;
};

template<typename T>
class CAutoWriteWeakLockT
{
public :
	CAutoWriteWeakLockT(T& objLock, DWORD timeout = 1) noexcept : m_lock(objLock)
	{
		isAcquired = m_lock.AcquireWriterLock(timeout);
	}
	~CAutoWriteWeakLockT() noexcept
	{
		release();
	}
	void Release()
	{
		release();
	}
	[[nodiscard]] bool IsAcquired() const
	{
		return isAcquired;
	}
	CAutoWriteWeakLockT(CAutoWriteWeakLockT&) = delete;
	CAutoWriteWeakLockT(CAutoWriteWeakLockT&&) = default;
	CAutoWriteWeakLockT& operator=(const CAutoWriteWeakLockT&) = delete;
protected:
	T& m_lock;
	bool isAcquired;

	void release()
	{
		if (isAcquired)
		{
			m_lock.ReleaseWriterLock();
			isAcquired = false;
		}
	}
};

//////////////////////////////////////////////////////////////////
// Instances of above template helper classes

export using CAutoReadLock = CAutoReadLockT<CReaderWriterLock>;
export using CAutoWriteLock = CAutoWriteLockT<CReaderWriterLock>;
export using CAutoReadWeakLock = CAutoReadWeakLockT<CReaderWriterLock>;
export using CAutoWriteWeakLock = CAutoWriteWeakLockT<CReaderWriterLock>;

//////////////////////////////////////////////////////////////////
// Inline methods

__forceinline
	void CReaderWriterLockNonReentrance::EnterCS() const noexcept {
		::EnterCriticalSection(&m_cs);
}

__forceinline
	void CReaderWriterLockNonReentrance::LeaveCS() const noexcept {
		::LeaveCriticalSection(&m_cs);
}



//////////////////////// CPP file //////////////////////////////////




///////////////////////////////////////////////////////
// CReaderWriterLockNonReentrance implementation

CReaderWriterLockNonReentrance::CReaderWriterLockNonReentrance() noexcept
{
	SecureZeroMemory(this, sizeof(*this));
#if (_WIN32_WINNT >= 0x0403)
	InitializeCriticalSectionAndSpinCount(&m_cs, READER_WRITER_SPIN_COUNT);
#else
	InitializeCriticalSection(&m_cs);
#endif
}

CReaderWriterLockNonReentrance::~CReaderWriterLockNonReentrance()
{
	_ASSERT(!m_hSafeToReadEvent && !m_hSafeToWriteEvent);
	DeleteCriticalSection(&m_cs);
}

bool CReaderWriterLockNonReentrance::_ReaderWait(ULONGLONG dwTimeout) noexcept
{
	bool blCanRead;

	++m_iNumOfReaderWaiting;
	if (!m_hSafeToReadEvent)
		m_hSafeToReadEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

	if(INFINITE == dwTimeout) // INFINITE is a special value
	{
		while (0 != m_iNumOfWriter)
		{
			LeaveCS();
			WaitForSingleObject(m_hSafeToReadEvent, INFINITE);
			// There might be one or more Writers entered, that's
			// why we need loop here
			EnterCS();
		}

		++m_iNumOfReaderEntered;
		blCanRead = TRUE;
	}
	else
	{
		LeaveCS();

		ULONGLONG const dwBeginTime = GetTickCount64();
		ULONGLONG dwConsumedTime = 0;

		while(TRUE)
		{
			blCanRead = (WAIT_OBJECT_0 == WaitForSingleObject(m_hSafeToReadEvent, static_cast<DWORD>(dwTimeout - dwConsumedTime)));

			EnterCS();

			if(0 == m_iNumOfWriter)
			{
				// Regardless timeout or not, there is no Writer
				// So it's safe to be Reader right now
				++m_iNumOfReaderEntered;
				blCanRead = TRUE;
				break;
			}

			if (!blCanRead)
			{
				// Timeout after waiting
				break;
			}

			// There are some Writers have just entered
			// So leave CS and prepare to try again
			LeaveCS();

			dwConsumedTime = GetTickCount64() - dwBeginTime;
			if(dwConsumedTime > dwTimeout)
			{
				// Don't worry why the code here looks stupid
				// Because this case rarely happens, it's better
				//  to optimize code for the usual case
				blCanRead = FALSE;
				EnterCS();
				break;
			}
		}
	}

	if(0==--m_iNumOfReaderWaiting)
	{
		CloseHandle(m_hSafeToReadEvent);
		m_hSafeToReadEvent = nullptr;
	}

	return blCanRead;
}

void CReaderWriterLockNonReentrance::_ReaderRelease() noexcept
{
	INT _iNumOfReaderEntered = --m_iNumOfReaderEntered;
	_ASSERT(0 <= _iNumOfReaderEntered);

	if (0 == _iNumOfReaderEntered && m_hSafeToWriteEvent)
	{
		SetEvent(m_hSafeToWriteEvent);
	}
}

bool CReaderWriterLockNonReentrance::_WriterWaitAndLeaveCSIfSuccess(ULONGLONG dwTimeout) noexcept
{
	//EnterCS();
	_ASSERT(0 != dwTimeout);

	// Increase Writer-counter & reset Reader-event if necessary
	INT _iNumOfWriter = ++m_iNumOfWriter;
	if (_iNumOfWriter == 1 && m_hSafeToReadEvent)
	{
		ResetEvent(m_hSafeToReadEvent);
	}

	if (!m_hSafeToWriteEvent)
	{
		m_hSafeToWriteEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	}
	LeaveCS();

	bool blCanWrite = (WAIT_OBJECT_0 == WaitForSingleObject(m_hSafeToWriteEvent, static_cast<DWORD>(dwTimeout)));
	if (!blCanWrite)
	{
		// Undo what we changed after timeout
		EnterCS();
		if(0 == --m_iNumOfWriter)
		{
			CloseHandle(m_hSafeToWriteEvent);
			m_hSafeToWriteEvent = nullptr;

			if(0 == m_iNumOfReaderEntered)
			{
				// Although it was timeout, it's still safe to be writer now
				++m_iNumOfWriter;
				LeaveCS();
				blCanWrite = TRUE;
			}
			else if(m_hSafeToReadEvent)
			{
				SetEvent(m_hSafeToReadEvent);
			}
		}
	}
	return blCanWrite;
}

bool CReaderWriterLockNonReentrance::_UpgradeToWriterLockAndLeaveCS(ULONGLONG dwTimeout) noexcept
{
	_ASSERT(m_iNumOfReaderEntered > 0);

	if(0 == dwTimeout)
	{
		LeaveCS();
		return FALSE;
	}

	--m_iNumOfReaderEntered;
	bool blCanWrite = _WriterWaitAndLeaveCSIfSuccess(dwTimeout);
	if (!blCanWrite)
	{
		// Now analyze why it was failed to have suitable action
		if(0 == m_iNumOfWriter)
		{
			_ASSERT(0 < m_iNumOfReaderEntered);
			// There are some readers still owning the lock
			// It's safe to be a reader again after failure
			++m_iNumOfReaderEntered;
		}
		else
		{
			// Reach to here, it's NOT safe to be a reader immediately
			_ReaderWait(INFINITE);
			if(1 == m_iNumOfReaderEntered)
			{
				// After wait, now it's safe to be writer
				_ASSERT(0 == m_iNumOfWriter);
				m_iNumOfReaderEntered = 0;
				m_iNumOfWriter = 1;
				blCanWrite = TRUE;
			}
		}
		LeaveCS();
	}

	return blCanWrite;
}

void CReaderWriterLockNonReentrance::_WriterRelease(bool blDowngrade) noexcept
{
	_ASSERT(0 == m_iNumOfReaderEntered);

	if(blDowngrade)
	{
		++m_iNumOfReaderEntered;
	}

	if(0 == --m_iNumOfWriter)
	{
		if (m_hSafeToWriteEvent)
		{
			CloseHandle(m_hSafeToWriteEvent);
			m_hSafeToWriteEvent = nullptr;
		}

		if(m_hSafeToReadEvent)
		{
			SetEvent(m_hSafeToReadEvent);
		}
	}
	else
	{
		//////////////////////////////////////////////////////////////////////////
		// Some WRITERs are queued
		_ASSERT(m_iNumOfWriter > 0 && m_hSafeToWriteEvent);

		if (!blDowngrade)
		{
			SetEvent(m_hSafeToWriteEvent);
		}
	}
}

bool CReaderWriterLockNonReentrance::AcquireReaderLock(DWORD dwTimeout) noexcept
{
	bool blCanRead;

	EnterCS();
	if(0 == m_iNumOfWriter)
	{
		// Enter successful without wait
		++m_iNumOfReaderEntered;
		blCanRead = TRUE;
	}
	else
	{
		blCanRead = (dwTimeout)? _ReaderWait(dwTimeout) : FALSE;
	}
	LeaveCS();

	return blCanRead;
}

void CReaderWriterLockNonReentrance::ReleaseReaderLock() noexcept
{
	EnterCS();
	_ReaderRelease();
	LeaveCS();
}

bool CReaderWriterLockNonReentrance::AcquireWriterLock(DWORD dwTimeout) noexcept
{
	bool blCanWrite ;

	EnterCS();
	if(0 == (m_iNumOfWriter | m_iNumOfReaderEntered))
	{
		++m_iNumOfWriter;
		blCanWrite = TRUE;
	}
	else if(0 == dwTimeout)
	{
		blCanWrite = FALSE;
	}
	else
	{
		blCanWrite = _WriterWaitAndLeaveCSIfSuccess(dwTimeout);
		if(blCanWrite)
		{
			return TRUE;
		}
	}

	LeaveCS();
	return blCanWrite;
}

void CReaderWriterLockNonReentrance::ReleaseWriterLock() noexcept
{
	EnterCS();
	_WriterRelease(FALSE);
	LeaveCS();
}

void CReaderWriterLockNonReentrance::DowngradeFromWriterLock() noexcept
{
	EnterCS();
	_WriterRelease(TRUE);
	LeaveCS();
}

bool CReaderWriterLockNonReentrance::UpgradeToWriterLock(DWORD dwTimeout) noexcept
{
	EnterCS();
	return _UpgradeToWriterLockAndLeaveCS(dwTimeout);
}

// END CReaderWriterLockNonReentrance implementation
///////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// CReaderWriterLock implementation

#define READER_RECURRENCE_UNIT 0x00000001
#define READER_RECURRENCE_MASK 0x0000FFFF
#define WRITER_RECURRENCE_UNIT 0x00010000

CReaderWriterLock::CReaderWriterLock()
{
}

CReaderWriterLock::~CReaderWriterLock()
{
}

bool CReaderWriterLock::AcquireReaderLock(DWORD dwTimeout) noexcept
{
	const DWORD dwCurrentThreadId = GetCurrentThreadId();

	m_impl.EnterCS();
	CMapThreadToState::iterator ite = m_map.find(dwCurrentThreadId);

	if(ite != m_map.end())
	{
		//////////////////////////////////////////////////////////////////////////
		// Current thread was already a WRITER or READER
		_ASSERT(0 < ite->second);
		ite->second += READER_RECURRENCE_UNIT;
		m_impl.LeaveCS();

		return TRUE;
	}

	if(0 == m_impl.m_iNumOfWriter)
	{
		// There is NO WRITER on this RW object
		// Current thread is going to be a READER
		++m_impl.m_iNumOfReaderEntered;
		m_map.emplace(dwCurrentThreadId, READER_RECURRENCE_UNIT);

		m_impl.LeaveCS();
		return TRUE;
	}

	if(0 == dwTimeout)
	{
		m_impl.LeaveCS();
		return FALSE;
	}

	bool blCanRead = m_impl._ReaderWait(dwTimeout);
	if(blCanRead)
	{
		m_map.emplace(dwCurrentThreadId, READER_RECURRENCE_UNIT);
	}
	m_impl.LeaveCS();

	return blCanRead;
}

void CReaderWriterLock::ReleaseReaderLock() noexcept
{
	const DWORD dwCurrentThreadId = GetCurrentThreadId();
	m_impl.EnterCS();

	CMapThreadToState::iterator ite = m_map.find(dwCurrentThreadId);
	_ASSERT( (ite != m_map.end()) && (READER_RECURRENCE_MASK & ite->second));

	const DWORD dwThreadState = (ite->second -= READER_RECURRENCE_UNIT);
	if(0 == dwThreadState)
	{
		m_map.erase(ite);
		m_impl._ReaderRelease();
	}
	m_impl.LeaveCS();
}

bool CReaderWriterLock::AcquireWriterLock(DWORD dwTimeout) noexcept
{
	const DWORD dwCurrentThreadId = GetCurrentThreadId();
	bool blCanWrite;

	m_impl.EnterCS();
	CMapThreadToState::iterator ite = m_map.find(dwCurrentThreadId);

	if(ite != m_map.end())
	{
		_ASSERT(0 < ite->second);

		if(ite->second >= WRITER_RECURRENCE_UNIT)
		{
			// Current thread was already a WRITER
			ite->second += WRITER_RECURRENCE_UNIT;
			m_impl.LeaveCS();
			return TRUE;
		}

		// Current thread was already a READER
		_ASSERT(1 <= m_impl.m_iNumOfReaderEntered);
		if(1 == m_impl.m_iNumOfReaderEntered)
		{
			// This object is owned by ONLY current thread for READ
			// There might be some threads queued to be WRITERs
			// but for effectiveness (higher throughput), we allow current
			// thread upgrading to be WRITER right now
			m_impl.m_iNumOfReaderEntered = 0;
			++m_impl.m_iNumOfWriter;
			ite->second += WRITER_RECURRENCE_UNIT;
			m_impl.LeaveCS();
			return TRUE;
		}

		// Try upgrading from reader to writer
		blCanWrite = m_impl._UpgradeToWriterLockAndLeaveCS(dwTimeout);
		if(blCanWrite)
		{
			m_impl.EnterCS();
			ite = m_map.find(dwCurrentThreadId);
			ite->second += WRITER_RECURRENCE_UNIT;
			m_impl.LeaveCS();
		}
	}
	else
	{
		if(0 == (m_impl.m_iNumOfWriter | m_impl.m_iNumOfReaderEntered))
		{
			// This RW object is not owned by any thread
			// --> it's safe to make this thread to be WRITER
			++m_impl.m_iNumOfWriter;
			m_map.emplace(dwCurrentThreadId, WRITER_RECURRENCE_UNIT);
			m_impl.LeaveCS();
			return TRUE;
		}

		if(0 == dwTimeout)
		{
			m_impl.LeaveCS();
			return FALSE;
		}

		blCanWrite = m_impl._WriterWaitAndLeaveCSIfSuccess(dwTimeout);
		if(blCanWrite)
		{
			m_impl.EnterCS();
			m_map.emplace(dwCurrentThreadId, WRITER_RECURRENCE_UNIT);
		}
		m_impl.LeaveCS();
	}

	return blCanWrite;
}

void CReaderWriterLock::ReleaseWriterLock() noexcept
{
	const DWORD dwCurrentThreadId = GetCurrentThreadId();
	m_impl.EnterCS();

	CMapThreadToState::iterator ite = m_map.find(dwCurrentThreadId);
	_ASSERT( (ite != m_map.end()) && (WRITER_RECURRENCE_UNIT <= ite->second));

	const DWORD dwThreadState = (ite->second -= WRITER_RECURRENCE_UNIT);
	if(0 == dwThreadState)
	{
		m_map.erase(ite);
		m_impl._WriterRelease(FALSE);
	}
	else if (WRITER_RECURRENCE_UNIT > dwThreadState)
	{
		// Down-grading from writer to reader
		m_impl._WriterRelease(TRUE);
	}
	m_impl.LeaveCS();
}

void CReaderWriterLock::ReleaseAllLocks() noexcept
{
	const DWORD dwCurrentThreadId = GetCurrentThreadId();

	m_impl.EnterCS();
	CMapThreadToState::iterator ite = m_map.find(dwCurrentThreadId);
	if(ite != m_map.end())
	{
		const DWORD dwThreadState = ite->second;
		m_map.erase(ite);
		if(WRITER_RECURRENCE_UNIT <= dwThreadState)
		{
			m_impl._WriterRelease(FALSE);
		}
		else
		{
			_ASSERT(0 < dwThreadState);
			m_impl._ReaderRelease();
		}
	}
	m_impl.LeaveCS();
}

DWORD CReaderWriterLock::GetCurrentThreadStatus() const noexcept
{
	DWORD dwThreadState;
	const DWORD dwCurrentThreadId = GetCurrentThreadId();

	m_impl.EnterCS();
	CMapThreadToState::const_iterator ite = m_map.find(dwCurrentThreadId);
	if(ite != m_map.end())
	{
		dwThreadState = ite->second;
		m_impl.LeaveCS();
		_ASSERT(dwThreadState > 0);
	}
	else
	{
		dwThreadState = 0;
		m_impl.LeaveCS();
	}

	return dwThreadState;
}

void CReaderWriterLock::GetCurrentThreadStatus(DWORD* lpdwReaderLockCounter,
	DWORD* lpdwWriterLockCounter) const noexcept
{
	const DWORD dwThreadState = GetCurrentThreadStatus();

	if (lpdwReaderLockCounter)
		*lpdwReaderLockCounter = (dwThreadState & READER_RECURRENCE_MASK);

	if (lpdwWriterLockCounter)
		*lpdwWriterLockCounter = (dwThreadState / WRITER_RECURRENCE_UNIT);
}
