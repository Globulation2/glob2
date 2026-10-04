// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault
#include "CampaignSelectorScreen.h"
#include "ui/FileListing.h"
#include <cassert>

using namespace Glob2UI;

CampaignSelectorScreen::CampaignSelectorScreen(bool isSelectingSave)
	: directory(isSelectingSave ? "games" : "campaigns")
{
	names = listFiles(directory, "txt");
}

void CampaignSelectorScreen::select(int index)
{
	selected = index;
	if (selected >= 0)
	{
		const auto caption = descriptionCache.getDescription(getCampaignName());
		description = caption.empty() ? "" : tr(caption);
	}
	else
		description.clear();
}

Element CampaignSelectorScreen::build(const Presentation &p)
{
	auto list = listView("campaigns", names, selected, [this](int i) { select(i); },
						 {{}, {}, {}, [this](int) { if (selected >= 0) endExecute(OK); }, {}, 10,
						  tr("[No items]")});
	auto details = scroll("description", paragraph(description));
	Element body = adaptive(
		[list, details](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(560))
				return column({expanded(list, 3), expanded(details, 2)});
			return row({expanded(list, 1), expanded(details, 1)}, {-1, CrossAlign::Stretch});
		});
	MenuAction ok{"ok", tr("[ok]"), [this] { if (selected >= 0) endExecute(OK); }, true, SDLK_RETURN, selected >= 0};
	MenuAction cancel{"cancel", tr("[Cancel]"), [this] { endExecute(CANCEL); }, false, SDLK_ESCAPE};
	return page(tr("[choose campaign]"), body, actions({ok, cancel}, p), p, 800);
}

std::string CampaignSelectorScreen::getCampaignName() const
{
	assert(selected >= 0 && selected < int(names.size()));
	return directory + "/" + names[std::size_t(selected)] + ".txt";
}
