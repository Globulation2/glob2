// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <BuildFeatures.h>
#include "online/ApiErrorText.h"
#include "ui/PresentedError.h"
#include <StringTable.h>
#include <Toolkit.h>

namespace Glob2UI
{
inline std::string onlineErrorLocale()
{
	auto *strings = GAGCore::Toolkit::getStringTable();
	return strings && strings->doesStringExist("[language-code]")
		? strings->getString("[language-code]") : "en";
}
inline std::string apiErrorText(const Online::ApiError &error)
{
	return Online::presentApiError(error, onlineErrorLocale());
}
inline std::string onlineProblemText(const std::string &diagnostic)
{
#if GLOB2_HAS_ONLINE
	return Online::presentOnlineProblem(diagnostic, onlineErrorLocale());
#else
	return diagnostic;
#endif
}
inline std::string exceptionText(const std::exception &error, const std::string &summary = {})
{
	const auto locale = onlineErrorLocale();
#if GLOB2_HAS_ONLINE
	return presentException(error, locale, summary.empty()
		? Online::presentOnlineProblem("", locale) : summary);
#else
	// Without the online platform there is no reviewed problem catalog.
	return presentException(error, locale, summary.empty() ? std::string(error.what()) : summary);
#endif
}
} // namespace Glob2UI
