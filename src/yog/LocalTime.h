// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

/// Local wall-clock time for the YOG lobby and server, replacing
/// boost::posix_time::ptime and second_clock::local_time().
///
/// Times count microseconds from 1970-01-01 00:00:00 *local* time, with no
/// time zone attached, and now() has whole-second resolution, as before.
/// Server files (banned IPs, player mute times, game log names) store times
/// in Boost's text form, which toString() and parseLocalTime() keep:
/// "2026-Sep-29 14:00:00", with ".ffffff" appended for fractional seconds, and
/// "not-a-date-time", "+infinity" or "-infinity" for the special values.
struct LocalClock
{
	typedef std::chrono::microseconds duration;
	typedef duration::rep rep;
	typedef duration::period period;
	typedef std::chrono::time_point<LocalClock> time_point;
	static const bool is_steady = false;

	/// Current local time, truncated to whole seconds
	static time_point now()
	{
		std::time_t t = std::time(nullptr);
		std::tm local;
#ifdef _WIN32
		local = *std::localtime(&t); // thread-local buffer in the Windows CRT
#else
		localtime_r(&t, &local);
#endif
		return fromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday)
			+ std::chrono::hours(local.tm_hour) + std::chrono::minutes(local.tm_min) + std::chrono::seconds(local.tm_sec);
	}

	/// Midnight starting the given date (month 1-12)
	static time_point fromCivil(std::int64_t year, unsigned month, unsigned day)
	{
		// days-from-civil conversion on the proleptic Gregorian calendar
		year -= month <= 2;
		const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
		const unsigned yearOfEra = unsigned(year - era * 400);
		const unsigned dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
		const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
		const std::int64_t days = era * 146097 + std::int64_t(dayOfEra) - 719468;
		return time_point(std::chrono::hours(24 * days));
	}
};

typedef LocalClock::time_point LocalTime;

/// Boost's not_a_date_time: unset or unparseable. Like Boost, it compares
/// equal only to itself and orders after every real time.
inline LocalTime notADateTime() { return LocalTime::max() - LocalClock::duration(1); }
inline LocalTime positiveInfinity() { return LocalTime::max(); }
inline LocalTime negativeInfinity() { return LocalTime::min(); }

/// Time elapsed since the preceding local midnight
inline std::chrono::microseconds timeOfDay(LocalTime t)
{
	const std::chrono::microseconds day = std::chrono::hours(24);
	std::chrono::microseconds r = t.time_since_epoch() % day;
	return r.count() < 0 ? r + day : r;
}

namespace LocalTimeDetail
{
	inline constexpr const char* monthNames[12] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
}

/// Boost ptime text form, e.g. "2026-Sep-29 14:00:00"
inline std::string toString(LocalTime t)
{
	if (t == notADateTime())
		return "not-a-date-time";
	if (t == positiveInfinity())
		return "+infinity";
	if (t == negativeInfinity())
		return "-infinity";

	const std::int64_t microsPerDay = 86400000000LL;
	std::int64_t micros = t.time_since_epoch().count();
	std::int64_t days = micros / microsPerDay;
	if (micros % microsPerDay < 0)
		--days;
	micros -= days * microsPerDay;

	// civil-from-days conversion, the inverse of LocalClock::fromCivil
	const std::int64_t z = days + 719468;
	const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned dayOfEra = unsigned(z - era * 146097);
	const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
	const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
	const unsigned mp = (5 * dayOfYear + 2) / 153;
	const unsigned day = dayOfYear - (153 * mp + 2) / 5 + 1;
	const unsigned month = mp < 10 ? mp + 3 : mp - 9;
	const std::int64_t year = std::int64_t(yearOfEra) + era * 400 + (month <= 2);

	const std::int64_t seconds = micros / 1000000;
	const std::int64_t fraction = micros % 1000000;
	char buffer[64];
	int length = std::snprintf(buffer, sizeof(buffer), "%04lld-%s-%02u %02d:%02d:%02d",
		(long long)year, LocalTimeDetail::monthNames[month - 1], day,
		int(seconds / 3600), int(seconds / 60 % 60), int(seconds % 60));
	if (fraction != 0)
		std::snprintf(buffer + length, sizeof(buffer) - length, ".%06d", int(fraction));
	return buffer;
}

/// Parses toString()'s form (and Boost's), ignoring surrounding whitespace;
/// the time of day is optional. Leaves result unchanged and returns false
/// when the text is not a time, as Boost's stream extraction did.
inline bool parseLocalTime(const std::string& text, LocalTime& result)
{
	std::size_t begin = text.find_first_not_of(" \t\r\n");
	std::size_t end = text.find_last_not_of(" \t\r\n");
	if (begin == std::string::npos)
		return false;
	const std::string s = text.substr(begin, end - begin + 1);
	if (s == "not-a-date-time")
	{
		result = notADateTime();
		return true;
	}
	if (s == "+infinity")
	{
		result = positiveInfinity();
		return true;
	}
	if (s == "-infinity")
	{
		result = negativeInfinity();
		return true;
	}

	int year, day, consumed = 0;
	char monthText[4];
	if (std::sscanf(s.c_str(), "%d-%3[A-Za-z]-%d%n", &year, monthText, &day, &consumed) != 3)
		return false;
	unsigned month = 0;
	for (unsigned m = 0; m < 12 && month == 0; ++m)
	{
		const char* name = LocalTimeDetail::monthNames[m];
		bool same = true;
		for (int c = 0; c < 3; ++c)
			if (std::tolower((unsigned char)monthText[c]) != std::tolower((unsigned char)name[c]))
				same = false;
		if (same)
			month = m + 1;
	}
	if (month == 0 || day < 1 || day > 31)
		return false;

	LocalTime parsed = LocalClock::fromCivil(year, month, unsigned(day));
	int hours, minutes, seconds, more = 0;
	if (std::sscanf(s.c_str() + consumed, " %d:%d:%d%n", &hours, &minutes, &seconds, &more) == 3)
	{
		parsed += std::chrono::hours(hours) + std::chrono::minutes(minutes) + std::chrono::seconds(seconds);
		const char* rest = s.c_str() + consumed + more;
		if (*rest == '.')
		{
			std::int64_t micros = 0;
			int digits = 0;
			for (++rest; std::isdigit((unsigned char)*rest) && digits < 6; ++rest, ++digits)
				micros = micros * 10 + (*rest - '0');
			for (; digits < 6; ++digits)
				micros *= 10;
			parsed += std::chrono::microseconds(micros);
		}
	}
	result = parsed;
	return true;
}
