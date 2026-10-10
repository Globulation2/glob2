// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include <functional>

namespace Glob2UI
{
// Closing the panel returns to its owning menu without changing the session.
class DiscordCommunity
{
  public:
	bool visible = false;
	void open()
	{
		visible = true;
		status.clear();
	}
	Element build(const Presentation &p, std::function<void()> changed);

  private:
	std::string status;
};
} // namespace Glob2UI
