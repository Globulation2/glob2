// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PlatformProtocol.h"

namespace Online
{
// Presentation only. The decoded raw code/message/details and the client/model
// diagnostics stay unchanged. Bulgarian and Thai have reviewed resources.
std::string presentApiError(const ApiError &error, std::string_view locale);
std::string presentOnlineProblem(const std::string &diagnostic, std::string_view locale);
} // namespace Online
