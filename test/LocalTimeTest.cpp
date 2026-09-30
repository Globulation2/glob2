// SPDX-License-Identifier: GPL-3.0-or-later
//
// LocalTime replaced boost::posix_time::ptime in the YOG server, whose files
// (banned IPs, player mute times, hourly game-log names) store times in
// Boost's text form. Expected strings below are what Boost 1.83 printed.

#include "Glob2Test.h"
#include "../src/yog/LocalTime.h"
#include <string>

namespace
{
void check(bool ok, const std::string& what)
{
	CHECK_MESSAGE(ok, (what));
}

LocalTime at(int year, unsigned month, unsigned day, int h, int m, int s, int us = 0)
{
	return LocalClock::fromCivil(year, month, day) + std::chrono::hours(h) + std::chrono::minutes(m)
		+ std::chrono::seconds(s) + std::chrono::microseconds(us);
}
}

TEST_SUITE("LocalTime")
{
TEST_CASE("keeps boost::posix_time's text form; parsing and special values")
{
	struct FormatCase
	{
		LocalTime time;
		const char* text;
	};
	const FormatCase cases[] = {
		{at(2026, 9, 29, 14, 0, 0), "2026-Sep-29 14:00:00"},
		{at(2009, 3, 5, 4, 5, 6), "2009-Mar-05 04:05:06"},
		{at(2026, 9, 29, 2, 49, 3, 100), "2026-Sep-29 02:49:03.000100"},
		{at(2000, 2, 29, 23, 59, 59), "2000-Feb-29 23:59:59"},
		{at(1970, 1, 1, 0, 0, 0), "1970-Jan-01 00:00:00"},
		{at(1969, 12, 31, 23, 59, 59, 500000), "1969-Dec-31 23:59:59.500000"},
		{at(1400, 1, 1, 0, 0, 0), "1400-Jan-01 00:00:00"},
		{at(9999, 12, 31, 23, 59, 59, 999999), "9999-Dec-31 23:59:59.999999"},
		{at(2400, 2, 29, 0, 0, 0), "2400-Feb-29 00:00:00"},
		{at(2100, 12, 31, 12, 30, 0), "2100-Dec-31 12:30:00"},
		{notADateTime(), "not-a-date-time"},
		{positiveInfinity(), "+infinity"},
		{negativeInfinity(), "-infinity"},
	};
	for (const FormatCase& c : cases)
	{
		check(toString(c.time) == c.text, std::string("format ") + c.text + " gave " + toString(c.time));
		LocalTime parsed = at(2001, 1, 1, 0, 0, 0);
		check(parseLocalTime(c.text, parsed) && parsed == c.time, std::string("parse ") + c.text);
	}

	// Boost's reader also accepted these; a date alone means midnight.
	LocalTime parsed;
	check(parseLocalTime(" 2026-sep-29 14:00:00 ", parsed) && parsed == at(2026, 9, 29, 14, 0, 0), "lower-case month");
	check(parseLocalTime("2026-Sep-29", parsed) && parsed == at(2026, 9, 29, 0, 0, 0), "date only");
	check(parseLocalTime("2026-SEP-29 01:02:03.5", parsed) && parsed == at(2026, 9, 29, 1, 2, 3, 500000), "short fraction");

	// Reject invalid dates, overflow and incomplete/trailing time text without
	// changing a caller's existing expiration time. Only persisted time syntax
	// is supported; Boost's permissive duration/trailing-text extensions are not.
	for (const char* bad : {
		"", "garbage", "2026-Foo-29 01:02:03", "2026-J-01", "2026-Ja-01",
		"2026-Feb-31", "2026-Feb-29", "1900-Feb-29", "2100-Feb-29", "2026-Apr-31",
		"2026-Jan-00", "2026-Jan-32", "1399-Dec-31", "10000-Jan-01", "-2026-Jan-01",
		"2147483647-Jan-01 00:00:00", "999999999999999999999999-Jan-01",
		"2026-Jan-999999999999999999999999", "2026-Sep-29 garbage", "2026-Sep-29 12:",
		"2026-Sep-29 12:00", "2026-Sep-29 24:00:00", "2026-Sep-29 12:60:00",
		"2026-Sep-29 12:00:60", "2026-Sep-29 -1:00:00", "2026-Sep-29 2147483647:00:00",
		"2026-Sep-29 999999999999999999999999:00:00", "2026-Sep-29 12:00:00.",
		"2026-Sep-29 12:00:00.1234567", "2026-Sep-29 12:00:00garbage",
		"2026-Sep-29 12:00:00.123garbage", "2026-Sep-29junk"})
	{
		LocalTime kept = at(2001, 1, 1, 0, 0, 0);
		check(!parseLocalTime(bad, kept) && kept == at(2001, 1, 1, 0, 0, 0), std::string("reject [") + bad + "]");
	}

	// not-a-date-time equals only itself and sorts after real times, so a
	// ban whose time failed to load never expires, as before.
	const LocalTime now = LocalClock::now();
	check(notADateTime() == notADateTime() && notADateTime() != now, "not-a-date-time equality");
	check(now < notADateTime() && notADateTime() < positiveInfinity(), "not-a-date-time ordering");
	check(now.time_since_epoch() % std::chrono::seconds(1) == std::chrono::microseconds(0), "now() has whole seconds");

	check(timeOfDay(at(2026, 9, 29, 14, 5, 6)) == std::chrono::hours(14) + std::chrono::minutes(5) + std::chrono::seconds(6), "timeOfDay");
	check(timeOfDay(at(1969, 12, 31, 23, 0, 0)) == std::chrono::hours(23), "timeOfDay before 1970");
	check(toString(std::chrono::floor<std::chrono::hours>(at(2026, 9, 29, 14, 59, 59, 1))) == "2026-Sep-29 14:00:00", "hour of game log");

}
}
