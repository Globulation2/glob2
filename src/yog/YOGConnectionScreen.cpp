// SPDX-License-Identifier: GPL-3.0-or-later
#include "YOGConnectionScreen.h"
#include "GlobalContainer.h"
#include "YOGClient.h"
#include "YOGConsts.h"
#include <Toolkit.h>

using namespace Glob2UI;

namespace
{
// The login and register screens coexist and share the cached sprite; only
// the last one releases it.
int earthUsers = 0;
} // namespace

YOGConnectionScreen::YOGConnectionScreen(std::shared_ptr<YOGClient> client)
	: status(tr("[YESTS_CREATED]")), client(client)
{
	earth = GAGCore::Toolkit::getSprite("data/gfx/rotatingEarth");
	++earthUsers;
}

YOGConnectionScreen::~YOGConnectionScreen()
{
	if (--earthUsers == 0)
		GAGCore::Toolkit::releaseSprite("data/gfx/rotatingEarth");
	if (globalContainer && globalContainer->gfx)
		globalContainer->gfx->cursorManager.setNextType(GAGCore::CursorManager::CURSOR_NORMAL);
}

void YOGConnectionScreen::setStatus(const std::string &key)
{
	status = tr(key);
	invalidate();
}

void YOGConnectionScreen::setConnecting(bool value)
{
	connecting = value;
	if (globalContainer && globalContainer->gfx)
		globalContainer->gfx->cursorManager.setNextType(
			value ? GAGCore::CursorManager::CURSOR_WAIT : GAGCore::CursorManager::CURSOR_NORMAL);
	invalidate();
}

Element YOGConnectionScreen::statusRow(const Presentation &p)
{
	auto text = paragraph(status);
	if (!connecting || !earth)
		return text;
	// Twenty frames of the rotating earth, two ticks per frame as before.
	auto globe = animation("connecting", earth, 20, 80);
	return row({globe, expanded(text)}, {-1, CrossAlign::Center});
}

void YOGConnectionScreen::reportRefusal(int reason)
{
	const char *key = "[YESTS_CONNECTION_REFUSED_UNEXPLAINED]";
	switch (reason)
	{
	case YOGPasswordIncorrect:
		key = "[YESTS_CONNECTION_REFUSED_BAD_PASSWORD]";
		break;
	case YOGUsernameAlreadyUsed:
		key = "[YESTS_CONNECTION_REFUSED_ALREADY_PASSWORD]";
		break;
	case YOGUserNotRegistered:
		key = "[YESTS_CONNECTION_REFUSED_BAD_PASSWORD_NON_ZERO]";
		break;
	case YOGClientVersionTooOld:
		key = "[network release mismatch]";
		break;
	case YOGAlreadyAuthenticated:
		key = "[YESTS_CONNECTION_REFUSED_USERNAME_ALLREADY_USED]";
		break;
	case YOGUsernameBanned:
		key = "[YESTS_CONNECTION_REFUSED_USERNAME_BANNED]";
		break;
	case YOGIPAddressBanned:
		key = "[YESTS_CONNECTION_REFUSED_IP_TEMPORARILY_BANNED]";
		break;
	case YOGNameInvalidSpecialCharacters:
		key = "[YESTS_CONNECTION_REFUSED_USERNAME_INVALID_SPECIAL_CHARACTERS]";
		break;
	default:
		break;
	}
	setStatus(key);
}

void YOGConnectionScreen::onTimer(Uint32)
{
	if (connectionAttemptPending && !client->isConnecting())
	{
		if (!client->isConnected())
		{
			setStatus("[YESTS_UNABLE_TO_CONNECT]");
			setConnecting(false);
		}
		connectionAttemptPending = false;
	}
	client->update();
}
