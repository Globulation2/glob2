// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#pragma once
#include "YOGConnectionScreen.h"

class YOGRegisterScreen : public YOGConnectionScreen
{
  public:
	///Construct with the given YOG client, which should not yet be connected.
	explicit YOGRegisterScreen(std::shared_ptr<YOGClient> client);
	~YOGRegisterScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	enum
	{
		Cancelled,
		Connected,
	};

  protected:
	void onEscape() override { endExecute(Cancelled); }

  private:
	///Responds to YOG events
	void handleYOGClientEvent(std::shared_ptr<YOGClientEvent> event) override;
	void submitRegistrationCredentials();
	void registerAccount();
	std::string nickname, password, passwordRepeat;
};
