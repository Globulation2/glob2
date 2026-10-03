// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "YOGClientEventListener.h"
#include "ui/FrontendUI.h"
#include <memory>

class YOGClient;

///Shared base for the YOG login and registration screens. Both take a
///not-yet-connected client and, each timer tick, poll its connection status,
///reporting a failed attempt through the status text and hiding the animation.
class YOGConnectionScreen : public Glob2UI::Screen, public YOGClientEventListener
{
  public:
	const char *recordingId() const override { return "yogconnection"; }
	///Construct with the given YOG client, which should not yet be connected.
	explicit YOGConnectionScreen(std::shared_ptr<YOGClient> client);
	~YOGConnectionScreen() override;

  protected:
	///Polls the client connection each tick, updating the status text and
	///animation when a connection attempt finishes.
	void onTimer(Uint32 tick) override;
	void setStatus(const std::string &key);
	void setConnecting(bool connecting);
	///Status text and the connecting animation, for the subclasses' pages.
	Glob2UI::Element statusRow(const Glob2UI::Presentation &presentation);
	///Translated refusal for a login-refused reason key.
	void reportRefusal(int reason);

	std::string status;
	bool connecting = false;
	bool connectionAttemptPending = false;
	std::shared_ptr<YOGClient> client;

  private:
	GAGCore::Sprite *earth = nullptr;
};
