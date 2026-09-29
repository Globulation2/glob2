// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006-2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "FormatableString.h"
#include "Game.h"
#include "AINames.h"
#include "AllyTeamWidgetIndex.h"
#include "GameHeader.h"
#include "GlobalContainer.h"
#include "GUIButton.h"
#include "GUIText.h"
#include "GUITextInput.h"
#include "MapEditDialog.h"
#include "MapHeader.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "EditorTouchWidgets.h"
#include "MobileSafeArea.h"
#include <cmath>

MapEditMenuScreen::MapEditMenuScreen() : OverlayScreen(globalContainer->gfx, 370, 310)
{
	addWidget(new TextButton(0, 10, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu",
							 Toolkit::getStringTable()->getString("[load map]"), LOAD_MAP));
	addWidget(new TextButton(0, 60, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu",
							 Toolkit::getStringTable()->getString("[save map]"), SAVE_MAP));
	addWidget(new TextButton(0, 110, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu",
							 Toolkit::getStringTable()->getString("[open scenario editor]"),
							 OPEN_SCRIPT_EDITOR, 27));
	addWidget(new TextButton(0, 160, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu",
							 Toolkit::getStringTable()->getString("[open teams editor]"),
							 OPEN_TEAMS_EDITOR, 27));
	addWidget(new TextButton(0, 210, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu",
							 Toolkit::getStringTable()->getString("[quit the editor]"),
							 QUIT_EDITOR));
	addWidget(new TextButton(0, 260, 300, 40, ALIGN_CENTERED, ALIGN_LEFT, "menu",
							 Toolkit::getStringTable()->getString("[return to editor]"),
							 RETURN_EDITOR, 27));
	dispatchInit();
}

void MapEditMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
		endValue = par1;
}

AskForTextInput::AskForTextInput(const std::string &aLabel, const std::string &aCurrent)
	: OverlayScreen(globalContainer->gfx, 300, 120), labelText(aLabel), currentText(aCurrent)
{
	label = new Text(0, 5, ALIGN_FILL, ALIGN_LEFT, "menu",
					 Toolkit::getStringTable()->getString(labelText.c_str()));
	textEntry =
		new TextInput(10, 35, 280, 25, ALIGN_LEFT, ALIGN_LEFT, "standard", currentText, true);
	ok = new TextButton(10, 70, 135, 40, ALIGN_LEFT, ALIGN_LEFT, "menu",
						Toolkit::getStringTable()->getString("[ok]"), OK);
	cancel = new TextButton(155, 70, 135, 40, ALIGN_LEFT, ALIGN_LEFT, "menu",
							Toolkit::getStringTable()->getString("[Cancel]"), CANCEL);
	addWidget(label);
	addWidget(textEntry);
	addWidget(ok);
	addWidget(cancel);
	dispatchInit();
}

void AskForTextInput::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (par1 == OK)
		{
			currentText = textEntry->getText();
			endValue = OK;
		}
		else if (par1 == CANCEL)
		{
			endValue = CANCEL;
		}
	}
}

std::string AskForTextInput::getText()
{
	return currentText;
}

TeamsEditor::TeamsEditor(Game *game) : OverlayScreen(globalContainer->gfx, 500, 420), game(game)
{
	addWidget(new Text(0, 5, ALIGN_FILL, ALIGN_LEFT, "menu",
					   Toolkit::getStringTable()->getString("[teams editor]")));
	addWidget(new TextButton(155, 370, 135, 40, ALIGN_RIGHT, ALIGN_TOP, "menu",
							 Toolkit::getStringTable()->getString("[ok]"), OK));
	addWidget(new TextButton(10, 370, 135, 40, ALIGN_RIGHT, ALIGN_TOP, "menu",
							 Toolkit::getStringTable()->getString("[Cancel]"), CANCEL));

	GameHeader &gameHeader = game->gameHeader;
	MapHeader &mapHeader = game->mapHeader;

	for (int i = 0; i < Team::MAX_COUNT; ++i)
	{
		isPlayerActive[i] = new OnOffButton(10, 60 + i * 25, 21, 21, ALIGN_LEFT, ALIGN_TOP,
											gameHeader.getBasePlayer(i).type != BasePlayer::P_NONE,
											PLAYER_ACTIVE_BASE + i);
		addWidget(isPlayerActive[i]);
		if (i == 0)
		{
			isPlayerActive[i]->visible = false;
		}

		color[i] = new ColorButton(35, 60 + 25 * i, 21, 21, ALIGN_LEFT, ALIGN_TOP, COLOR_BASE + i);
		for (int j = 0; j < mapHeader.getNumberOfTeams(); j++)
			color[i]->addColor(mapHeader.getBaseTeam(j).color);
		color[i]->setSelectedColor(gameHeader.getBasePlayer(i).teamNumber);
		addWidget(color[i]);

		if (i == 0)
		{
			playerName[i] = new Text(60, 60 + 25 * i, ALIGN_LEFT, ALIGN_TOP, "standard",
									 Toolkit::getStringTable()->getString("[Human]"));
			aiSelector[i] = NULL;
			addWidget(playerName[i]);
		}
		else
		{
			playerName[i] = NULL;
			aiSelector[i] = new MultiTextButton(
				60, 60 + i * 25, 100, 21, ALIGN_LEFT, ALIGN_TOP, "standard",
				Toolkit::getStringTable()->getString("[AI]"), AI_SELECTOR_BASE + i);
			for (int aii = 0; aii < AI::SIZE; aii++)
				aiSelector[i]->addText(AINames::getAIText(aii));
			if (gameHeader.getBasePlayer(i).type >= BasePlayer::P_AI)
				aiSelector[i]->setIndex(gameHeader.getBasePlayer(i).type - BasePlayer::P_AI);
			else
				aiSelector[i]->setIndex(AI::NONE);
			addWidget(aiSelector[i]);
		}

		allyTeamNumbers[i] = new MultiTextButton(185, 60 + 25 * i, 21, 21, ALIGN_LEFT, ALIGN_TOP,
												 "standard", "", ALLY_TEAM_BASE + i);
		allyTeamNumbers[i]->clearTexts();
		for (int j = 0; j < mapHeader.getNumberOfTeams(); ++j)
		{
			std::stringstream s;
			s << j + 1;
			allyTeamNumbers[i]->addText(s.str());
		}
		allyTeamNumbers[i]->setIndex(allyTeamNumberToWidgetIndex(
			gameHeader.getAllyTeamNumber(gameHeader.getBasePlayer(i).teamNumber),
			mapHeader.getNumberOfTeams()));
		addWidget(allyTeamNumbers[i]);

		if (gameHeader.getBasePlayer(i).type == BasePlayer::P_NONE)
		{
			color[i]->visible = false;
			allyTeamNumbers[i]->visible = false;
			if (aiSelector[i])
				aiSelector[i]->visible = false;
			if (playerName[i])
				playerName[i]->visible = false;
		}
	}
	dispatchInit();
}

void TeamsEditor::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (par1 == OK)
		{
			generateGameHeader();
			endValue = OK;
		}
		else if (par1 == CANCEL)
		{
			endValue = CANCEL;
		}
		else if (par1 >= PLAYER_ACTIVE_BASE && par1 < COLOR_BASE)
		{
			int n = par1 - PLAYER_ACTIVE_BASE;
			color[n]->visible = isPlayerActive[n]->getState();
			aiSelector[n]->visible = isPlayerActive[n]->getState();
			allyTeamNumbers[n]->visible = isPlayerActive[n]->getState();
		}
	}
	if (action == BUTTON_PRESSED || action == BUTTON_SHORTCUT)
	{
		if (par1 >= COLOR_BASE && par1 < AI_SELECTOR_BASE)
		{
			int n = par1 - COLOR_BASE;
			for (int i = 0; i < Team::MAX_COUNT; ++i)
			{
				if (color[i]->getSelectedColor() == color[n]->getSelectedColor() && i != n)
				{
					allyTeamNumbers[n]->setIndex(allyTeamNumbers[i]->getIndex());
				}
			}
		}

		if (par1 >= ALLY_TEAM_BASE)
		{
			GameHeader &gameHeader = game->gameHeader;
			int team = -1;
			int nth = 0;
			///Find which team number this widget is for
			for (int i = 0; i < gameHeader.getNumberOfPlayers(); ++i)
			{
				if (allyTeamNumbers[i] == source)
				{
					team = color[i]->getSelectedColor();
					nth = allyTeamNumbers[i]->getIndex();
					break;
				}
			}
			///Adjust all widgets that have this team number
			for (int i = 0; i < gameHeader.getNumberOfPlayers(); ++i)
			{
				if (gameHeader.getBasePlayer(i).teamNumber == team)
				{
					allyTeamNumbers[i]->setIndex(nth);
				}
			}
		}
	}
}

void TeamsEditor::generateGameHeader()
{
	GameHeader gameHeader;
	int count = 0;
	for (int i = 0; i < Team::MAX_COUNT; i++)
	{
		if (isPlayerActive[i]->getState())
		{
			int teamColor = color[i]->getSelectedColor();
			if (i == 0)
			{
				gameHeader.getBasePlayer(count) =
					BasePlayer(0, Toolkit::getStringTable()->getString("[Human]"), teamColor,
							   BasePlayer::P_LOCAL);
			}
			else
			{
				AI::ImplementationID iid =
					static_cast<AI::ImplementationID>(aiSelector[i]->getIndex());
				FormattableString name("%0 %1");
				name.arg(AINames::getAIText(iid)).arg(i - 1);
				gameHeader.getBasePlayer(count) = BasePlayer(
					i, name.c_str(), teamColor, BasePlayer::playerTypeFromImplementationID(iid));
			}
			gameHeader.setAllyTeamNumber(teamColor, allyTeamNumbers[i]->getIndex() + 1);
			count += 1;
		}
		else
		{
			gameHeader.getBasePlayer(i) = BasePlayer();
		}
	}
	gameHeader.setNumberOfPlayers(count);
	game->setGameHeader(gameHeader);
}

std::string TeamsEditor::phoneLabel(Widget *widget) const
{
	for (int slot = 0; slot < Team::MAX_COUNT; ++slot)
	{
		const char *key = nullptr;
		if (widget == isPlayerActive[slot])
			key = "[Player slot]";
		else if (widget == color[slot])
			key = "[Team color]";
		else if (widget == allyTeamNumbers[slot])
			key = "[Alliance group]";
		else if (widget == aiSelector[slot])
			key = "[AI]";
		if (key)
			return GAGCore::FormattableString(Toolkit::getStringTable()->getString("[%0 %1]"))
				.arg(Toolkit::getStringTable()->getString(key))
				.arg(slot + 1);
	}
	return {};
}

namespace
{
// Normalize a single pointer only. The host owns multi-touch map navigation;
// overlays consume contacts and never forward them to the map behind them.
int editorDialogPointer(const SDL_Event &e, GAGCore::ViewPoint &p)
{
	auto *gfx = globalContainer->gfx;
	if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP)
	{
		p = {e.tfinger.x * gfx->getW(), e.tfinger.y * gfx->getH()};
		return e.type == SDL_FINGERDOWN ? 0 : e.type == SDL_FINGERUP ? 2 : 1;
	}
	if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)
	{
		if (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT)
			return -1;
		p = {double(e.button.x), double(e.button.y)};
		return e.type == SDL_MOUSEBUTTONDOWN ? 0 : 2;
	}
	if (e.type == SDL_MOUSEMOTION && e.motion.which != SDL_TOUCH_MOUSEID)
	{
		p = {double(e.motion.x), double(e.motion.y)};
		return 1;
	}
	return -1;
}
} // namespace

void MapEditMenuScreen::prepareTouch()
{
	const double u = globalContainer->gfx->logicalUnitsPerPoint();
	const auto safe = GAGCore::mobileDialogSafe(globalContainer->gfx);
	const int columns = safe.w >= 560 * u ? 2 : 1;
	const double w = std::min(safe.w - 24 * u, columns * 300 * u), row = 48 * u, gap = 8 * u;
	const double h = (6 / columns) * (row + gap) - gap;
	const double x = safe.x + (safe.w - w) / 2, y = safe.y + (safe.h - h) / 2;
	touchControls.clear();
	const int actions[] = {RETURN_EDITOR,      SAVE_MAP,          LOAD_MAP,
						   OPEN_SCRIPT_EDITOR, OPEN_TEAMS_EDITOR, QUIT_EDITOR};
	for (int i = 0; i < 6; ++i)
		touchControls.push_back(
			{{x + (i % columns) * (w + gap) / columns, y + (i / columns) * (row + gap),
			  (w - gap * (columns - 1)) / columns, row},
			 actions[i]});
}
void MapEditMenuScreen::drawTouch()
{
	prepareTouch();
	const auto first = touchControls.front().first, last = touchControls.back().first;
	EditorTouch::panel(
		globalContainer->gfx,
		{first.x - 8, first.y - 8, last.x + last.w - first.x + 16, last.y + last.h - first.y + 16});
	const char *keys[] = {"[load map]",          "[save map]",         "[open scenario editor]",
						  "[open teams editor]", "[return to editor]", "[quit the editor]"};
	for (const auto &c : touchControls)
		EditorTouch::button(globalContainer->gfx, c.first,
							Toolkit::getStringTable()->getString(keys[c.second]));
}
bool MapEditMenuScreen::eventTouch(SDL_Event event)
{
	prepareTouch();
	GAGCore::ViewPoint p;
	const int phase = editorDialogPointer(event, p);
	if (phase == 0)
	{
		touchHeld = -1;
		for (const auto &c : touchControls)
			if (c.first.contains(p))
				touchHeld = c.second;
	}
	if (phase == 2)
	{
		for (const auto &c : touchControls)
			if (c.second == touchHeld && c.first.contains(p))
				onAction(nullptr, BUTTON_RELEASED, c.second, 0);
		touchHeld = -1;
	}
	if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
		endValue = RETURN_EDITOR;
	return true;
}

void TeamsEditor::prepareTouch()
{
	const double u = globalContainer->gfx->logicalUnitsPerPoint();
	const auto safe = GAGCore::mobileDialogSafe(globalContainer->gfx);
	const double w = std::min(safe.w - 16 * u, 760 * u), h = std::min(safe.h - 16 * u, 710 * u);
	touchBounds = {safe.x + (safe.w - w) / 2, safe.y + (safe.h - h) / 2, w, h};
	touchRows = {touchBounds.x + 8 * u, touchBounds.y + 80 * u, w - 24 * u, h - 144 * u};
	const double stride = 52 * u;
	touchMaximum = std::max(0., Team::MAX_COUNT * stride - touchRows.h);
	touchOffset = std::clamp(touchOffset, 0., touchMaximum);
	touchCells.clear();
	for (int i = 0; i < Team::MAX_COUNT; ++i)
	{
		const double y = touchRows.y + i * stride - touchOffset;
		const double slot = 48 * u, swatch = 48 * u, group = 52 * u,
					 player = touchRows.w - slot - swatch - group - 12 * u;
		double x = touchRows.x;
		touchCells.push_back({{x, y, slot, 48 * u}, isPlayerActive[i], std::to_string(i + 1), i});
		x += slot + 4 * u;
		touchCells.push_back({{x, y, swatch, 48 * u}, color[i], "", i});
		x += swatch + 4 * u;
		touchCells.push_back(
			{{x, y, player, 48 * u},
			 aiSelector[i],
			 i == 0 ? Toolkit::getStringTable()->getString("[Human]") : aiSelector[i]->caption(),
			 i});
		x += player + 4 * u;
		touchCells.push_back(
			{{x, y, group, 48 * u}, allyTeamNumbers[i], allyTeamNumbers[i]->caption(), i});
	}
}
void TeamsEditor::drawTouch()
{
	prepareTouch();
	auto *context = globalContainer->gfx;
	const double u = context->logicalUnitsPerPoint();
	EditorTouch::panel(context, touchBounds);
	EditorTouch::label(
		context, {touchBounds.x + 8 * u, touchBounds.y + 4 * u, touchBounds.w - 152 * u, 36 * u},
		Toolkit::getStringTable()->getString("[teams editor]"));
	const int first = std::min(Team::MAX_COUNT, int(std::ceil(touchOffset / (52 * u))) + 1);
	const int last =
		std::clamp(int(std::floor((touchOffset + touchRows.h - 48 * u) / (52 * u))) + 1, first,
				   Team::MAX_COUNT);
	EditorTouch::label(
		context, {touchBounds.x + touchBounds.w - 142 * u, touchBounds.y + 4 * u, 134 * u, 36 * u},
		std::to_string(first) + "–" + std::to_string(last) + " / " +
			std::to_string(Team::MAX_COUNT));
	if (touchMaximum > 0)
	{
		const double thumb =
			std::max(20 * u, touchRows.h * touchRows.h / (touchRows.h + touchMaximum));
		context->drawFilledRect(int(touchRows.x + touchRows.w + 3 * u), int(touchRows.y),
								std::max(1, int(3 * u)), int(touchRows.h), InGameTouchTheme::field);
		context->drawFilledRect(
			int(touchRows.x + touchRows.w + 3 * u),
			int(touchRows.y + (touchRows.h - thumb) * touchOffset / touchMaximum),
			std::max(1, int(3 * u)), int(thumb), InGameTouchTheme::border);
	}
	const std::string headings[] = {Toolkit::getStringTable()->getString("[Slot]"),
									Toolkit::getStringTable()->getString("[Color]"),
									Toolkit::getStringTable()->getString("[Player / AI]"),
									Toolkit::getStringTable()->getString("[Group]")};
	for (int i = 0; i < 4; ++i)
	{
		auto r = touchCells[i].bounds;
		r.y = touchBounds.y + 42 * u;
		r.h = 32 * u;
		EditorTouch::label(context, r, headings[i]);
	}
	for (const auto &c : touchCells)
	{
		if (c.bounds.y < touchRows.y || c.bounds.y + c.bounds.h > touchRows.y + touchRows.h)
			continue;
		const bool active = isPlayerActive[c.slot]->getState();
		if (c.widget == color[c.slot] && active)
		{
			EditorTouch::button(context, c.bounds, "");
			const auto selected =
				game->mapHeader.getBaseTeam(color[c.slot]->getSelectedColor()).color;
			context->drawFilledRect(int(c.bounds.x + 8 * u), int(c.bounds.y + 8 * u),
									int(c.bounds.w - 16 * u), int(c.bounds.h - 16 * u), selected);
		}
		else if (c.widget == isPlayerActive[c.slot] || active)
			EditorTouch::button(context, c.bounds, c.caption,
								c.widget == isPlayerActive[c.slot] && active);
		else
			EditorTouch::label(context, c.bounds,
							   c.widget == aiSelector[c.slot]
								   ? Toolkit::getStringTable()->getString("[Inactive]")
								   : "—");
		if (c.widget == isPlayerActive[c.slot])
		{
			const int x = int(c.bounds.x + 29 * u), y = int(c.bounds.y + 17 * u),
					  side = int(14 * u);
			// A one-logical-pixel outline disappears when a legacy viewport
			// is reduced to phone size. Rasterize the checkbox in point-sized
			// strokes, including the check, instead of relying on font glyphs.
			const int edge = std::max(1, int(2 * u));
			context->drawFilledRect(x, y, side, side, InGameTouchTheme::ink);
			context->drawFilledRect(x + edge, y + edge, side - 2 * edge, side - 2 * edge,
									InGameTouchTheme::field);
			if (active)
			{
				auto dot = [&](int dx, int dy)
				{
					context->drawFilledRect(x + int(dx * u), y + int(dy * u), edge, edge,
											InGameTouchTheme::ink);
				};
				for (int step = 0; step < 5; ++step)
					dot(2 + step, 6 + step);
				for (int step = 0; step < 8; ++step)
					dot(5 + step, 10 - step);
			}
		}
	}
	const double y = touchBounds.y + touchBounds.h - 56 * u, w = (touchBounds.w - 24 * u) / 2;
	EditorTouch::button(context, {touchBounds.x + 8 * u, y, w, 48 * u},
						Toolkit::getStringTable()->getString("[Cancel]"));
	EditorTouch::button(context, {touchBounds.x + 16 * u + w, y, w, 48 * u},
						Toolkit::getStringTable()->getString("[ok]"));
}
bool TeamsEditor::eventTouch(SDL_Event event)
{
	prepareTouch();
	const double u = globalContainer->gfx->logicalUnitsPerPoint();
	if (event.type == SDL_MOUSEWHEEL)
	{
		touchOffset -= event.wheel.y * 52 * u;
		return true;
	}
	if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
	{
		endValue = CANCEL;
		return true;
	}
	GAGCore::ViewPoint p;
	const int phase = editorDialogPointer(event, p);
	const double footer = touchBounds.y + touchBounds.h - 56 * u;
	if (phase == 0)
	{
		touchDown = p;
		touchMoved = false;
		touchHeld = -1;
		if (p.y >= footer && touchBounds.contains(p))
			touchHeld = p.x < touchBounds.x + touchBounds.w / 2 ? -2 : -3;
		else if (touchRows.contains(p))
			for (size_t i = 0; i < touchCells.size(); ++i)
				if (touchCells[i].bounds.contains(p))
					touchHeld = i;
	}
	else if (phase == 1 && touchHeld >= 0)
	{
		if (std::abs(p.y - touchDown.y) > 8 * u)
		{
			touchMoved = true;
			touchOffset += touchDown.y - p.y;
			touchDown = p;
		}
	}
	else if (phase == 2)
	{
		if (!touchMoved)
		{
			if (touchHeld == -2 || touchHeld == -3)
			{
				if (p.y >= footer && touchBounds.contains(p))
					onAction(nullptr, BUTTON_RELEASED, touchHeld == -2 ? CANCEL : OK, 0);
			}
			else if (touchHeld >= 0 && touchHeld < int(touchCells.size()))
			{
				const auto c = touchCells[touchHeld];
				if (c.bounds.contains(p) && touchRows.contains(p) && c.widget &&
					!(c.slot == 0 && c.widget == isPlayerActive[0]) &&
					(c.widget == isPlayerActive[c.slot] || isPlayerActive[c.slot]->getState()))
				{
					auto *rect = dynamic_cast<RectangularWidget *>(c.widget);
					const auto r = rect->getScreenRect();
					c.widget->activateAt(r.x + r.w / 2, r.y + r.h / 2);
				}
			}
		}
		touchHeld = -1;
	}
	return true;
}

// A compact child dialog owns its bounds and actions; the retained TextInput
// continues to own UTF-8 editing. Provisional IME text never commits an area name.
void AskForTextInput::prepareTouch()
{
	prepareTouch(GAGCore::mobileDialogSafe(globalContainer->gfx));
}
void AskForTextInput::prepareTouch(GAGCore::ViewRect safe)
{
	const double u = globalContainer->gfx->logicalUnitsPerPoint();
	const bool compact = safe.h < 172 * u, horizontal = safe.h < 116 * u;
	const double w = std::max(1., std::min(480 * u, safe.w - 16 * u));
	const double h = std::min(safe.h, (horizontal ? 60 : compact ? 108 : 156) * u);
	touchBounds = {safe.x + (safe.w - w) / 2, safe.y + (safe.h - h) / 2, w, h};
	if (horizontal)
	{
		const double row = std::min(44 * u, h), y = touchBounds.y + (h - row) / 2;
		const double action = std::min(88 * u, w / 4);
		touchInput = {touchBounds.x + 4 * u, y, std::max(1., w - 2 * action - 16 * u), row};
		touchCancel = {touchInput.x + touchInput.w + 4 * u, y, action, row};
		touchConfirm = {touchCancel.x + action + 4 * u, y, action, row};
		return;
	}
	touchInput = {touchBounds.x + 12 * u, touchBounds.y + (compact ? 8 : 48) * u, w - 24 * u,
				  44 * u};
	touchCancel = {touchInput.x, touchBounds.y + (compact ? 56 : 104) * u, (w - 32 * u) / 2,
				   44 * u};
	touchConfirm = {touchCancel.x + touchCancel.w + 8 * u, touchCancel.y, touchCancel.w, 44 * u};
}
void AskForTextInput::cancelTouch()
{
	touchHeld = -1;
	touchFinger = -1;
	touchPreedit.clear();
}
void AskForTextInput::drawTouch()
{
	drawTouchInViewport(GAGCore::mobileDialogSafe(globalContainer->gfx));
}
void AskForTextInput::drawTouchInViewport(GAGCore::ViewRect safe)
{
	prepareTouch(safe);
	auto *gfx = globalContainer->gfx;
	const double u = gfx->logicalUnitsPerPoint();
	EditorTouch::panel(gfx, touchBounds);
	if (touchInput.y - touchBounds.y >= 40 * u)
		EditorTouch::label(gfx, {touchInput.x, touchBounds.y + 8 * u, touchInput.w, 32 * u},
						   Toolkit::getStringTable()->getString(labelText.c_str()));
	EditorTouch::button(gfx, touchInput, "");
	auto *font = Toolkit::getFont("standard");
	InGameTouchTheme::TextStyle ink(font);
	const auto text = textEntry->displayPreedit(touchPreedit);
	const auto cursor = std::min(text.size(), textEntry->cursorPosition() + touchPreedit.size());
	size_t start = 0;
	const int available = std::max(1, int(touchInput.w / u) - 16);
	while (start < cursor &&
		   font->getStringWidth(text.substr(start, cursor - start)) > available - 8)
	{
		++start;
		while (start < cursor && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80)
			++start;
	}
	SDL_Rect clip{int(touchInput.x), int(touchInput.y), int(touchInput.w), int(touchInput.h)};
	gfx->setUITransform(u, touchInput.x + 8 * u, touchInput.y + 12 * u, &clip);
	gfx->drawString(0, 0, font, text.substr(start), available);
	const int caret = font->getStringWidth(text.substr(start, cursor - start));
	gfx->drawLine(caret, 0, caret, font->getStringHeight("Ag"), InGameTouchTheme::ink);
	gfx->setUITransform();
	gfx->setClipRect();
	textEntry->presentBrowserInput(clip, gfx->getW(), gfx->getH());
	SDL_SetTextInputRect(&clip);
	EditorTouch::button(gfx, touchCancel, Toolkit::getStringTable()->getString("[Cancel]"));
	EditorTouch::button(gfx, touchConfirm, Toolkit::getStringTable()->getString("[ok]"), true);
}
bool AskForTextInput::eventTouch(SDL_Event event)
{
	prepareTouch();
	if (event.type == SDL_WINDOWEVENT)
	{
		cancelTouch();
		return true;
	}
	if (event.type == SDL_TEXTEDITING)
	{
		touchPreedit = event.edit.text;
		return true;
	}
	if (event.type == SDL_TEXTINPUT)
	{
		touchPreedit.clear();
		dispatchEvents(&event);
		return true;
	}
	if (event.type == SDL_KEYDOWN)
	{
		const auto key = event.key.keysym.sym;
		if (key == SDLK_ESCAPE && !touchPreedit.empty())
		{
			touchPreedit.clear();
			return true;
		}
		if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER)
		{
			if (touchPreedit.empty())
			{
				onAction(nullptr, BUTTON_RELEASED, key == SDLK_ESCAPE ? CANCEL : OK, 0);
				SDL_StopTextInput();
			}
			return true;
		}
		dispatchEvents(&event);
		return true;
	}
	GAGCore::ViewPoint p;
	const int phase = editorDialogPointer(event, p);
	const int target = touchInput.contains(p)     ? 2
					   : touchCancel.contains(p)  ? CANCEL
					   : touchConfirm.contains(p) ? OK
												  : -1;
	if (event.type == SDL_FINGERDOWN && touchFinger != -1)
	{
		touchHeld = -1;
		return true;
	}
	if (phase == 0)
	{
		touchHeld = target;
		touchFinger = event.type == SDL_FINGERDOWN ? event.tfinger.fingerId : -1;
	}
	if (phase == 2)
	{
		if (event.type == SDL_FINGERUP && event.tfinger.fingerId != touchFinger)
			return true;
		if (target == touchHeld && target >= 0)
		{
			if (target == 2)
			{
				textEntry->activate();
				SDL_StartTextInput();
			}
			else if (target == CANCEL || touchPreedit.empty())
			{
				onAction(nullptr, BUTTON_RELEASED, target, 0);
				SDL_StopTextInput();
			}
		}
		touchHeld = -1;
		touchFinger = -1;
	}
	return true;
}
