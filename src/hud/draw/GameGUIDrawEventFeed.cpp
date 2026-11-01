// SPDX-License-Identifier: GPL-3.0-or-later
// The game-event notification rows (GameEventFeed): an icon for the unit,
// building or team concerned, the message, and how many reports it stands for.

#include <FormatableString.h>
#include <SDL3/SDL.h>
#include <TouchText.h>
#include <RenderStateScope.h>
#include <cmath>
#include <optional>
#include "GameGUITouch.h"
#include "InGameTouchTheme.h"

#include "GameGUI.h"
#include "GameGUIEventLayout.h"
#include "ConnectionOverlay.h"
#include <Toolkit.h>
#include "GameGUIInternal.h"
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
	globalContainer->gfx->drawSprite(x + (EVENT_ICON_PX - dw) / 2, y + (EVENT_ICON_PX - dh) / 2, dw,
									 dh, sprite, frame, alpha);
}
} // namespace

std::optional<int> GameGUI::eventFeedStartY()
{
	const auto connectionLines =
		connectionNotice && !connectionOverlay ? connectionNotice() : std::vector<std::string>{};
	if (!connectionLines.empty() ||
		(!connectionOverlay && clientEvents.waitingMask() && anyPlayerWaitedTimeFor > 2))
		return {};
	int y = 32, increment = 0;
	if (connectionOverlay)
	{
		const auto panel = connectionOverlay->panelBounds();
		if (panel.h > 0 && panel.x < 232)
			y = std::max(y, panel.y + panel.h + 12);
	}
	const auto &hud = drawnScene().panels.hud;
	const bool mobile = touch && touch->usesHUD();
	if (hud.state().legacyScriptTextShown && !mobile)
	{
		std::vector<std::string> lines;
		setMultiLine(hud.state().legacyScriptText, &lines);
		increment += int(lines.size()) * 20 + (swallowSpaceKey ? 20 : 0) + 8;
	}
	if (!scriptText.empty() && !mobile)
	{
		std::vector<std::string> lines;
		setMultiLine(scriptText, &lines);
		increment += int(lines.size()) * 20;
	}
	if (hud.state().legacyScriptTimer)
		increment = std::max(increment, 32);
	return y + increment + 2;
}

EventFeedLayout GameGUI::eventFeedLayout(int x, int y) const
{
	EventFeedLayout result;
	auto *gfx = globalContainer->gfx;
	Font *font = globalContainer->standardFont;
	result.mobile = touch && touch->usesHUD();
	result.area = result.mobile ? touch->notificationBounds() : GAGCore::ViewRect{};
	std::optional<InGameTouchTheme::TextStyle> mobileStyle;
	if (result.mobile)
		mobileStyle.emplace(font);
	const auto weight = result.mobile ? Font::STYLE_NORMAL : Font::STYLE_BOLD;
	result.scale = result.mobile ? .8 * gfx->textUnitsPerPoint() : 1.;
	result.originX = result.mobile ? result.area.x : 0;
	result.originY = result.mobile ? std::max(result.area.y, double(y)) : 0;
	result.lineHeight =
		result.mobile ? std::max(EVENT_ROW_H_PX, font->getStringHeight("Ag") + 4) : EVENT_ROW_H_PX;
	if (result.mobile)
		x = y = 0;
	const Uint64 now = SDL_GetTicks();
	for (const auto &event : eventFeed.rows())
	{
		EventFeedRowLayout row;
		row.event = event;
		row.alpha = static_cast<Uint8>(255.f * GameEventFeed::opacity(event, now));
		if (!row.alpha)
			continue;
		std::string text = event.label;
		if (event.count > 1)
			text += FormattableString(" ×%0").arg(event.count);
		font->pushStyle(Font::Style(weight, event.color));
		const double textWidth = result.area.w / result.scale - EVENT_TEXT_X_PX - 4;
		row.lines = result.mobile ? GAGCore::wrapTouchText(font, text, textWidth)
								  : std::vector<std::string>{text};
		if (result.mobile)
		{
			const double available =
				(result.area.y + result.area.h - result.originY) / result.scale - y;
			const int availableLines =
				int(std::floor((available - 8 + result.lineHeight - font->getStringHeight("Ag")) /
							   result.lineHeight));
			if (textWidth <= 0 || availableLines < 1 ||
				available < std::max(EVENT_ICON_PX, font->getStringHeight("Ag")) + 8)
			{
				font->popStyle();
				break;
			}
			if (int(row.lines.size()) > availableLines)
			{
				row.lines.resize(availableLines);
				const auto tail = GAGCore::wrapTouchText(font, row.lines.back(),
														 textWidth - font->getStringWidth("…"));
				row.lines.back() = (tail.empty() ? std::string{} : tail.front()) + "…";
			}
		}
		const int textHeight = font->getStringHeight("Ag") +
							   (std::max(1, int(row.lines.size())) - 1) * result.lineHeight;
		row.height = result.mobile ? std::max(EVENT_ICON_PX, textHeight) + 8 : EVENT_ROW_H_PX;
		row.iconY = result.mobile ? y + (row.height - EVENT_ICON_PX) / 2 : y;
		row.textY = result.mobile ? y + (row.height - textHeight) / 2 : y;
		int textWidthPixels = 0;
		for (const auto &line : row.lines)
			textWidthPixels = std::max(textWidthPixels, font->getStringWidth(line));
		font->popStyle();
		row.x = x;
		row.y = y;
		row.bounds = {int(result.originX + x * result.scale),
					  int(result.originY + y * result.scale),
					  int(std::ceil((EVENT_TEXT_X_PX + textWidthPixels) * result.scale)),
					  int(std::ceil(row.height * result.scale))};
		y += row.height;
		result.rows.push_back(std::move(row));
	}
	result.bottom = int(std::ceil(result.originY + y * result.scale));
	return result;
}

int GameGUI::drawEventFeed(int x, int y)
{
	eventFeedHits.clear();
	const auto layout = eventFeedLayout(x, y);
	const auto &teams = drawnScene().entities.teams;
	const auto ownColor = localTeamNo >= 0 && localTeamNo < int(teams.size())
							  ? presentationColor(teams[localTeamNo].color)
							  : GAGCore::Color();
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	std::optional<InGameTouchTheme::TextStyle> mobileStyle;
	if (layout.mobile)
		mobileStyle.emplace(font);
	const auto weight = layout.mobile ? Font::STYLE_NORMAL : Font::STYLE_BOLD;
	SDL_Rect oldClip;
	gfx->getClipRect(&oldClip.x, &oldClip.y, &oldClip.w, &oldClip.h);
	std::unique_ptr<GAGCore::UITransformScope> transform;
	if (layout.mobile)
	{
		const SDL_Rect clip{int(layout.area.x), int(layout.area.y), int(layout.area.w),
							int(layout.area.h)};
		transform = std::make_unique<GAGCore::UITransformScope>(*gfx, layout.scale, layout.originX,
																layout.originY, &clip);
	}
	for (const auto &entry : layout.rows)
	{
		const auto &row = entry.event;
		const int x = entry.x, y = entry.y, iconY = entry.iconY, textY = entry.textY;
		const auto alpha = entry.alpha;
		if (layout.mobile)
		{
			auto background = InGameTouchTheme::field();
			background.a = alpha * 3 / 4;
			gfx->drawFilledRect(x, y, int(layout.area.w / layout.scale), entry.height, background);
		}
		// Icon
		switch (row.type)
		{
		case GEUnitUnderAttack:
			if (row.subject < NB_UNIT_TYPE)
			{
				globalContainer->unitmini->setBaseColor(ownColor);
				drawFittedIcon(x, iconY, globalContainer->unitmini, row.subject, alpha);
				gfx->finishDrawingSprite(globalContainer->unitmini, alpha);
			}
			break;
		case GEUnitLostConversion:
		case GEUnitGainedConversion:
			// Identify the other team using the worker's normal team-color mask.
			globalContainer->unitmini->setBaseColor(
				row.subject < teams.size() ? presentationColor(teams[row.subject].color)
										   : ownColor);
			drawFittedIcon(x, iconY, globalContainer->unitmini, WORKER, alpha);
			gfx->finishDrawingSprite(globalContainer->unitmini, alpha);
			globalContainer->unitmini->setBaseColor(ownColor);
			break;
		case GEBuildingUnderAttack:
		case GEBuildingCompleted:
		{
			const BuildingType *type = row.subject < drawnScene().buildingTypes->size()
										   ? &drawnScene().buildingTypes->at(row.subject)
										   : nullptr;
			if (!type)
				break;
			const bool mini = type->miniSpriteImage >= 0;
			Sprite *sprite = mini ? type->miniSpritePtr : type->gameSpritePtr;
			const int frame = mini ? type->miniSpriteImage : type->gameSpriteImage;
			if (!sprite)
				break;
			sprite->setBaseColor(ownColor);
			drawFittedIcon(x, iconY, sprite, frame, alpha);
			globalContainer->gfx->finishDrawingSprite(sprite, alpha);
			break;
		}
		case GESize:
			break;
		}

		font->pushStyle(Font::Style(weight, row.color));
		for (size_t i = 0; i < entry.lines.size(); ++i)
			gfx->drawString(x + EVENT_TEXT_X_PX, textY + int(i) * layout.lineHeight, font,
							entry.lines[i], 0, alpha);
		font->popStyle();
		eventFeedHits.push_back(
			{entry.bounds.x, entry.bounds.y, entry.bounds.w, entry.bounds.h, row.x, row.y});
	}
	transform.reset();
	gfx->setClipRect(oldClip.x, oldClip.y, oldClip.w, oldClip.h);
	return layout.bottom;
}
