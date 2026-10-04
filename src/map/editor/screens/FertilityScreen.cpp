// SPDX-License-Identifier: GPL-3.0-or-later
#include "FertilityScreen.h"

using namespace Glob2UI;

FertilityScreen::FertilityScreen(Map &map) : job(map) {}

Element FertilityScreen::build(const Presentation &p)
{
	return page("", column({paragraph(tr("[Computing Fertility]"), {FontRole::Body, false, TextAlign::Center}), progress(permille, 1000)}),
				actions({{"cancel", tr("[Cancel]"), [this] { endExecute(0); }, false, SDLK_ESCAPE}}, p), p, 480);
}

void FertilityScreen::onTimer(Uint32)
{
	if (!presented)
	{
		presented = true;
		return;
	}
	if (job.advance(65536))
	{
		job.commit();
		endExecute(1);
	}
	const int next = static_cast<int>(job.progress() * 1000);
	if (next != permille)
	{
		permille = next;
		invalidate();
	}
}
