// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <functional>
#include <optional>

namespace GAGCore
{
// Main-thread receipt for the actual consumer of a buffered event range. A null
// count means pending; a count resolves the delivered prefix, including zero.
struct InputDispatchReceipt
{
	std::optional<std::size_t> consumed;
	std::function<bool()> cancelled;
	bool isCancelled() const { return cancelled && cancelled(); }
};
} // namespace GAGCore
