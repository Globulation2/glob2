// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "RelayLog.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace Relay
{
std::int64_t unixNow()
{
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string utcTimestamp(std::int64_t unixSeconds)
{
	const std::time_t t = static_cast<std::time_t>(unixSeconds);
	std::tm tm{};
	gmtime_r(&t, &tm);
	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
	return buffer;
}

void logLine(const char* level, const std::string& message)
{
	static std::mutex lock;
	std::lock_guard<std::mutex> guard(lock);
	std::fprintf(stderr, "%s %s %s\n", utcTimestamp(unixNow()).c_str(), level, message.c_str());
	std::fflush(stderr);
}
}
