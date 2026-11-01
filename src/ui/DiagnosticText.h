// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/ApiErrorText.h"
#include <initializer_list>
#include <string_view>

namespace Glob2UI
{
inline bool bulgarianErrorLocale()
{
	const auto locale = onlineErrorLocale();
	return locale == "bg" || locale == "bg-BG";
}
// Exact native source binding with an explicit English fallback when a key is
// unavailable. Data parameters are substituted literally in one pass below.
inline std::string nativeSurfaceText(const std::string &key, const std::string &english)
{
	auto *strings = GAGCore::Toolkit::getStringTable();
	return strings && strings->doesStringExist(key) ? strings->getString(key) : english;
}
inline std::string literalSurfaceFormat(std::string_view text,
	std::initializer_list<std::string_view> values)
{
	std::string out;
	for (std::size_t i = 0; i < text.size();)
	{
		if (text[i] == '%' && i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9')
		{
			const auto slot = std::size_t(text[i + 1] - '0');
			if (slot < values.size()) { out += *(values.begin() + slot); i += 2; continue; }
		}
		out += text[i++];
	}
	return out;
}
// One bounded cache per UI diagnostic context. Repaints of the same failure do
// not repeat logs. Underlying model/report bytes and readiness remain untouched.
class DiagnosticText
{
	std::string lastIdentity, lastLocale, lastSummary, display;
	bool hasValue = false;
  public:
	std::string exception(const std::exception &error, const std::string &summary = {})
	{
		const auto *marked = dynamic_cast<const PresentedError *>(&error);
		std::string identity = marked ? marked->diagnostic : std::string();
		identity += '\0'; identity += error.what();
		const auto locale = onlineErrorLocale();
		if (!hasValue || identity != lastIdentity || locale != lastLocale || summary != lastSummary)
		{
			display = exceptionText(error, summary);
			lastIdentity = std::move(identity); lastLocale = locale; lastSummary = summary; hasValue = true;
		}
		return display;
	}
	std::string operator()(const std::string &raw)
	{
		if (raw.empty()) { hasValue = false; return {}; }
		std::string summary;
		if (bulgarianErrorLocale())
		{
			auto *strings = GAGCore::Toolkit::getStringTable();
			const auto key = "[" + raw + "]";
			summary = strings && strings->doesStringExist(key)
				? strings->getString(key) : onlineProblemText(raw);
		}
		return exception(std::runtime_error(raw), summary);
	}
};
} // namespace Glob2UI
