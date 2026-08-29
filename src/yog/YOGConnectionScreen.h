// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include "Glob2Screen.h"
#include "YOGClientEventListener.h"

namespace GAGGUI
{
	class TextArea;
	class Animation;
}

class YOGClient;

///Shared base for the YOG login and registration screens. Both take a
///not-yet-connected client and, each timer tick, poll its connection status --
///reporting a failed attempt through the status text and hiding the animation.
class YOGConnectionScreen : public Glob2Screen, public YOGClientEventListener
{
public:
	///Construct with the given YOG client, which should not yet be connected.
	YOGConnectionScreen(std::shared_ptr<YOGClient> client);

protected:
	///Polls the client connection each tick, updating the status text and
	///animation when a connection attempt finishes.
	void onTimer(Uint32 tick);

	TextArea *statusText;
	Animation *animation;
	bool wasConnecting;
	bool changeTabAgain;
	std::shared_ptr<YOGClient> client;
};
