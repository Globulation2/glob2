// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include <CooperativeSlice.h>
#include <functional>
#include <memory>
#include <optional>
class Engine;
// Cooperative loading: a reused engine must have finalized its active session.
class GameLoadScreen : public Glob2UI::Screen
{
  public:
	using Initializer = std::function<GAGCore::CooperativeTask(Engine &)>;
	explicit GameLoadScreen(Initializer initialize,
							GAGCore::CooperativeSlice slice = GAGCore::CooperativeSlice());
	GameLoadScreen(std::unique_ptr<Engine> engine, Initializer initialize,
				   GAGCore::CooperativeSlice slice = GAGCore::CooperativeSlice());
	~GameLoadScreen() override;
	std::unique_ptr<Engine> takeEngine();
	std::string failureMessage() const;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32) override;
	Uint32 executionDelay(Uint32, Uint32) override { return 1; }

  protected:
	void onEscape() override { endExecute(0); }

  private:
	GAGCore::CooperativeSlice slice;
	std::string previousRng;
	std::unique_ptr<Engine> engine;
	std::optional<GAGCore::CooperativeTask> task;
	std::string status;
	std::string failureDiagnostic;
	bool accepted = false;
};
