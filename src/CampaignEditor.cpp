// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "CampaignEditor.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "ChooseMapScreen.h"
#include "GlobalContainer.h"
#include <set>
#include <algorithm>
#include "GUICheckList.h"
#include "gui/MobileSafeArea.h"
#include <TouchText.h>

namespace
{
// Keep a swipe inside a list from selecting a row or toggling a prerequisite.
// Existing mouse hit testing runs only after a completed one-finger tap.
template <class Base> class CampaignTouchList : public Base
{
	bool held = false, moved = false;
	SDL_FingerID owner = 0;
	int lastY = 0;

  public:
	using Base::Base;
	bool touchEvent(const SDL_Event &event, int width, int height)
	{
		if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
											  event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
			held = false;
		if (!this->visible || (event.type != SDL_FINGERDOWN && event.type != SDL_FINGERMOTION &&
							   event.type != SDL_FINGERUP))
			return false;
		int x, y, w, h;
		this->getScreenPos(&x, &y, &w, &h);
		const int px = event.tfinger.x * width, py = event.tfinger.y * height;
		const bool inside = px >= x && px < x + w && py >= y && py < y + h;
		if (event.type == SDL_FINGERDOWN)
		{
			if (held)
			{
				moved = true;
				return true;
			}
			if (!inside)
				return false;
			held = true;
			moved = false;
			owner = event.tfinger.fingerId;
			lastY = py;
			return true;
		}
		if (!held)
			return false;
		if (event.tfinger.fingerId != owner)
			return true;
		if (event.type == SDL_FINGERMOTION)
		{
			const int row = std::max(1, int(this->textHeight));
			if (std::abs(py - lastY) >= row)
			{
				moved = true;
				this->disp =
					std::clamp(int(this->disp) + (lastY - py) / row, 0,
							   std::max(0, int(this->strings.size()) - std::max(1, (h - 8) / row)));
				lastY = py;
			}
			return true;
		}
		held = false;
		if (!moved && inside)
		{
			SDL_Event mouse{};
			mouse.type = SDL_MOUSEBUTTONDOWN;
			mouse.button.button = SDL_BUTTON_LEFT;
			mouse.button.x = px;
			mouse.button.y = py;
			this->onSDLMouseButtonDown(&mouse);
			mouse.type = SDL_MOUSEBUTTONUP;
			this->onSDLMouseButtonUp(&mouse);
		}
		return true;
	}
};
// TextArea caches wrapping and visible-line count. Reflow only when its bounds
// change, retaining the draft, cursor and scroll state during responsive layout.
class CampaignTextArea : public TextArea
{
	std::string preedit;
	bool held = false, scrolling = false;
	float lastY = 0;
	SDL_FingerID finger = 0;

  public:
	using TextArea::TextArea;
	bool editing() const { return activated; }
	// Touch ownership is local to the canvas: swiping scrolls without moving
	// the insertion point or opening a keyboard. Composition is never draft text.
	bool touchEvent(const SDL_Event &event, int screenW, int screenH)
	{
		if (readOnly || !visible)
			return false;
		if (event.type == SDL_TEXTEDITING && activated)
		{
			preedit = event.edit.text;
			return true;
		}
		if (event.type == SDL_TEXTINPUT)
			preedit.clear();
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && activated)
		{
			deactivate();
			preedit.clear();
			SDL_StopTextInput();
			return true;
		}
		if (event.type == SDL_KEYDOWN && activated && !preedit.empty())
			return true;
		if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
											  event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
		{
			held = false;
			if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
				preedit.clear();
		}
		if (event.type != SDL_FINGERDOWN && event.type != SDL_FINGERMOTION &&
			event.type != SDL_FINGERUP)
			return false;
		int bx, by, bw, bh;
		getScreenPos(&bx, &by, &bw, &bh);
		const int px = event.tfinger.x * screenW, py = event.tfinger.y * screenH;
		const bool inside = px >= bx && px < bx + bw && py >= by && py < by + bh;
		if (event.type == SDL_FINGERDOWN)
		{
			if (held)
			{
				scrolling = true;
				return true;
			}
			if (!inside)
				return false;
			held = true;
			scrolling = false;
			lastY = py;
			finger = event.tfinger.fingerId;
			return true;
		}
		if (!held)
			return false;
		if (event.tfinger.fingerId != finger)
			return true;
		if (event.type == SDL_FINGERMOTION)
		{
			const int delta = int(lastY - py);
			if (std::abs(delta) >= int(charHeight))
			{
				scrolling = true;
				areaPos = std::clamp(int(areaPos) + delta / int(charHeight), 0,
									 std::max(0, int(lines.size()) - int(areaHeight)));
				lastY = py;
			}
			return true;
		}
		held = false;
		if (!scrolling && inside)
		{
			const size_t row = std::min(
				lines.size() - 1, areaPos + size_t(std::max(0, (py - by - 4) / int(charHeight))));
			size_t at = lines[row], end = row + 1 < lines.size() ? lines[row + 1] : text.size();
			while (at < end && text[at] != '\n')
			{
				const size_t next = GAGGUI::getNextUTF8Char(text, at);
				if (font->getStringWidth(text.substr(lines[row], next - lines[row]).c_str()) >
					px - bx - 4)
					break;
				at = next;
			}
			setCursorPos(at);
			activate();
			notifyCursorMoved();
			parent->onAction(this, TEXT_ACTIVATED, 0, 0);
			SDL_Rect rect{bx, by, bw, bh};
			SDL_SetTextInputRect(&rect);
			SDL_StartTextInput();
		}
		return true;
	}
	void paint() override
	{
		TextArea::paint();
		if (!activated || preedit.empty() || cursorPosY < areaPos ||
			cursorPosY >= areaPos + areaHeight)
			return;
		int bx, by, bw, bh;
		getScreenPos(&bx, &by, &bw, &bh);
		auto *surface = parent->getSurface();
		surface->setClipRect(bx, by, bw, bh);
		surface->drawString(int(bx + 4 + cursorScreenPosY),
							int(by + 4 + (cursorPosY - areaPos) * charHeight), font,
							preedit.c_str(), bw - 8 - cursorScreenPosY);
		surface->setClipRect();
	}
	void fit(int x, int y, int width, int height)
	{
		const bool changed = w != width || h != height;
		setScreenRectangle(x, y, width, height);
		areaHeight = std::max(1, (height - 8) / int(charHeight));
		if (changed)
		{
			layout();
			compute();
		}
	}
};
void fitArea(TextArea *area, int x, int y, int w, int h)
{
	static_cast<CampaignTextArea *>(area)->fit(x, y, w, h);
}
} // namespace

CampaignEditor::CampaignEditor(const std::string &name, GAGGUI::ScreenStack &screens)
	: screens(screens)
{
	if (name != "" && !campaign.load(name))
		campaign.setName(name);
	StringTable &table = *Toolkit::getStringTable();
	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu",
					 table.getString("[campaign editor]"));
	mapList = new CampaignTouchList<List>(10, 50, 300, 300, ALIGN_SCREEN_CENTERED,
										  ALIGN_SCREEN_CENTERED, "standard");
	addMap = new TextButton(10, 360, 145, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
							table.getString("[add map]"), ADDMAP);
	editMap = new TextButton(165, 360, 145, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
							 "menu", table.getString("[edit map]"), EDITMAP);
	removeMap = new TextButton(10, 410, 145, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
							   "menu", table.getString("[remove map]"), REMOVEMAP);
	nameEditor = new TextInput(320, 60, 310, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
							   "standard", campaign.getName());
	ok = new TextButton(260, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
						table.getString("[ok]"), OK);
	cancel = new TextButton(450, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
							table.getString("[Cancel]"), CANCEL);
	description =
		new CampaignTextArea(320, 90, 310, 225, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
							 "standard", false, campaign.getDescription().c_str());
	addWidget(title);
	addWidget(mapList);
	addWidget(addMap);
	addWidget(editMap);
	addWidget(removeMap);
	addWidget(nameEditor);
	addWidget(ok);
	addWidget(cancel);
	addWidget(description);
	saveStatus = new CampaignTextArea(320, 330, 310, 80, ALIGN_SCREEN_CENTERED,
									  ALIGN_SCREEN_CENTERED, "standard", true, "");
	addWidget(saveStatus);
	detailsTab = new TextButton(0, 0, 100, 44, ALIGN_LEFT, ALIGN_TOP, "standard", "Details", 100);
	mapsTab = new TextButton(0, 0, 100, 44, ALIGN_LEFT, ALIGN_TOP, "standard", "Maps", 101);
	nameLabel = new Text(0, 0, ALIGN_LEFT, ALIGN_TOP, "standard", "Campaign name");
	descriptionLabel = new Text(0, 0, ALIGN_LEFT, ALIGN_TOP, "standard", "Description");
	addWidget(detailsTab);
	addWidget(mapsTab);
	addWidget(nameLabel);
	addWidget(descriptionLabel);
	syncMapList();
}

void CampaignEditor::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED || action == BUTTON_SHORTCUT) &&
		(source == detailsTab || source == mapsTab))
	{
		description->deactivate();
		nameEditor->deactivate();
		SDL_StopTextInput();
		mobilePage = source == mapsTab;
		return;
	}
	if (persistence)
		return;
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (source == ok)
		{
			saveCampaign();
		}
		else if (source == cancel)
		{
			endExecute(CANCEL);
		}
		else if (source == addMap)
		{
			screens.push(std::make_unique<ChooseMapScreen>("campaigns", "map", false),
						 [this](Screen &screen, int result)
						 {
							 if (result != ChooseMapScreen::OK)
								 return;
							 const auto name =
								 static_cast<ChooseMapScreen &>(screen).getMapHeader().getMapName();
							 auto draft = std::make_shared<CampaignMapEntry>(
								 name, glob2NameToFilename("campaigns", name, "map"));
							 screens.push(
								 std::make_unique<CampaignMapEntryEditor>(campaign, *draft),
								 [this, draft](Screen &, int result)
								 {
									 if (result == CampaignMapEntryEditor::OK)
									 {
										 campaign.appendMap(*draft);
										 mapList->addText(draft->getMapName());
									 }
								 });
						 });
		}
		else if (source == editMap)
		{
			auto selected = mapList->selection();
			if (selected)
			{
				for (unsigned i = 0; i < campaign.getMapCount(); ++i)
				{
					if (campaign.getMap(i).getMapName() == mapList->get())
					{
						screens.push(
							std::make_unique<CampaignMapEntryEditor>(campaign, campaign.getMap(i)),
							[this, i, index = *selected](Screen &, int result)
							{
								if (result == CampaignMapEntryEditor::OK)
									mapList->setText(index, campaign.getMap(i).getMapName());
							});
						break;
					}
				}
			}
		}
		else if (source == removeMap)
		{
			auto sel = mapList->selection();
			if (sel)
			{
				for (unsigned i = 0; i < campaign.getMapCount(); ++i)
				{
					std::vector<std::string>::iterator iter =
						std::find(campaign.getMap(i).getUnlockedByMaps().begin(),
								  campaign.getMap(i).getUnlockedByMaps().end(), mapList->get());
					if (iter != campaign.getMap(i).getUnlockedByMaps().end())
					{
						campaign.getMap(i).getUnlockedByMaps().erase(iter);
					}
				}
				campaign.removeMap(*sel);
				mapList->removeText(*sel);
			}
		}
	}
	else if (action == TEXT_ACTIVATED)
	{
		if (source == nameEditor)
			description->deactivate();
		else if (source == description)
			nameEditor->deactivate();
	}
	else if (action == TEXT_MODIFIED)
	{
		if (source == nameEditor)
		{
			campaign.setName(nameEditor->getText());
		}
		if (source == description)
		{
			campaign.setDescription(description->getText());
		}
	}
}

void CampaignEditor::saveFailed()
{
	persistence.reset();
	for (Widget *widget : std::initializer_list<Widget *>{ok, cancel, addMap, editMap, removeMap,
														  nameEditor, description, mapList})
		widget->visible = true;
	saveStatus->setText(Toolkit::getStringTable()->getString("[campaign editor save failed]"));
}

void CampaignEditor::saveCampaign()
{
	try
	{
		if (GAGCore::ApplicationHost::storageRestoreFailed() || !campaign.save())
		{
			saveFailed();
			return;
		}
		persistence = GAGCore::ApplicationHost::persistStorage();
		if (!persistence)
		{
			saveFailed();
			return;
		}
		for (Widget *widget : std::initializer_list<Widget *>{
				 ok, cancel, addMap, editMap, removeMap, nameEditor, description, mapList})
			widget->visible = false;
		saveStatus->setText(Toolkit::getStringTable()->getString("[saving to storage]"));
	}
	catch (const std::exception &)
	{
		saveFailed();
	}
}

void CampaignEditor::onTimer(Uint32 tick)
{
	Glob2Screen::onTimer(tick);
	if (!persistence)
		return;
	const auto state = persistence->state();
	if (state == GAGCore::ApplicationHost::PersistenceState::Failed)
		saveFailed();
	else if (state == GAGCore::ApplicationHost::PersistenceState::Succeeded)
	{
		persistence.reset();
		endExecute(OK);
	}
}

void CampaignEditor::syncMapList()
{
	for (unsigned n = 0; n < campaign.getMapCount(); n++)
	{
		mapList->addText(campaign.getMap(n).getMapName());
	}
}

CampaignMapEntryEditor::CampaignMapEntryEditor(Campaign &campaign, CampaignMapEntry &mapEntry)
	: entry(mapEntry), campaign(campaign)
{
	StringTable &table = *Toolkit::getStringTable();
	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu",
					 table.getString("[editing map]"));
	mapsUnlockedBy = new CampaignTouchList<CheckList>(10, 80, 150, 300, ALIGN_SCREEN_CENTERED,
													  ALIGN_SCREEN_CENTERED, "standard", false);
	mapsUnlockedByLabel = new Text(10, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard",
								   table.getString("[unlocked by]"));
	nameEditorLabel = new Text(405, 80, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard",
							   table.getString("[map name]"));
	nameEditor = new TextInput(420, 105, 180, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
							   "standard", entry.getMapName());
	isUnlockedLabel = new Text(430, 140, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard",
							   table.getString("[unlocked at start]"));
	isUnlocked = new OnOffButton(405, 140, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
								 entry.isUnlocked(), ISUNLOCKED);
	descriptionEditorLabel = new Text(405, 170, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
									  "standard", table.getString("[map description]"));
	descriptionEditor =
		new CampaignTextArea(420, 195, 180, 225, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
							 "standard", false, entry.getDescription().c_str());
	ok = new TextButton(260, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
						table.getString("[ok]"), OK);
	cancel = new TextButton(450, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
							table.getString("[Cancel]"), CANCEL);

	std::set<std::string> unlockedBy;
	for (unsigned n = 0; n < entry.getUnlockedByMaps().size(); ++n)
	{
		unlockedBy.insert(entry.getUnlockedByMaps()[n]);
	}
	for (unsigned n = 0; n < campaign.getMapCount(); ++n)
	{
		if (campaign.getMap(n).getMapName() != entry.getMapName())
		{
			if (unlockedBy.find(campaign.getMap(n).getMapName()) == unlockedBy.end())
			{
				mapsUnlockedBy->addItem(campaign.getMap(n).getMapName(), false);
			}
			else
			{
				mapsUnlockedBy->addItem(campaign.getMap(n).getMapName(), true);
			}
		}
	}
	addWidget(title);
	addWidget(mapsUnlockedBy);
	addWidget(mapsUnlockedByLabel);
	addWidget(nameEditorLabel);
	addWidget(nameEditor);
	addWidget(isUnlockedLabel);
	addWidget(isUnlocked);
	addWidget(descriptionEditorLabel);
	addWidget(descriptionEditor);
	addWidget(ok);
	addWidget(cancel);
	detailsTab = new TextButton(0, 0, 100, 44, ALIGN_LEFT, ALIGN_TOP, "standard", "Details", 100);
	unlockTab = new TextButton(0, 0, 100, 44, ALIGN_LEFT, ALIGN_TOP, "standard", "Unlocking", 101);
	addWidget(detailsTab);
	addWidget(unlockTab);
}

void CampaignMapEntryEditor::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED || action == BUTTON_SHORTCUT) &&
		(source == detailsTab || source == unlockTab))
	{
		descriptionEditor->deactivate();
		nameEditor->deactivate();
		SDL_StopTextInput();
		mobilePage = source == unlockTab;
		return;
	}
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (source == ok)
		{
			///If the maps name was changes, make sure to change it in all of the other map entries
			for (unsigned i = 0; i < campaign.getMapCount(); ++i)
			{
				std::vector<std::string>::iterator iter =
					std::find(campaign.getMap(i).getUnlockedByMaps().begin(),
							  campaign.getMap(i).getUnlockedByMaps().end(), entry.getMapName());
				if (iter != campaign.getMap(i).getUnlockedByMaps().end())
				{
					(*iter) = nameEditor->getText();
				}
			}
			entry.setMapName(nameEditor->getText());
			entry.setDescription(descriptionEditor->getText());
			entry.getUnlockedByMaps().clear();
			for (unsigned i = 0; i < mapsUnlockedBy->getCount(); ++i)
			{
				if (mapsUnlockedBy->isChecked(i))
				{
					entry.getUnlockedByMaps().push_back(mapsUnlockedBy->getText(i));
				}
			}

			if (!isUnlocked->getState())
				entry.lockMap();
			else
				entry.unlockMap();
			endExecute(OK);
		}
		else if (source == cancel)
		{
			endExecute(CANCEL);
		}
	}
	else if (action == TEXT_ACTIVATED)
	{
		if (source == nameEditor)
			descriptionEditor->deactivate();
		else if (source == descriptionEditor)
			nameEditor->deactivate();
	}
}

namespace
{
// Native editing widgets keep their cursor, composition and persistence logic;
// only presentation bounds and pointer adaptation differ from desktop.
void adaptEditorPointer(SDL_Event &event, int w, int h)
{
	if (event.type != SDL_FINGERDOWN && event.type != SDL_FINGERUP &&
		event.type != SDL_FINGERMOTION)
		return;
	const auto finger = event.tfinger;
	const auto kind = event.type;
	event = {};
	if (kind == SDL_FINGERMOTION)
	{
		event.type = SDL_MOUSEMOTION;
		event.motion.x = int(finger.x * w);
		event.motion.y = int(finger.y * h);
		event.motion.state = SDL_BUTTON_LMASK;
	}
	else
	{
		event.type = kind == SDL_FINGERDOWN ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
		event.button.button = SDL_BUTTON_LEFT;
		event.button.x = int(finger.x * w);
		event.button.y = int(finger.y * h);
	}
}
struct CampaignFrame
{
	int x, y, w, h, footer;
	bool wide;
	explicit CampaignFrame(GraphicContext *gfx)
	{
		auto safe = mobileDialogSafe(gfx);
		w = std::min(940, int(safe.w) - 32);
		h = std::min(660, int(safe.h) - 24);
		x = int(safe.x) + (int(safe.w) - w) / 2;
		y = int(safe.y) + (int(safe.h) - h) / 2;
		footer = y + h - 48;
		wide = w >= 700;
	}
};
} // namespace
void CampaignEditor::handleExecutionEvent(SDL_Event event)
{
	if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
		event.button.which == SDL_TOUCH_MOUSEID)
		return;
	if (static_cast<CampaignTouchList<List> *>(mapList)->touchEvent(event, getW(), getH()))
		return;
	if (static_cast<CampaignTextArea *>(description)->touchEvent(event, getW(), getH()))
		return;
	adaptEditorPointer(event, getW(), getH());
	Screen::handleExecutionEvent(event);
}
void CampaignMapEntryEditor::handleExecutionEvent(SDL_Event event)
{
	if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
		event.button.which == SDL_TOUCH_MOUSEID)
		return;
	if (static_cast<CampaignTouchList<CheckList> *>(mapsUnlockedBy)
			->touchEvent(event, getW(), getH()))
		return;
	if (static_cast<CampaignTextArea *>(descriptionEditor)->touchEvent(event, getW(), getH()))
		return;
	adaptEditorPointer(event, getW(), getH());
	Screen::handleExecutionEvent(event);
}
void CampaignEditor::paint()
{
	mapList->setMinimumRowHeight(phonePresentationRequested() ? 44 : 0);
	CampaignFrame f(globalContainer->gfx);
	title->visible = true;
	title->setScreenRectangle(f.x, f.y, f.w, 28);
	const bool statusVisible = persistence || !saveStatus->getText().empty();
	const int tabY = f.y + 34, statusTop = tabY + (f.wide ? 0 : 52),
			  body = statusTop + (statusVisible ? 56 : 0), col = f.wide ? (f.w - 20) / 2 : f.w;
	detailsTab->visible = mapsTab->visible = !f.wide;
	detailsTab->setScreenRectangle(f.x, tabY, f.w / 2 - 4, 44);
	mapsTab->setScreenRectangle(f.x + f.w / 2 + 4, tabY, f.w / 2 - 4, 44);
	const bool details = f.wide || mobilePage == 0, maps = f.wide || mobilePage == 1;
	nameLabel->visible = nameEditor->visible = descriptionLabel->visible = description->visible =
		details && !persistence;
	mapList->visible = addMap->visible = editMap->visible = removeMap->visible =
		maps && !persistence;
	nameLabel->setScreenRectangle(f.x, body, col, 22);
	nameEditor->setScreenRectangle(f.x, body + 24, col, 40);
	descriptionLabel->setScreenRectangle(f.x, body + 72, col, 22);
	fitArea(description, f.x, body + 98, col, std::max(40, f.footer - body - 110));
	const int mx = f.wide ? f.x + col + 20 : f.x;
	mapList->setScreenRectangle(mx, body, col, std::max(40, f.footer - body - 64));
	const int buttonW = (col - 12) / 3;
	addMap->setScreenRectangle(mx, f.footer - 56, buttonW, 44);
	editMap->setScreenRectangle(mx + buttonW + 6, f.footer - 56, buttonW, 44);
	removeMap->setScreenRectangle(mx + 2 * (buttonW + 6), f.footer - 56, buttonW, 44);
	ok->setScreenRectangle(f.x + f.w / 2 + 4, f.footer, f.w / 2 - 4, 48);
	cancel->setScreenRectangle(f.x, f.footer, f.w / 2 - 4, 48);
	saveStatus->visible = statusVisible;
	fitArea(saveStatus, f.x, statusTop, f.w, 48);
	// With a landscape keyboard, devote the remaining viewport to the active
	// field. Footer actions stay reachable and never cover the draft.
	if (mobileKeyboardInset(globalContainer->gfx) > 0 && f.h < 360)
	{
		for (auto *widget : widgets)
			widget->visible = false;
		ok->visible = cancel->visible = true;
		if (nameEditor->isActivated())
		{
			nameEditor->visible = true;
			nameEditor->setScreenRectangle(f.x, f.y, f.w,
										   std::min(40, std::max(24, f.footer - f.y - 8)));
		}
		else
		{
			description->visible = true;
			fitArea(description, f.x, f.y, f.w, std::max(24, f.footer - f.y - 8));
		}
	}
	SDL_Rect bounds{f.x, f.y, f.w, f.h};
	if (FrontendTheme::current)
		FrontendTheme::current->background(gfx, true, &bounds);
}
void CampaignMapEntryEditor::paint()
{
	mapsUnlockedBy->setMinimumRowHeight(phonePresentationRequested() ? 44 : 0);
	CampaignFrame f(globalContainer->gfx);
	title->visible = true;
	title->setScreenRectangle(f.x, f.y, f.w, 28);
	const int tabY = f.y + 34, body = tabY + (f.wide ? 0 : 52), col = f.wide ? (f.w - 20) / 2 : f.w;
	detailsTab->visible = unlockTab->visible = !f.wide;
	detailsTab->setScreenRectangle(f.x, tabY, f.w / 2 - 4, 44);
	unlockTab->setScreenRectangle(f.x + f.w / 2 + 4, tabY, f.w / 2 - 4, 44);
	const bool details = f.wide || mobilePage == 0, unlock = f.wide || mobilePage == 1;
	nameEditorLabel->visible = nameEditor->visible = descriptionEditorLabel->visible =
		descriptionEditor->visible = details;
	isUnlockedLabel->visible = isUnlocked->visible = mapsUnlockedByLabel->visible =
		mapsUnlockedBy->visible = unlock;
	nameEditorLabel->setScreenRectangle(f.x, body, col, 22);
	nameEditor->setScreenRectangle(f.x, body + 24, col, 40);
	descriptionEditorLabel->setScreenRectangle(f.x, body + 72, col, 22);
	fitArea(descriptionEditor, f.x, body + 98, col, std::max(40, f.footer - body - 110));
	const int ux = f.wide ? f.x + col + 20 : f.x;
	isUnlocked->setScreenRectangle(ux, body, 40, 40);
	isUnlockedLabel->setScreenRectangle(ux + 48, body + 10, col - 48, 28);
	mapsUnlockedByLabel->setScreenRectangle(ux, body + 56, col, 22);
	mapsUnlockedBy->setScreenRectangle(ux, body + 84, col, std::max(40, f.footer - body - 96));
	cancel->setScreenRectangle(f.x, f.footer, f.w / 2 - 4, 48);
	ok->setScreenRectangle(f.x + f.w / 2 + 4, f.footer, f.w / 2 - 4, 48);
	// With a landscape keyboard, devote the remaining viewport to the active
	// field. Footer actions stay reachable and never cover the draft.
	if (mobileKeyboardInset(globalContainer->gfx) > 0 && f.h < 360)
	{
		for (auto *widget : widgets)
			widget->visible = false;
		ok->visible = cancel->visible = true;
		if (nameEditor->isActivated())
		{
			nameEditor->visible = true;
			nameEditor->setScreenRectangle(f.x, f.y, f.w,
										   std::min(40, std::max(24, f.footer - f.y - 8)));
		}
		else
		{
			descriptionEditor->visible = true;
			fitArea(descriptionEditor, f.x, f.y, f.w, std::max(24, f.footer - f.y - 8));
		}
	}
	SDL_Rect bounds{f.x, f.y, f.w, f.h};
	if (FrontendTheme::current)
		FrontendTheme::current->background(gfx, true, &bounds);
}
