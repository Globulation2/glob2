// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "YOGConnectionScreen.h"

namespace GAGGUI
{
class ScreenStack;
}

///Connects the user to YOG and logs them in. The client must not be connected yet.
class YOGLoginScreen : public YOGConnectionScreen
{
  public:
    const char* recordingId() const override { return "yoglogin"; }
	YOGLoginScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client);
	~YOGLoginScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

	enum
	{
		Cancelled,
		LoggedIn,
		ConnectionLost,
	};

  protected:
	void onEscape() override { endExecute(Cancelled); }
	void onTimer(Uint32 tick) override;

  private:
	///Responds to YOG events
	void handleYOGClientEvent(std::shared_ptr<YOGClientEvent> event) override;
	///Submit a login using the entered credentials
	void submitLoginCredentials();
	void login();
	void openRegistration();
	bool lobbyRequested = false;
	std::string nickname, password;
	bool rememberPassword = false;
	GAGGUI::ScreenStack &screens;
};
