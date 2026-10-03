// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"

class RecordingFilesScreen final : public Glob2UI::Screen
{
  public:
	RecordingFilesScreen();
	const char *recordingId() const override { return "recording_files"; }
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
  protected:
	void onEscape() override { if (!deleting.empty()) { deleting.clear(); invalidate(); } else endExecute(0); }
	void beforePaint() override;
  private:
	std::string deleting, error;
	std::uint32_t refreshed = 0;
};
