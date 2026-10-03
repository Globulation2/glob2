// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include <string>
#include <vector>

// A decision with explicit completion: the chosen caption index, or app quit.
class MessageScreen : public Glob2UI::Screen
{
  public:
	MessageScreen(const std::string &message, const std::vector<std::string> &captions);
	/// A compact titled notice ("Room closed" and why) instead of a full page.
	MessageScreen(const std::string &title, const std::string &message, const std::vector<std::string> &captions);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	/// The highlighted choice, which Enter picks (the first by default). Escape always
	/// picks the last; put the safe choice there and make it primary for confirmations.
	void setPrimary(int index) { primary = index; }

  protected:
	void onEscape() override { endExecute(int(captions.size()) - 1); }

  private:
	std::string title, message;
	std::vector<std::string> captions;
	int primary = 0;
};
