// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include <CooperativeSlice.h>
#include <optional>
#include <memory>
#include <functional>
class MapEdit;
class EditorLoadScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "editor_load"; }
	explicit EditorLoadScreen(const std::string &filename,
							  GAGCore::CooperativeSlice slice = GAGCore::CooperativeSlice());
	~EditorLoadScreen() override;
	std::unique_ptr<MapEdit> takeEditor();
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32) override;
	Uint32 executionDelay(Uint32, Uint32) override { return 1; }

  protected:
	void onEscape() override { endExecute(0); }
	using Initializer = std::function<GAGCore::CooperativeTask(MapEdit &)>;
	EditorLoadScreen(Initializer initialize, const char *caption,
					 GAGCore::CooperativeSlice slice = GAGCore::CooperativeSlice());

  private:
	GAGCore::CooperativeTask prepare(Initializer initialize);
	GAGCore::CooperativeSlice slice;
	std::string previousRng;
	std::unique_ptr<MapEdit> editor;
	std::optional<GAGCore::CooperativeTask> task;
	std::string status;
	bool accepted = false;
};
