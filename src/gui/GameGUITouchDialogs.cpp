// SPDX-License-Identifier: GPL-3.0-or-later
#include <FormatableString.h>
#include "InGameTouchTheme.h"
#include "GameGUITouch.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "Order.h"
#include "GlobalContainer.h"
#include "Player.h"
#include <GUIButton.h>
#include <GUIList.h>
#include <GUISelector.h>
#include <GUIText.h>
#include <GUITextArea.h>
#include <GUITextInput.h>
#include <ResponsiveDialog.h>
#include "MobileSafeArea.h"
#include <Toolkit.h>
#include <StringTable.h>
#if defined(__IPHONEOS__)
#include "mobile/ios/SafeArea.h"
#endif
using namespace GAGCore;
using namespace GAGGUI;

OverlayScreen *GameGUITouch::activeDialog() const
{
	if (gui.gameMenuScreen)
		return gui.gameMenuScreen.get();
	if (gui.typingInputScreen)
		return gui.typingInputScreen;
	return gui.scrollableText;
}
void GameGUITouch::prepareDialog()
{
	auto *screen = activeDialog();
	if (dialogOwner != screen)
	{
		dialogOwner = screen;
		chatComposition.clear();
		dialogScroll = 0;
		editingDialogWidget = nullptr;
		SDL_StopTextInput();
	}
	dialogRows.clear();
	if (!screen)
		return;
	auto tr = [](const char *key)
	{ return std::string(Toolkit::getStringTable()->getString(key)); };
	auto *alliance = dynamic_cast<InGameAllianceScreen *>(screen);
	const bool objectives = dynamic_cast<InGameObjectivesScreen *>(screen);
	const bool main = dynamic_cast<InGameMainScreen *>(screen);
	const auto *outcome = dynamic_cast<InGameEndOfGameScreen *>(screen);
	const bool chat = screen == gui.typingInputScreen;
	if (alliance)
	{
		dialogRows.push_back({nullptr, tr("[Teams]")});
		if (gui.game.gameHeader.areAllyTeamsFixed())
			dialogRows.push_back(
				{nullptr, GAGCore::Toolkit::getStringTable()->getString(
							  "[Alliance and shared vision are fixed for this match.]")});
		int count = 0;
		const std::string labels[] = {
			GAGCore::Toolkit::getStringTable()->getString("[Alliance]"),
			GAGCore::Toolkit::getStringTable()->getString("[Share vision]"),
			GAGCore::Toolkit::getStringTable()->getString("[Share food vision]"),
			GAGCore::Toolkit::getStringTable()->getString("[Share market vision]"),
			GAGCore::Toolkit::getStringTable()->getString("[Chat]")};
		for (int i = 0; i < 16; ++i)
		{
			OnOffButton *choices[] = {alliance->alliance[i], alliance->normalVision[i],
									  alliance->foodVision[i], alliance->marketVision[i],
									  alliance->chat[i]};
			bool eligible = false;
			for (auto *choice : choices)
				eligible |= choice && choice->visible;
			if (!eligible)
				continue;
			++count;
			dialogRows.push_back(
				{nullptr, alliance->texts[i]
							  ? alliance->texts[i]->getText()
							  : GAGCore::FormattableString(
									GAGCore::Toolkit::getStringTable()->getString("[Player %0]"))
									.arg(i + 1)});
			dialogRows.back().team = gui.game.gameHeader.getBasePlayer(i).teamNumber;
			dialogRows.back().role = DialogRow::Role::Player;
			for (int j = 0; j < 5; ++j)
				if (choices[j] && choices[j]->visible)
					dialogRows.push_back({choices[j], labels[j], 2, 0, choices[j]->getState()});
		}
		if (!count)
			dialogRows.push_back(
				{nullptr,
				 GAGCore::Toolkit::getStringTable()->getString(
					 "[No other players have editable diplomatic settings in this match.]")});
		dialogRows.push_back({nullptr, tr("[ok]"), 104, 0, false, true});
	}
	else if (objectives)
	{
		const char *tabs[] = {"[briefing]", "[objectives]", "[hints]"};
		for (int i = 0; i < 3; ++i)
			dialogRows.push_back({nullptr, tr(tabs[i]), 105, i, objectivePage == i});
		if (objectivePage == 0)
			dialogRows.push_back({nullptr, gui.game.missionBriefing.empty()
											   ? GAGCore::Toolkit::getStringTable()->getString(
													 "[No briefing for this match.]")
											   : gui.game.missionBriefing});
		else if (objectivePage == 1)
		{
			for (auto group : {GameObjectives::Primary, GameObjectives::Secondary})
			{
				dialogRows.push_back(
					{nullptr, tr(group == GameObjectives::Primary ? "[Primary Objectives]"
																  : "[Secondary Objectives]")});
				int count = 0;
				dialogRows.back().role = DialogRow::Role::Section;
				for (int i = 0; i < gui.game.objectives.getNumberOfObjectives(); ++i)
				{
					auto &model = gui.game.objectives;
					if (!model.isObjectiveVisible(i) || model.getObjectiveType(i) != group)
						continue;
					dialogRows.push_back(
						{nullptr, (model.isObjectiveComplete(i) ? "[x] "
								   : model.isObjectiveFailed(i) ? "[!] "
																: "[ ] ") +
									  (model.getGameObjectiveText(i).starts_with("[")
										   ? tr(model.getGameObjectiveText(i).c_str())
										   : model.getGameObjectiveText(i))});
					++count;
				}
				if (!count)
					dialogRows.push_back({nullptr, tr("[No Objectives]")});
			}
		}
		else
		{
			// Hints remain owned by the scenario; expose their text without
			// inferring semantic ordering from desktop widget coordinates.
			for (int i = 0; i < gui.game.gameHints.getNumberOfHints(); ++i)
				if (gui.game.gameHints.isHintVisible(i))
					dialogRows.push_back({nullptr, gui.game.gameHints.getGameHintText(i)});
		}
		dialogRows.push_back({nullptr, tr("[ok]"), 104, 0, false, true});
	}
	else if (chat)
	{
		dialogRows.push_back({nullptr, GAGCore::Toolkit::getStringTable()->getString(
										   "[Chat · recipients selected in Teams]")});
		auto *input = gui.typingInputScreen->composerInput();
		dialogRows.push_back(
			{input, input->displayPreedit(chatComposition), 4, 0, input->isActivated()});
	}
	else if (outcome)
	{
		dialogRows.push_back({nullptr, outcome->title});
		if (outcome->canContinue)
			dialogRows.push_back(
				{nullptr, tr("[Continue playing]"), 106, InGameEndOfGameScreen::CONTINUE, true});
		dialogRows.push_back(
			{nullptr, tr("[Statistics]"), 106, InGameEndOfGameScreen::QUIT, false, true});
	}
	else if (main)
	{
		dialogRows.push_back({nullptr, tr("[Menu]")});
		dialogRows.push_back(
			{nullptr, tr("[return to game]"), 106, InGameMainScreen::RETURN_GAME, true, true});
		if (!globalContainer->isViewingGame())
		{
			dialogRows.push_back({nullptr, tr("[save game]"), 106, InGameMainScreen::SAVE_GAME});
		}
		dialogRows.push_back({nullptr,
							  tr(globalContainer->replaying ? "[load replay]" : "[load game]"), 106,
							  InGameMainScreen::LOAD_GAME});
		dialogRows.push_back({nullptr, tr("[Options]"), 106, InGameMainScreen::OPTIONS});
		dialogRows.push_back({nullptr, tr("[quit]"), 106, InGameMainScreen::QUIT_GAME});
	}
	// Compatibility adapter for unmigrated Options, Save/Load, and text entry.
	// Migrated in-game views above never inspect desktop widget geometry.
	screen->updateLayout();
	auto widgets = (alliance || objectives || main || chat || outcome)
					   ? std::vector<Widget *>{}
					   : screen->presentationWidgets();
	auto bounds = [](Widget *w)
	{
		auto *r = dynamic_cast<RectangularWidget *>(w);
		return r ? r->screenRectangle() : SDL_Rect{};
	};
	std::stable_sort(widgets.begin(), widgets.end(),
					 [&](Widget *a, Widget *b)
					 {
						 const auto x = bounds(a), y = bounds(b);
						 return x.y == y.y ? x.x < y.x : x.y < y.y;
					 });
	for (auto *widget : widgets)
	{
		if (!widget->visible)
			continue;
		const auto rect = bounds(widget);
		DialogRow row{widget, ""};
		if (auto *button = dynamic_cast<TextButton *>(widget))
		{
			row.text = button->caption();
			row.kind = 1;
			row.footer = rect.y >= screen->getH() - 60;
		}
		else if (auto *button = dynamic_cast<OnOffButton *>(widget))
		{
			row.kind = 2;
			row.selected = button->getState();
			row.text = Toolkit::getStringTable()->getString("[Mute]");
			if (alliance)
			{
				const char *keys[] = {
					"[abreaviation explanation A]", "[abreaviation explanation V]",
					"[abreaviation explanation fV]", "[abreaviation explanation mV]",
					"[abreaviation explanation C]"};
				for (int i = 0; i < 16; ++i)
				{
					OnOffButton *choices[] = {alliance->alliance[i], alliance->normalVision[i],
											  alliance->foodVision[i], alliance->marketVision[i],
											  alliance->chat[i]};
					for (int j = 0; j < 5; ++j)
						if (choices[j] == widget && alliance->texts[i])
							row.text = alliance->texts[i]->getText() + ": " +
									   Toolkit::getStringTable()->getString(keys[j]);
				}
			}
		}
		else if (auto *selector = dynamic_cast<Selector *>(widget))
		{
			row.kind = 3;
			row.text = std::to_string(selector->getValue());
		}
		else if (auto *input = dynamic_cast<TextInput *>(widget))
		{
			if (gui.inGameMenu == GameGUI::IGM_SAVE)
			{
				dialogRows.push_back(
					{nullptr, GAGCore::Toolkit::getStringTable()->getString("[Filename]")});
				dialogRows.back().role = DialogRow::Role::Section;
			}
			row.kind = 4;
			row.text = input->displayText();
			row.selected = input->isActivated();
#ifdef __EMSCRIPTEN__
			if (input->isActivated())
				editingDialogWidget = input;
#endif
			if (row.text.empty())
				row.text = "…";
		}
		else if (auto *list = dynamic_cast<List *>(widget))
		{
			for (size_t i = 0; i < list->getCount(); ++i)
				dialogRows.push_back(
					{widget, list->getText(i), 5, int(i), list->getSelectionIndex() == int(i)});
			continue;
		}
		else if (auto *text = dynamic_cast<Text *>(widget))
		{
			if (alliance)
				continue; // Each alliance toggle carries its full player/action label.
			row.text = text->getText();
			for (auto *other : widgets)
				if (other->visible && bounds(other).y == rect.y)
					if (auto *state = dynamic_cast<TriButton *>(other))
						row.text = (state->getState() == 1   ? "[x] "
									: state->getState() == 2 ? "[!] "
															 : "[ ] ") +
								   row.text;
		}
		else if (auto *text = dynamic_cast<TextArea *>(widget))
			row.text = text->getText();
		else
			continue;
		dialogRows.push_back(row);
	}
	if (gui.inGameMenu == GameGUI::IGM_SAVE)
		for (auto &row : dialogRows)
			if (row.kind == 1 && row.text == tr("[ok]"))
			{
				row.text = tr("[save game]");
				row.selected = true;
			}
	if ((main || alliance || chat || outcome) && !dialogRows.empty())
		dialogRows.front().role = DialogRow::Role::Title;
	if (gui.inGameMenu == GameGUI::IGM_MAIN)
	{
		dialogRows.push_back(
			{nullptr, Toolkit::getStringTable()->getString("[pause game]"), 100, 0});
	}
	if (gui.inGameMenu == GameGUI::IGM_OPTION)
		dialogRows.push_back({nullptr,
							  GAGCore::Toolkit::getStringTable()->getString("[Reduced motion]"),
							  100, 93, reducedMotion});
	if (gui.inGameMenu == GameGUI::IGM_OPTION)
		for (int i = 0; i < 3; ++i)
			dialogRows.push_back(
				{nullptr,
				 GAGCore::FormattableString(
					 Toolkit::getStringTable()->getString("[Dialog text size %0]"))
					 .arg(100 + i * 25),
				 100, 90 + i, globalContainer->settings.mobileDialogTextPercent == 100 + i * 25});
	if (gui.typingInputScreen)
	{
		dialogRows.push_back({nullptr, GAGCore::Toolkit::getStringTable()->getString("[Send]"), 101,
							  0, false, true});
		dialogRows.push_back({nullptr, GAGCore::Toolkit::getStringTable()->getString("[Close]"),
							  101, 1, false, true});
	}
	else if (gui.scrollableText)
		dialogRows.push_back(
			{nullptr, Toolkit::getStringTable()->getString("[ok]"), 102, 0, false, true});
	if (editingDialogWidget)
		dialogRows.push_back({nullptr, Toolkit::getStringTable()->getString("[Hide keyboard]"), 103,
							  0, false, true});
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	auto safe = mobileDialogSafe(globalContainer->gfx);
	const double width = std::min(safe.w - 16 * unit, 560 * unit);
	const double dialogHeight = std::min(safe.h - 16 * unit, gui.typingInputScreen ? 200 * unit
															 : outcome             ? 224 * unit
															 : main                ? 408 * unit
																				   : 560 * unit);
	safe.x += (safe.w - width) / 2;
	safe.y +=
		gui.typingInputScreen ? safe.h - dialogHeight - 8 * unit : (safe.h - dialogHeight) / 2;
	safe.w = width;
	safe.h = dialogHeight;
	dialogBounds = safe;
	const double textScale =
		InGameTouchTheme::textScale * globalContainer->settings.mobileDialogTextPercent / 100.0;
	const size_t pinnedCount = objectives ? 3 : 0;
	const auto header = safe;
	if (pinnedCount)
	{
		safe.y += 52 * unit;
		safe.h = std::max(0.0, safe.h - 52 * unit);
	}
	std::vector<bool> footer;
	for (size_t i = pinnedCount; i < dialogRows.size(); ++i)
		footer.push_back(dialogRows[i].footer);
	auto height = [&](size_t index, double width)
	{
		const auto &row = dialogRows[index + pinnedCount];
		if (outcome && index == 0)
			return 96 * unit;
		if (row.role == DialogRow::Role::Section)
			return 32 * unit * textScale;
		const double textWidth = std::max(unit, row.kind == 3 ? width - 96 * unit : width);
		return std::max(
			48 * unit,
			pointLines((row.kind == 2 ? (row.selected ? "[x] " : "[ ] ") : "") + row.text,
					   textWidth, textScale)
						.size() *
					16 * textScale * unit +
				8 * unit);
	};
	auto placement = ResponsiveDialog::calculate(safe, footer, height, dialogScroll * unit, unit);
	if (editingDialogWidget && lastDialogHeight != placement.content.h)
		for (size_t i = pinnedCount; i < dialogRows.size(); ++i)
			if (dialogRows[i].widget == editingDialogWidget)
			{
				const auto &rect = placement.rows[i - pinnedCount].rect;
				double offset =
					placement.offset +
					std::max(0.0, rect.y + rect.h - placement.content.y - placement.content.h);
				offset = std::min(offset, placement.offset + rect.y - placement.content.y);
				placement = ResponsiveDialog::calculate(safe, footer, height, offset, unit);
				break;
			}
	dialogContent = placement.content;
	lastDialogHeight = dialogContent.h;
	dialogScroll = placement.offset / unit;
	dialogMaximum = placement.maximum / unit;
	for (size_t i = 0; i < dialogRows.size(); ++i)
	{
		if (i < pinnedCount)
		{
			dialogRows[i].rect = {header.x + i * header.w / 3, header.y, header.w / 3 - 2 * unit,
								  48 * unit};
			dialogRows[i].footer = true;
		}
		else
		{
			dialogRows[i].rect = placement.rows[i - pinnedCount].rect;
			dialogRows[i].footer = placement.rows[i - pinnedCount].footer;
		}
	}
	if (alliance)
	{
		// Player cards own their layout. Keep a title and Close fixed while
		// settings form a touch-sized grid instead of a five-row button list.
		const double gap = 4 * unit, inset = 8 * unit;
		dialogRows.front().rect = {dialogBounds.x + inset, dialogBounds.y,
								   dialogBounds.w - 2 * inset, 40 * unit};
		dialogRows.front().footer = true;
		dialogRows.back().text = GAGCore::Toolkit::getStringTable()->getString("[Close]");
		dialogRows.back().rect = {dialogBounds.x + inset,
								  dialogBounds.y + dialogBounds.h - 56 * unit,
								  dialogBounds.w - 2 * inset, 48 * unit};
		dialogRows.back().footer = true;
		dialogContent = {dialogBounds.x + inset, dialogBounds.y + 40 * unit,
						 dialogBounds.w - 2 * inset, std::max(0.0, dialogBounds.h - 104 * unit)};
		const int columns = dialogContent.w >= 460 * unit ? 3 : 2;
		const double cell = (dialogContent.w - (columns - 1) * gap) / columns;
		double y = 0;
		for (size_t i = 1; i + 1 < dialogRows.size();)
		{
			auto &row = dialogRows[i];
			row.footer = false;
			if (row.kind != 2)
			{
				const double height =
					std::max(36 * unit, pointLines(row.text, dialogContent.w, textScale).size() *
												16 * textScale * unit +
											8 * unit);
				row.rect = {dialogContent.x, y, dialogContent.w, height};
				y += height + gap;
				++i;
				continue;
			}
			const size_t first = i;
			while (i + 1 < dialogRows.size() && dialogRows[i].kind == 2)
				++i;
			for (size_t n = first; n < i;)
			{
				const size_t end = std::min(i, n + columns);
				double height = 48 * unit;
				for (size_t k = n; k < end; ++k)
					height = std::max(
						height,
						pointLines(dialogRows[k].text, cell - 28 * unit, .9 * textScale).size() *
								16 * textScale * unit +
							8 * unit);
				for (size_t k = n; k < end; ++k)
				{
					dialogRows[k].footer = false;
					dialogRows[k].rect = {dialogContent.x + (k - n) * (cell + gap), y, cell,
										  height};
				}
				y += height + gap;
				n = end;
			}
			y += 8 * unit;
		}
		dialogMaximum = std::max(0.0, (y - dialogContent.h) / unit);
		dialogScroll = std::clamp(dialogScroll, 0.0, dialogMaximum);
		for (size_t i = 1; i + 1 < dialogRows.size(); ++i)
			dialogRows[i].rect.y += dialogContent.y - dialogScroll * unit;
	}
	if (gui.inGameMenu == GameGUI::IGM_SAVE)
	{
		// Filename and file actions stay together; only the saved-file list
		// scrolls. A short landscape viewport must not hide the active name.
		double footerTop = dialogBounds.y + dialogBounds.h;
		for (const auto &row : dialogRows)
			if (row.footer)
				footerTop = std::min(footerTop, row.rect.y);
		for (size_t i = 0; i < dialogRows.size(); ++i)
			if (dialogRows[i].kind == 4)
			{
				auto &input = dialogRows[i];
				input.footer = true;
				input.rect = {dialogContent.x, footerTop - 56 * unit, dialogContent.w, 48 * unit};
				if (i && dialogRows[i - 1].text ==
							 GAGCore::Toolkit::getStringTable()->getString("[Filename]"))
				{
					dialogRows[i - 1].footer = true;
					dialogRows[i - 1].rect = {dialogContent.x, footerTop - 80 * unit,
											  dialogContent.w, 24 * unit};
				}
				dialogContent.h = std::max(0.0, footerTop - 88 * unit - dialogContent.y);
				break;
			}
		double extent = 0;
		for (auto &row : dialogRows)
			if (!row.footer)
			{
				row.rect.y = extent;
				extent += row.rect.h + 8 * unit;
			}
		dialogMaximum = std::max(0.0, (extent - dialogContent.h) / unit);
		dialogScroll = std::clamp(dialogScroll, 0.0, dialogMaximum);
		for (auto &row : dialogRows)
			if (!row.footer)
				row.rect.y += dialogContent.y - dialogScroll * unit;
	}
}
bool GameGUITouch::drawDialog()
{
	dialogHUDDrawn = usesHUD() && activeDialog();
	if (!dialogHUDDrawn)
		return false;
	prepareDialog();
	auto *gfx = globalContainer->gfx;
	const double unit = gfx->logicalUnitsPerPoint();
	const double textScale =
		InGameTouchTheme::textScale * globalContainer->settings.mobileDialogTextPercent / 100.0;
	gfx->setClipRect();
	gfx->drawFilledRect(int(dialogBounds.x), int(dialogBounds.y), int(dialogBounds.w),
						int(dialogBounds.h), InGameTouchTheme::paper);
	gfx->drawRect(int(dialogBounds.x), int(dialogBounds.y), int(dialogBounds.w),
				  int(dialogBounds.h), InGameTouchTheme::border);
	if (dynamic_cast<InGameEndOfGameScreen *>(activeDialog()))
	{
		gfx->drawRect(int(dialogBounds.x), int(dialogBounds.y), int(dialogBounds.w),
					  int(dialogBounds.h), InGameTouchTheme::border);
		gfx->drawFilledRect(int(dialogBounds.x), int(dialogBounds.y), int(dialogBounds.w),
							int(6 * unit), gui.localTeam->color);
	}
	for (const auto &row : dialogRows)
	{
		labelClip = row.footer ? std::optional<ViewRect>{} : dialogContent;
		const auto r = row.rect;
		if (!row.footer &&
			(r.y + r.h <= dialogContent.y || r.y >= dialogContent.y + dialogContent.h))
			continue;
		SDL_Rect clip{int(dialogContent.x), int(dialogContent.y), int(dialogContent.w),
					  int(dialogContent.h)};
		if (auto *input = dynamic_cast<TextInput *>(row.widget))
			input->presentBrowserInput({int(r.x), int(r.y), int(r.w), int(r.h)}, gfx->getW(),
									   gfx->getH(), row.footer ? nullptr : &clip);
		if (!row.footer)
			gfx->setClipRect(clip.x, clip.y, clip.w, clip.h);
		else
			gfx->setClipRect();
		if (row.kind)
			gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h),
								row.selected ? InGameTouchTheme::selected
											 : InGameTouchTheme::field);
		if (row.kind == 4)
			gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), InGameTouchTheme::border);
		if (row.team >= 0 && row.team < gui.game.teamsCount())
			gfx->drawFilledRect(int(r.x), int(r.y), int(4 * unit), int(r.h),
								gui.game.teams[row.team]->color);
		if (row.kind == 2 && dynamic_cast<InGameAllianceScreen *>(activeDialog()))
		{
			const int side = int(14 * unit), x = int(r.x + 8 * unit),
					  y = int(r.y + (r.h - side) / 2);
			// Filled edges survive fractional UI scaling without missing sides.
			gfx->drawFilledRect(x, y, side, side, InGameTouchTheme::border);
			const int edge = std::max(1, int(2 * unit));
			gfx->drawFilledRect(x + edge, y + edge, side - 2 * edge, side - 2 * edge,
								row.selected ? InGameTouchTheme::selected
											 : InGameTouchTheme::field);
			if (row.selected)
				gfx->drawFilledRect(x + int(3 * unit), y + int(3 * unit), side - int(6 * unit),
									side - int(6 * unit), InGameTouchTheme::ink);
			drawPointLabel({r.x + 26 * unit, r.y, r.w - 26 * unit, r.h}, row.text, .9 * textScale,
						   true);
		}
		else if (row.kind == 3)
		{
			drawPointLabel({r.x, r.y, 48 * unit, r.h}, "−", textScale);
			drawPointLabel({r.x + 48 * unit, r.y, r.w - 96 * unit, r.h}, row.text, textScale);
			drawPointLabel({r.x + r.w - 48 * unit, r.y, 48 * unit, r.h}, "+", textScale);
		}
		else if (dynamic_cast<InGameEndOfGameScreen *>(activeDialog()) &&
				 &row == &dialogRows.front())
		{
			globalContainer->unitmini->setBaseColor(gui.localTeam->color);
			for (int i = 0; i < 3; ++i)
			{
				const double bob = gui.localTeam->hasWon && !reducedMotion &&
										   !(globalContainer->settings.optionFlags &
											 GlobalContainer::OPTION_LOW_SPEED_GFX)
									   ? std::sin(SDL_GetTicks64() / 220.0 + i) * 3 * unit
									   : 0;
				SDL_Rect artClip{int(r.x), int(r.y), int(r.w), int(r.h)};
				gfx->setUITransform(2 * unit, r.x + r.w / 2 + (i - 1) * 44 * unit - 16 * unit,
									r.y + 8 * unit + bob, &artClip);
				gfx->drawSprite(0, 0, globalContainer->unitmini, i);
				gfx->setUITransform();
				gfx->setClipRect();
			}
			drawPointLabel({r.x, r.y + 48 * unit, r.w, r.h - 48 * unit}, row.text, textScale * 1.8);
		}
		else
			drawPointLabel(r, (row.kind == 2 ? (row.selected ? "[x] " : "[ ] ") : "") + row.text,
						   textScale * (row.role == DialogRow::Role::Title ? 1.2 : 1.0),
						   !row.kind && row.role != DialogRow::Role::Title);
	}
	labelClip.reset();
	gfx->setClipRect();
	if (dialogMaximum > 0)
	{
		const double extent = dialogContent.h + dialogMaximum * unit;
		gfx->drawFilledRect(int(dialogContent.x + dialogContent.w - 3 * unit),
							int(dialogContent.y + dialogScroll * unit * dialogContent.h / extent),
							std::max(1, int(2 * unit)),
							int(dialogContent.h * dialogContent.h / extent), Color(170, 185, 190));
	}
	return true;
}
void GameGUITouch::tapDialog(ViewPoint point)
{
	prepareDialog();
	auto *screen = activeDialog();
	if (!screen)
		return;
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	for (const auto row : dialogRows)
	{
		if (row.widget != heldDialogWidget || row.index != heldDialogIndex ||
			row.kind != heldDialogKind || !row.rect.contains(point) ||
			(!row.footer && !dialogContent.contains(point)) || !row.kind)
			continue;
		if (row.kind == 100)
		{
			menuAction(row.index);
			return;
		}
		if (row.kind == 105)
		{
			objectivePage = row.index;
			dialogScroll = 0;
			return;
		}
		if (row.kind == 104 || row.kind == 106)
		{
			screen->endValue = row.kind == 104 ? (gui.inGameMenu == GameGUI::IGM_OBJECTIVES
													  ? InGameObjectivesScreen::OK
													  : InGameAllianceScreen::OK)
											   : row.index;
			SDL_Event event{};
			event.type = SDL_USEREVENT;
			gui.processGameMenu(&event);
			return;
		}
		if (row.kind == 103)
		{
			editingDialogWidget = nullptr;
			SDL_StopTextInput();
			return;
		}
		if (row.kind == 101)
		{
			if (row.index == 0 && !chatComposition.empty())
				return;
			chatComposition.clear();
			SDL_Event event{};
			event.type = SDL_KEYDOWN;
			event.key.keysym.sym = row.index ? SDLK_ESCAPE : SDLK_RETURN;
			gui.processTypingInput(&event);
			delete gui.typingInputScreen;
			gui.typingInputScreen = nullptr;
			dialogOwner = nullptr;
			SDL_StopTextInput();
			return;
		}
		if (row.kind == 102)
		{
			delete gui.scrollableText;
			gui.scrollableText = nullptr;
			dialogOwner = nullptr;
			return;
		}
		if (row.kind == 5)
		{
			auto *list = static_cast<List *>(row.widget);
			list->setSelectionIndex(row.index);
			list->selectionChanged();
		}
		else if (row.kind == 3)
		{
			auto *selector = static_cast<Selector *>(row.widget);
			const int delta = point.x < row.rect.x + 48 * unit                 ? -1
							  : point.x >= row.rect.x + row.rect.w - 48 * unit ? 1
																			   : 0;
			const int step = std::max(1, int(selector->maximumValue() / 16));
			selector->setValue(std::clamp(int(selector->getValue()) + delta * step, 0,
										  int(selector->maximumValue())));
			screen->onAction(selector, VALUE_CHANGED, selector->getValue(), 0);
		}
		else if (row.kind == 4)
		{
			for (auto *widget : screen->presentationWidgets())
				if (auto *input = dynamic_cast<TextInput *>(widget))
					input->deactivate();
			auto *input = static_cast<TextInput *>(row.widget);
			input->activate();
			input->setCursorPos(input->getText().size());
			editingDialogWidget = input;
			lastDialogHeight = -1;
			SDL_StartTextInput();
		}
		else if (row.kind == 2)
		{
			auto *toggle = static_cast<OnOffButton *>(row.widget);
			toggle->setState(!toggle->getState());
			screen->onAction(toggle, BUTTON_STATE_CHANGED, toggle->returnCode,
							 toggle->getState() ? 1 : 0);
			screen->onAction(toggle, BUTTON_RELEASED, toggle->returnCode, 0);
		}
		else
		{
			auto r = static_cast<RectangularWidget *>(row.widget)->screenRectangle();
			row.widget->activateAt(r.x + r.w / 2, r.y + r.h / 2);
		}
		SDL_Event event{};
		event.type = SDL_USEREVENT;
		if (gui.gameMenuScreen)
			gui.processGameMenu(&event);
		if (activeDialog() != screen)
		{
			SDL_StopTextInput();
			dialogOwner = nullptr;
		}
		return;
	}
}

void GameGUITouch::menuAction(int action)
{
	if (action == 93)
	{
		reducedMotion = !reducedMotion;
		return;
	}
	if (action == -2)
		return;
	if (action == -1)
	{
		showStatistics = false;
		panelScroll = 0;
		return;
	}
	if (action >= 90 && action <= 92)
	{
		globalContainer->settings.mobileDialogTextPercent = 100 + (action - 90) * 25;
		lastDialogHeight = 0;
		return;
	}
	gui.inGameMenu = GameGUI::IGM_NONE;
	gui.gameMenuScreen.reset();
	dialogOwner = nullptr;
	if (action >= 20 && action < 24)
	{
		bool *states[] = {&gui.showStarvingMap, &gui.showDamagedMap, &gui.showDefenseMap,
						  &gui.showFertilityMap};
		const bool enabled = !*states[action - 20];
		for (auto *state : states)
			*state = false;
		*states[action - 20] = enabled;
		const OverlayArea::OverlayType types[] = {OverlayArea::Starving, OverlayArea::Damage,
												  OverlayArea::Defence, OverlayArea::Fertility};
		gui.overlay.compute(gui.game, types[action - 20], gui.localTeamNo);
		return;
	}
	if (globalContainer->replaying && action >= 31 && action < 56)
	{
		if (action == 31)
			globalContainer->replayShowFog = !globalContainer->replayShowFog;
		else if (action == 32)
			globalContainer->replayVisibleTeams =
				globalContainer->replayVisibleTeams == 0xffffffff ? gui.localTeam->me : 0xffffffff;
		else if (action == 33)
			globalContainer->replayShowAreas = !globalContainer->replayShowAreas;
		else if (action == 34)
			globalContainer->replayShowFlags = !globalContainer->replayShowFlags;
		else if (action >= 40 && action - 40 < gui.game.teamsCount())
		{
			gui.clearSelection();
			gui.localTeamNo = action - 40;
			gui.adjustLocalTeam();
			for (int i = 0; i < gui.game.gameHeader.getNumberOfPlayers(); ++i)
				if (gui.game.players[i]->teamNumber == gui.localTeamNo)
				{
					gui.localPlayer = i;
					break;
				}
			if (globalContainer->replayVisibleTeams != 0xffffffff)
				globalContainer->replayVisibleTeams = gui.localTeam->me;
		}
		return;
	}
	switch (action)
	{
	case 0:
		if (globalContainer->replaying)
			gui.gamePaused = !gui.gamePaused;
		else if (!globalContainer->isViewingGame())
			gui.orderQueue.push_back(std::make_shared<PauseGameOrder>(!gui.gamePaused));
		break;
	case 1:
		gui.typingInputScreen = new InGameTextInput(globalContainer->gfx);
		gui.typingInputScreenInc = 0;
		gui.typingInputScreenPos = TYPING_INPUT_MAX_POS;
		prepareDialog();
		SDL_StartTextInput();
		break;
	case 2:
		panelOpen = true;
		gui.clearSelection();
		panelScroll = 0;
		break;
	case 3:
		showStatistics = true;
		panelOpen = true;
		gui.clearSelection();
		gui.replayDisplayMode = GameGUI::RDM_STAT_GRAPH_VIEW;
		gui.displayMode = GameGUI::STAT_GRAPH_VIEW;
		panelScroll = 0;
		break;
	case 4:
		gui.scrollableText = gui.messageManager.createScrollableHistoryScreen();
		break;
	case 5:
		gui.putMark = true;
		break;
	case 6:
		gui.drawHealthFoodBar = !gui.drawHealthFoodBar;
		break;
	case 30:
		globalContainer->replayFastForward = !globalContainer->replayFastForward;
		gui.gamePaused = false;
		break;
	}
}
