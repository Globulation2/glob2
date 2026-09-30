// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "YOGLoginScreen.h"
#include "GlobalContainer.h"
#include "SessionTabsScreen.h"
#include "Settings.h"
#include "YOGClient.h"
#include "YOGClientEvent.h"
#include "YOGClientLobbyScreen.h"
#include "YOGClientMapDownloadScreen.h"
#include "YOGClientOptionsScreen.h"
#include "YOGRegisterScreen.h"
#include <ScreenStack.h>

using namespace Glob2UI;
using std::static_pointer_cast;

namespace
{
// Tabs outlive their asynchronous children and are destroyed before the session
// screen releases the widgets they lent it. No tab lives on a modal call stack.
class YOGSessionScreen final : public SessionTabsScreen
{
	YOGClientLobbyScreen lobby;
	YOGClientOptionsScreen options;
	YOGClientMapDownloadScreen maps;

  public:
	YOGSessionScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client)
		: lobby(screens, client), options(client), maps(screens, client)
	{
		addTab(&lobby, true);
		addTab(&options, true);
		addTab(&maps, true);
	}
	~YOGSessionScreen() override
	{
		removeTab(&maps);
		removeTab(&options);
		removeTab(&lobby);
	}
};
} // namespace

YOGLoginScreen::YOGLoginScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client)
	: YOGConnectionScreen(client), nickname(globalContainer->settings.getUsername()),
	  password(globalContainer->settings.getPasswd()), rememberPassword(!password.empty()),
	  screens(screens)
{
	client->addEventListener(this);
}

YOGLoginScreen::~YOGLoginScreen()
{
	client->removeEventListener(this);
}

Element YOGLoginScreen::build(const Presentation &p)
{
	TextFieldOptions passwordOptions;
	passwordOptions.password = true;
	passwordOptions.maxLength = 32;
	passwordOptions.submit = [this](const std::string &) { login(); };
	auto body = scroll("login/scroll",
					   column({statusRow(p),
							   form({field(tr("[Enter your nickname :]"),
										   textField("nickname", nickname, [this](const std::string &v) { nickname = v; }, {false, 32})),
									 field(tr("[Enter your password :]"),
										   textField("password", password, [this](const std::string &v) { password = v; }, passwordOptions))}),
							   toggle("remember", tr("[Remember YOG password localy]"), rememberPassword,
									  [this](bool v) { rememberPassword = v; })}));
	return page(tr("[yog]"), body,
				actions({{"login", tr("[login]"), [this] { login(); }, true, SDLK_RETURN, !connecting},
						 {"register", tr("[Register]"), [this] { openRegistration(); }, false, SDLK_UNKNOWN, !connecting},
						 {"cancel", tr("[Cancel]"), [this] { endExecute(Cancelled); }, false, SDLK_ESCAPE}},
						p),
				p, 640);
}

void YOGLoginScreen::login()
{
	if (connecting)
		return;
	setConnecting(true);
	setStatus("[YESTS_CONNECTING]");
	client->connect(YOG_SERVER_IP);
	connectionAttemptPending = true;
}

void YOGLoginScreen::openRegistration()
{
	client->removeEventListener(this);
	screens.push(std::make_unique<YOGRegisterScreen>(client),
				 [this](GAGGUI::Screen &, int rc)
				 {
					 client->addEventListener(this);
					 if (rc == -1)
						 endExecute(-1);
					 else if (rc == YOGRegisterScreen::Connected)
						 lobbyRequested = true;
				 });
}

void YOGLoginScreen::handleYOGClientEvent(std::shared_ptr<YOGClientEvent> event)
{
	const Uint8 type = event->getEventType();
	if (type == YEConnected)
		submitLoginCredentials();
	else if (type == YEConnectionLost)
	{
		lobbyRequested = false;
		setConnecting(false);
		setStatus("[YESTS_CONNECTION_LOST]");
	}
	else if (type == YELoginAccepted)
	{
		setConnecting(false);
		lobbyRequested = true;
	}
	else if (type == YELoginRefused)
	{
		auto info = static_pointer_cast<YOGLoginRefusedEvent>(event);
		setConnecting(false);
		reportRefusal(info->getReason());
		client->disconnect();
	}
}

void YOGLoginScreen::submitLoginCredentials()
{
	if (rememberPassword)
	{
		globalContainer->settings.setPasswd(password);
		globalContainer->settings.setUsername(nickname);
		globalContainer->settings.save();
	}
	client->attemptLogin(nickname, password);
}

void YOGLoginScreen::onTimer(Uint32 tick)
{
	YOGConnectionScreen::onTimer(tick);
	if (!lobbyRequested)
		return;
	lobbyRequested = false;
	// Login acceptance arrives during listener iteration. Defer changing
	// listeners and constructing lobby tabs until the network update returns.
	client->removeEventListener(this);
	screens.push(std::make_unique<YOGSessionScreen>(screens, client),
				 [this](GAGGUI::Screen &, int rc)
				 {
					 if (rc == YOGClientLobbyScreen::ConnectionLost)
						 endExecute(ConnectionLost);
					 else if (rc == -1)
						 endExecute(-1);
					 else
						 endExecute(LoggedIn);
				 });
}
