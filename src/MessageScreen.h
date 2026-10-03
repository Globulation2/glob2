// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include <string>
#include <vector>

// A decision with explicit completion: the chosen caption index, or app quit.
class MessageScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "message"; }
	MessageScreen(const std::string &message, const std::vector<std::string> &captions);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

  protected:
	void onEscape() override { endExecute(int(captions.size()) - 1); }

  private:
	std::string message;
	std::vector<std::string> captions;
};
