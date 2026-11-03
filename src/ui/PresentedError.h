// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Glob2UI
{
// UI boundary only. Never throw this from a model, protocol, storage or CLI layer.
class PresentedError : public std::runtime_error
{
  public:
	explicit PresentedError(const std::string &text, std::string diagnostic = {})
		: std::runtime_error(text), diagnostic(std::move(diagnostic)) {}
	const std::string diagnostic;
};

// The caller supplies an already-reviewed summary for this context. Ordinary
// exception prose is diagnostic data; a marker preserves deliberately presented text.
inline std::string presentException(const std::exception &error, std::string_view locale,
								   const std::string &summary)
{
	const auto *presented = dynamic_cast<const PresentedError *>(&error);
	const auto *raw = presented && !presented->diagnostic.empty()
		? presented->diagnostic.c_str() : error.what();
	std::fprintf(stderr, "UI operation failed: %s\n", raw);
	if (locale != "bg" && locale != "bg-BG" && locale != "th" && locale != "th-TH") return raw;
	if (presented) return error.what();
	return summary;
}
} // namespace Glob2UI
