// SPDX-License-Identifier: GPL-3.0-or-later
// The game-event notification rows (GameEventFeed): an icon for the unit,
// building or team concerned, the message, and how many reports it stands for.

#include <FormatableString.h>
#include <SDL3/SDL.h>

#include "GameGUI.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "UnitConsts.h"

namespace
{
constexpr int EVENT_ROW_H_PX = 20;
constexpr int EVENT_ICON_PX = 18;
constexpr int EVENT_TEXT_X_PX = EVENT_ICON_PX + 6;

// Draw frame `frame` of `sprite` scaled to fit an EVENT_ICON_PX square at
// (x, y), keeping its aspect ratio.
void drawFittedIcon(int x, int y, Sprite *sprite, int frame, Uint8 alpha)
{
	const int w = sprite->getW(frame);
	const int h = sprite->getH(frame);
	if (w <= 0 || h <= 0)
		return;
	const float scale = std::min(1.f, static_cast<float>(EVENT_ICON_PX) / std::max(w, h));
	const int dw = std::max(1, static_cast<int>(w * scale));
	const int dh = std::max(1, static_cast<int>(h * scale));
	globalContainer->gfx->drawSprite(x + (EVENT_ICON_PX - dw) / 2, y + (EVENT_ICON_PX - dh) / 2, dw, dh, sprite,
									 frame, alpha);
}
} // namespace

int GameGUI::drawEventFeed(int x, int y)
{
	eventFeedHits.clear();
	const Uint64 nowMs = SDL_GetTicks();
	const auto &teams = drawnScene().entities.teams;
	const GAGCore::Color ownColor =
		localTeamNo >= 0 && localTeamNo < static_cast<int>(teams.size()) ? presentationColor(teams[localTeamNo].color) : GAGCore::Color();
	Font *font = globalContainer->standardFont;

	for (const GameEventFeed::Row &row : eventFeed.rows())
	{
		const Uint8 alpha = static_cast<Uint8>(255.f * GameEventFeed::opacity(row, nowMs));
		if (!alpha)
			continue;

		// Icon
		switch (row.type)
		{
		case GEUnitUnderAttack:
			if (row.subject < NB_UNIT_TYPE)
				drawFittedIcon(x, y, globalContainer->unitmini, row.subject, alpha);
			break;
		case GEUnitLostConversion:
		case GEUnitGainedConversion:
			// The other team's colour behind a worker.
			if (row.subject < teams.size())
			{
				const GAGCore::Color c = presentationColor(teams[row.subject].color);
				globalContainer->gfx->drawFilledRect(x, y, EVENT_ICON_PX, EVENT_ICON_PX, c.r, c.g, c.b,
													 static_cast<Uint8>(alpha * 3 / 4));
			}
			drawFittedIcon(x, y, globalContainer->unitmini, WORKER, alpha);
			break;
		case GEBuildingUnderAttack:
		case GEBuildingCompleted:
		{
			const BuildingType *type = row.subject < drawnScene().buildingTypes->size() ? &drawnScene().buildingTypes->at(row.subject) : nullptr;
			if (!type)
				break;
			const bool mini = type->miniSpriteImage >= 0;
			Sprite *sprite = mini ? type->miniSpritePtr : type->gameSpritePtr;
			const int frame = mini ? type->miniSpriteImage : type->gameSpriteImage;
			if (!sprite)
				break;
			sprite->setBaseColor(ownColor);
			drawFittedIcon(x, y, sprite, frame, alpha);
			globalContainer->gfx->finishDrawingSprite(sprite, alpha);
			break;
		}
		case GESize:
			break;
		}

		// Message, with the number of reports it stands for
		std::string text = row.label;
		if (row.count > 1)
			text += FormattableString(" ×%0").arg(row.count);
		font->pushStyle(Font::Style(Font::STYLE_BOLD, row.color));
		globalContainer->gfx->drawString(x + EVENT_TEXT_X_PX, y, font, text, 0, alpha);
		const int textW = font->getStringWidth(text.c_str());
		font->popStyle();

		eventFeedHits.push_back({x, y, EVENT_TEXT_X_PX + textW, EVENT_ROW_H_PX, row.x, row.y});
		y += EVENT_ROW_H_PX;
	}
	return y;
}
