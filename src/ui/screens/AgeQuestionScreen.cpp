// SPDX-License-Identifier: GPL-3.0-or-later
#include "AgeQuestionScreen.h"
#include "AgeGate.h"

void AgeQuestionScreen::answer(AgeGate::Band band)
{
	const AgeGate::Local updated = AgeGate::applyAnswer(AgeGate::local(), band);
	if (!AgeGate::store(updated))
	{
		saveFailed = true;
		invalidate();
		return;
	}
	endExecute(1);
}

Glob2UI::Element AgeQuestionScreen::build(const Glob2UI::Presentation &p)
{
	using namespace Glob2UI;
	std::vector<Element> body;
	body.push_back(paragraph(tr("[age question explanation]"), {FontRole::Body, false, TextAlign::Center}));
	body.push_back(hint(tr("[age question privacy]")));
	body.push_back(hint(tr("[age question not now]")));
	if (saveFailed) body.push_back(paragraph(tr("[age question save failed]")));
	// The two answers carry equal weight: neither is the default or primary.
	return page(tr("[How old are you?]"), scroll("age.body", column(std::move(body))),
		column({row({expanded(button("age.under13", tr("[Under 13]"), [this] { answer(AgeGate::Band::Under13); })),
					 expanded(button("age.13plus", tr("[13 or over]"), [this] { answer(AgeGate::Band::ThirteenPlus); }))},
					{p.pt(8)}),
			button("age.notnow", tr("[Not now]"), [this] { endExecute(0); }, {.flat = true})},
			{p.pt(8)}),
		p, 520);
}
