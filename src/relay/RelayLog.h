// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// One-line structured log records on stderr: "<UTC time> <level> <message>".

#include <cstdint>
#include <string>

namespace Relay
{
	void logLine(const char* level, const std::string& message);
	/// RFC 3339 UTC timestamp with seconds precision, as in the protocol's Timestamp.
	std::string utcTimestamp(std::int64_t unixSeconds);
	std::int64_t unixNow();
}
