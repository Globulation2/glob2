// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ApplicationHost.h>
#include <functional>
#include <utility>

// Tells the web host when play in a local match stops and continues
// (ApplicationHost::MatchBreak::Paused and Resumed), so portal SDKs hear
// gameplayStop and gameplayStart around the in-game menu and pauses
// (docs/releases/web-portals.md#ad-breaks). Each edge is sent once:
// opening the menu while paused stays one stop, and only a return to play sends
// a start. Nothing is sent before started(), after stopped(), or for a relay
// match, which GameSessionScreen never starts here; the match's own end
// (MatchBreak::Ended) then stops gameplay without a second Paused.
class MatchPlayReporter
{
  public:
	using Send = std::function<bool(GAGCore::ApplicationHost::MatchBreak)>;
	explicit MatchPlayReporter(Send send = GAGCore::ApplicationHost::matchBreak) : send(std::move(send)) {}

	// The match started (after MatchBreak::Starting); play is running.
	void started()
	{
		active = true;
		interrupted = false;
	}
	// The match is over (Ended or Left reported): stop reporting.
	void stopped() { active = false; }
	// Whether play is currently stopped (the in-game menu or a pause).
	void update(bool stoppedNow)
	{
		if (!active || stoppedNow == interrupted)
			return;
		interrupted = stoppedNow;
		using GAGCore::ApplicationHost::MatchBreak;
		send(stoppedNow ? MatchBreak::Paused : MatchBreak::Resumed);
	}
	bool reporting() const { return active; }
	bool playStopped() const { return interrupted; }

  private:
	Send send;
	bool active = false, interrupted = false;
};
