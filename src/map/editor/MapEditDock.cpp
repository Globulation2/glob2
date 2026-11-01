// SPDX-License-Identifier: GPL-3.0-or-later
// MapEdit's side of the desktop/tablet dock (EditorDock.h): ownership, the width
// the map gives up, palette navigation and the map status strip.

#include "EditorDock.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "PhoneEditor.h"
#include "MapZoomControls.h"
#include <ui/PositionedControls.h>
#include "ui/FrontendUI.h"
#include <FormatableString.h>
#include <iterator>

void MapEdit::createDock()
{
	dock = std::make_unique<EditorDock>(*this);
	if (globalContainer && !globalContainer->runNoX && globalContainer->gfx)
	{
		dock->attach(*globalContainer->gfx);
		dock->markAttached();
	}
}

void MapEdit::destroyDock()
{
	dock.reset();
}

int MapEdit::dockWidth() const
{
	if (dock)
		return dock->width();
	return phone ? 0 : RIGHT_MENU_WIDTH;
}

void MapEdit::revealBrushGroup(BrushSection section, const std::string &group)
{
	if (!dock)
		return;
	dock->revealGroup(section, group);
}

bool MapEdit::pointerOverInterface() const
{
	if (hasDialog())
		return true;
	return dock && dock->contains(mouseX, mouseY);
}

// The map status strip: the cell under the pointer and, while it lasts, the
// latest showStatus() message above it, in the in-game theme's HUD colours.
void MapEdit::drawStatus()
{
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->littleFont;
	if (!gfx || !font)
		return;
	const auto &theme = Glob2UI::inGameTheme();
	const int pad = 5, gap = 4;
	// Beside the zoom controls (MapZoomControls.h: at most 144 pixels from x 8),
	// sharing their bottom margin; caption metrics determine their height.
	const int margin = phone ? 8 : 8 + 144 + 8;
	const int mapRight = gfx->getW() - dockWidth();
	int bottom = gfx->getH() - 4;
	auto pill = [&](const std::string &text, GAGCore::Color border)
	{
		const int w = std::min(font->getStringWidth(text.c_str()) + 2 * pad, std::max(0, mapRight - margin - 8));
		const int h = font->getStringHeight(text.c_str()) + 2 * pad;
		const int y = bottom - h;
		gfx->setClipRect(margin, y, w, h);
		gfx->drawFilledRect(margin, y, w, h, theme.hud.paper);
		gfx->drawRect(margin, y, w, h, border);
		gfx->drawString(margin + pad, y + pad, font, text.c_str());
		gfx->setClipRect();
		bottom = y - gap;
	};
	if (!coordinates.empty() && !phone)
		pill(coordinates, theme.hud.border);
	if (!statusText.empty() && SDL_GetTicks() < statusUntil)
		pill(statusText, theme.hud.ink);
}

std::vector<GAGGUI::ui::Host *> MapEdit::inspectionHosts()
{
	if (auto *dialog = activeDialog())
		return {&dialog->host()};
	if (phone)
		return {&phone->inspectionHost()};
	// A dock popup captures pointer/key input over the map as well.
	if (dock && dock->host().popupOpen())
		return {&dock->host()};
	if (!mapControlsHost)
		mapControlsHost = std::make_unique<GAGGUI::ui::Host>(Glob2UI::inGameTheme(),
			[this](const GAGGUI::ui::Presentation &p)
			{
				using namespace GAGGUI::ui;
				PositionedControl map;
				map.key = "editor/map";
				map.role = "canvas";
				map.label = "Map canvas";
				map.bounds = {0, 16, p.viewport.w - dockWidth(), std::max(0, p.viewport.h - 16)};
				map.actions.clear();
				std::vector<PositionedControl> controls{std::move(map)};
				auto zoom = mapZoomControls(camera, "editor/zoom", [this](int index)
				{
					const auto at = mapZoomControlBounds(camera)[index].center();
					clickMapZoomControls(camera, at.x, at.y);
					viewportX = camera.tileX();
					viewportY = camera.tileY();
				});
				controls.insert(controls.end(), std::make_move_iterator(zoom.begin()), std::make_move_iterator(zoom.end()));
				return positionedControls(std::move(controls));
			});
	mapControlsHost->setPresentation(GAGGUI::ui::resolvePresentation(*globalContainer->gfx));
	mapControlsHost->invalidate();
	mapControlsHost->layoutIfNeeded();
	std::vector<GAGGUI::ui::Host *> result{mapControlsHost.get()};
	if (dock) result.push_back(&dock->host());
	return result;
}
