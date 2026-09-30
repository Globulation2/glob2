// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#include "YOGRegisterScreen.h"
#include "GlobalContainer.h"
#include "YOGClient.h"
#include "YOGClientEvent.h"

using namespace Glob2UI;
using std::static_pointer_cast;

YOGRegisterScreen::YOGRegisterScreen(std::shared_ptr<YOGClient> client)
	: YOGConnectionScreen(client), nickname(globalContainer->settings.getUsername())
{
	client->addEventListener(this);
}

YOGRegisterScreen::~YOGRegisterScreen()
{
	client->removeEventListener(this);
}

Element YOGRegisterScreen::build(const Presentation &p)
{
	TextFieldOptions secret;
	secret.password = true;
	secret.maxLength = 32;
	TextFieldOptions repeat = secret;
	repeat.submit = [this](const std::string &) { registerAccount(); };
	auto body = scroll("register/scroll",
					   column({statusRow(p),
							   form({field(tr("[Enter your nickname :]"),
										   textField("nickname", nickname, [this](const std::string &v) { nickname = v; }, {false, 32, "", {}, true})),
									 field(tr("[Enter your password :]"),
										   textField("password", password, [this](const std::string &v) { password = v; }, secret)),
									 field(tr("[Repeat your password :]"),
										   textField("repeat", passwordRepeat, [this](const std::string &v) { passwordRepeat = v; }, repeat))})}));
	return page(tr("[Register]"), body,
				actions({{"register", tr("[Register]"), [this] { registerAccount(); }, true, SDLK_RETURN, !connecting},
						 {"cancel", tr("[Cancel]"), [this] { endExecute(Cancelled); }, false, SDLK_ESCAPE}},
						p),
				p, 640);
}

void YOGRegisterScreen::registerAccount()
{
	if (connecting)
		return;
	if (password != passwordRepeat)
	{
		setStatus("[YESTS_PASSWORDS_DONT_MATCH]");
		return;
	}
	if (password.empty())
		return;
	setConnecting(true);
	setStatus("[YESTS_CONNECTING]");
	client->connect(YOG_SERVER_IP);
	connectionAttemptPending = true;
}

void YOGRegisterScreen::handleYOGClientEvent(std::shared_ptr<YOGClientEvent> event)
{
	const Uint8 type = event->getEventType();
	if (type == YEConnected)
		submitRegistrationCredentials();
	else if (type == YEConnectionLost)
	{
		setConnecting(false);
		setStatus("[YESTS_CONNECTION_LOST]");
	}
	else if (type == YELoginAccepted)
	{
		setConnecting(false);
		endExecute(Connected);
	}
	else if (type == YELoginRefused)
	{
		auto info = static_pointer_cast<YOGLoginRefusedEvent>(event);
		setConnecting(false);
		reportRefusal(info->getReason());
		client->disconnect();
	}
}

void YOGRegisterScreen::submitRegistrationCredentials()
{
	client->attemptRegistration(nickname, password);
}
