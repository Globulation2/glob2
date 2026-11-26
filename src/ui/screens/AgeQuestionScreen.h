// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FrontendUI.h"

namespace AgeGate
{
enum class Band; // src/app/AgeGate.h
}

// The one-time age question, asked before LAN or online play while the age band
// is unknown (src/app/AgeGate.h). Only the answer (under 13, or 13 or over) is
// kept, never a birthdate. "Not now" leaves the band unknown, which keeps chat
// off; the question comes back next time. Ends with 1 when an answer was saved,
// 0 for "Not now".
class AgeQuestionScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "age_question"; }
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void answer(AgeGate::Band band);

  protected:
	void onEscape() override { endExecute(0); } // Escape is "Not now": chat stays off.

  private:
	bool saveFailed = false;
};
