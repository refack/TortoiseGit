#pragma once

#include "ReaderWriterLock.h"
#include <string>
#include <vector>

// #pragma comment (lib, "psapi.lib")

/**
 * Collects the profiling info for a given profiled block / line.
 * Records execution count, min, max and accumulated execution time
 * in CPU clock ticks.
 */

class CProfilingRecord
{
	/// identification

	const char *name;
	const char *file;
	int line;

	/// collected profiling info

	size_t count;
	unsigned __int64 sum;
	unsigned __int64 minValue;
	unsigned __int64 maxValue;

  public:
	/// construction

	CProfilingRecord(const char *name, const char *file, int line)
		: name(name), file(file), line(line), count(0), sum(0), minValue(ULLONG_MAX), maxValue(0)
	{
	}
	/// record values

	void Add(unsigned __int64 value);

	/// modification

	void Reset();

	/// data access

	[[nodiscard]] const char *GetName() const
	{
		return name;
	}
	[[nodiscard]] const char *GetFile() const
	{
		return file;
	}
	[[nodiscard]] int GetLine() const
	{
		return line;
	}

	[[nodiscard]] size_t GetCount() const
	{
		return count;
	}
	[[nodiscard]] unsigned __int64 GetSum() const
	{
		return sum;
	}
	[[nodiscard]] unsigned __int64 GetMinValue() const
	{
		return minValue;
	}
	[[nodiscard]] unsigned __int64 GetMaxValue() const
	{
		return maxValue;
	}
};

/**
 * RAII class that encapsulates a single execution of a profiled
 * block / line. The result gets added to an existing profiling record.
 */

class CRecordProfileEvent
{
  private:
	CProfilingRecord *record;

	/// the initial CPU counter value

	unsigned __int64 start;

  public:
	/// construction: start clock

	CRecordProfileEvent(CProfilingRecord *aRecord);

	/// destruction: time interval to profiling record,
	/// if Stop() had not been called before

	~CRecordProfileEvent();

	/// don't wait for destruction

	void Stop();
};

#ifndef _DEBUG

/// construction / destruction

inline CRecordProfileEvent::CRecordProfileEvent(CProfilingRecord *aRecord)
	: record(aRecord), start(ReadTimeStampCounter())
{
}

inline CRecordProfileEvent::~CRecordProfileEvent()
{
	if (record)
		record->Add(ReadTimeStampCounter() - start);
}

#endif

/// don't wait for destruction

inline void CRecordProfileEvent::Stop()
{
	if (record) {
		record->Add(ReadTimeStampCounter() - start);
		record = nullptr;
	}
}

/**
 * Singleton class that acts as container for all profiling records.
 * You may reset its content as well as write it to disk.
 */

class CProfilingInfo
{
	using TRecords = std::vector<CProfilingRecord *>;
	TRecords records;

	/// construction / destruction

  public:
	CProfilingInfo() = default;
	~CProfilingInfo();
	// prevent cloning
	CProfilingInfo(const CProfilingInfo &) = delete;
	CProfilingInfo &operator=(const CProfilingInfo &) = delete;

	/// create report

	[[nodiscard]] std::string GetReport() const;

	/// access to default instance

	static CProfilingInfo *GetInstance();

	/// add a new record

	CProfilingRecord *Create(const char *name, const char *file, int line);
};

/**
 * Profiling macros
 */

#define PROFILE_CONCAT3(a, b) a##b
#define PROFILE_CONCAT2(a, b) PROFILE_CONCAT3(a, b)
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT2(a, b)

/// measures the time from the point of usage to the end of the respective block

#define PROFILE_BLOCK                                                                                                                    \
	static CProfilingRecord *PROFILE_CONCAT(record, __LINE__) = CProfilingInfo::GetInstance()->Create(__FUNCTION__, __FILE__, __LINE__); \
	CRecordProfileEvent PROFILE_CONCAT(profileSection, __LINE__)(PROFILE_CONCAT(record, __LINE__));

/// measures the time taken to execute the respective code line

#define PROFILE_LINE(line)                                                                                                               \
	static CProfilingRecord *PROFILE_CONCAT(record, __LINE__) = CProfilingInfo::GetInstance()->Create(__FUNCTION__, __FILE__, __LINE__); \
	CRecordProfileEvent PROFILE_CONCAT(profileSection, __LINE__)(PROFILE_CONCAT(record, __LINE__));                                      \
	line;                                                                                                                                \
	PROFILE_CONCAT(profileSection, __LINE__).Stop();


//////////////////////////////////////////////////////////////////////
// record values
//////////////////////////////////////////////////////////////////////

inline void CProfilingRecord::Add(unsigned __int64 value)
{
	++count;
	sum += value;

	if (value < minValue)
		minValue = value;
	if (value > maxValue)
		maxValue = value;
}

//////////////////////////////////////////////////////////////////////
// modification
//////////////////////////////////////////////////////////////////////

inline void CProfilingRecord::Reset()
{
	count = 0;
	sum = 0;

	minValue = LLONG_MAX;
	maxValue = 0;
}

//////////////////////////////////////////////////////////////////////
// construction / destruction
//////////////////////////////////////////////////////////////////////

CProfilingInfo::~CProfilingInfo()
{
	// if (!records.empty())
	// {
	// 	// write profile to file
	//
	// 	wchar_t buffer [MAX_PATH] = { 0 };
	// 	if (GetModuleFileNameEx(GetCurrentProcess(), nullptr, buffer, _countof(buffer)) > 0)
	// 		try
	// 		{
	// 			std::wstring fileName (buffer);
	// 			fileName += L".profile";
	//
	// 			std::string report = GetInstance()->GetReport();
	//
	// 			CFile file (fileName.c_str(), CFile::modeCreate | CFile::modeWrite );
	// 			file.Write(report.c_str(), static_cast<UINT>(report.size()));
	// 		}
	// 		catch (...)
	// 		{
	// 			// ignore all file errors etc.
	// 		}
	//
	//
	// 	// free data
	//
	// 	for (size_t i = 0; i < records.size(); ++i)
	// 		delete records[i];
	// }
}

//////////////////////////////////////////////////////////////////////
// access to default instance
//////////////////////////////////////////////////////////////////////

inline CProfilingInfo *CProfilingInfo::GetInstance()
{
	static CProfilingInfo instance;
	return &instance;
}

//////////////////////////////////////////////////////////////////////
// create a report
//////////////////////////////////////////////////////////////////////

static std::string IntToStr(unsigned __int64 value)
{
	char buffer[100] = { 0 };
	_ui64toa_s(value, buffer, 100, 10);

	std::string result = buffer;
	for (size_t i = 3; i < result.length(); i += 4)
		result.insert(result.length() - i, 1, ',');

	return result;
};

inline std::string CProfilingInfo::GetReport() const
{
	enum { LINE_LENGTH = 500 };

	char lineBuffer[LINE_LENGTH];
	const char *const format = "%10s%17s%17s%17s%6s %s\t%s\n";

	std::string result;
	result.reserve(LINE_LENGTH * records.size());
	sprintf_s(lineBuffer, format, "count", "sum", "min", "max", "line", "name", "file");
	result += lineBuffer;

	for (const auto record : records) {
		unsigned __int64 minValue = record->GetMinValue();
		if (minValue == ULLONG_MAX)
			minValue = 0;

		sprintf_s(lineBuffer, format, IntToStr(record->GetCount()).c_str(), IntToStr(record->GetSum()).c_str(), IntToStr(minValue).c_str(), IntToStr(record->GetMaxValue()).c_str()

																																				  ,
			IntToStr(record->GetLine()).c_str(),
			record->GetName(),
			record->GetFile());

		result += lineBuffer;
	}

	return result;
}

//////////////////////////////////////////////////////////////////////
// add a new record
//////////////////////////////////////////////////////////////////////

inline CProfilingRecord *CProfilingInfo::Create(const char *name, const char *file, int line)
{
	auto record = new CProfilingRecord(name, file, line);
	records.push_back(record);

	return record;
}
