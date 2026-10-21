// SPDX-License-Identifier: GPL-3.0-or-later
// The game-event notification rows (GameEventFeed): an icon for the unit,
// building or team concerned, the message, and how many reports it stands for.

#include <FormatableString.h>
#include <SDL3/SDL.h>
#include <TouchText.h>
#include <RenderStateScope.h>
#include <cmath>
#include "GameGUITouch.h"
#include "InGameTouchTheme.h"

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
	auto *gfx = globalContainer->gfx;
	const bool mobile = touch->usesHUD();
	const auto area = mobile ? touch->notificationBounds() : GAGCore::ViewRect{};
	const double scale = mobile ? 1.15 * gfx->textUnitsPerPoint() : 1.;
	const double originX = mobile ? area.x : 0, originY = mobile ? std::max(area.y, double(y)) : 0;
	const int lineHeight = mobile ? std::max(EVENT_ROW_H_PX, font->getStringHeight("Ag") + 4) : EVENT_ROW_H_PX;
	SDL_Rect oldClip;
	gfx->getClipRect(&oldClip.x, &oldClip.y, &oldClip.w, &oldClip.h);
	std::unique_ptr<GAGCore::UITransformScope> transform;
	if (mobile)
	{
		x = y = 0;
		const SDL_Rect clip{int(area.x), int(area.y), int(area.w), int(area.h)};
		transform = std::make_unique<GAGCore::UITransformScope>(*gfx, scale, originX, originY, &clip);
	}

	for (const GameEventFeed::Row &row : eventFeed.rows())
	{
		const Uint8 alpha = static_cast<Uint8>(255.f * GameEventFeed::opacity(row, nowMs));
		if (!alpha)
			continue;
		std::string text = row.label;
		if (row.count > 1)
			text += FormattableString(" ×%0").arg(row.count);
		font->pushStyle(Font::Style(Font::STYLE_BOLD, row.color));
		const double textWidth = area.w / scale - EVENT_TEXT_X_PX - 4;
		auto lines = mobile ? GAGCore::wrapTouchText(font, text, textWidth)
			: std::vector<std::string>{text};
		if (mobile)
		{
			const int availableLines = int(std::floor(((area.y + area.h - originY) / scale - y) / lineHeight));
			if (textWidth <= 0 || availableLines < 1)
			{
				font->popStyle();
				break;
			}
			if (int(lines.size()) > availableLines)
			{
				lines.resize(availableLines);
				const auto tail = GAGCore::wrapTouchText(font, lines.back(), textWidth - font->getStringWidth("…"));
				lines.back() = (tail.empty() ? std::string{} : tail.front()) + "…";
			}
		}
		const int rowHeight = std::max(1, int(lines.size())) * lineHeight;
		font->popStyle();
		if (mobile)
		{
			auto background = InGameTouchTheme::field();
			background.a = alpha * 3 / 4;
			gfx->drawFilledRect(x, y, int(area.w / scale), rowHeight, background);
		}

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
		font->pushStyle(Font::Style(Font::STYLE_BOLD, row.color));
		int textW = 0;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			gfx->drawString(x + EVENT_TEXT_X_PX, y + int(i) * lineHeight, font, lines[i], 0, alpha);
			textW = std::max(textW, font->getStringWidth(lines[i]));
		}
		font->popStyle();

		eventFeedHits.push_back({int(originX + x * scale), int(originY + y * scale),
			int(std::ceil((EVENT_TEXT_X_PX + textW) * scale)), int(std::ceil(rowHeight * scale)), row.x, row.y});
		y += rowHeight;
	}
	transform.reset();
	gfx->setClipRect(oldClip.x, oldClip.y, oldClip.w, oldClip.h);
	return int(std::ceil(originY + y * scale));
}
