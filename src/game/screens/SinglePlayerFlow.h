// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ScreenStack.h>
#include "GameLoadScreen.h"
#include "OnlineHandoff.h"
#include <memory>
#include <string>
class Engine;

// Lives alongside its stack until the flow finishes. Owns navigation only;
// simulation and file formats remain in Engine.
class SinglePlayerFlow
{
  public:
	explicit SinglePlayerFlow(GAGGUI::ScreenStack &screens) : screens(screens) {}
	void custom(const std::optional<Online::MapPlayRequest> &map = std::nullopt);
	void load();
    void introduction();
    void lessons();
	void studio();
	void generatorStudio();
	void replay(const std::string &filename);

  private:
	GAGGUI::ScreenStack &screens;
	enum class ReturnTo { MainMenu, CustomSetup, Lessons };
    void returnTo(ReturnTo destination);
	void launch(GameLoadScreen::Initializer initialize, ReturnTo destination,
				std::shared_ptr<void> mapFile = nullptr);
};
