// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "SoundMixer.h"
#include <GameplayRecording.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <cstdio>
#include <iterator>

using namespace GAGCore;
using namespace Glob2UI;

SettingsScreen::SettingsScreen() : gameKeys(GameGUIShortcuts), editorKeys(MapEditShortcuts) {}

void SettingsScreen::custom(const std::string &id, std::function<Element(const Presentation &)> render)
{
	auto &r = add(id, Kind::Custom, "");
	r.render = std::move(render);
}

SettingsScreen::~SettingsScreen()
{
	if (settingsDirty || keyboardDirty[0] || keyboardDirty[1])
		persist();
}

std::string SettingsScreen::tr(const std::string &text)
{
	return Toolkit::getStringTable()->getString("[settings " + text + "]");
}

SettingsScreen::Row &SettingsScreen::add(const std::string &id, Kind kind, const std::string &label, const std::string &help)
{
	Row r;
	r.id = id;
	r.kind = kind;
	r.label = label;
	r.help = help;
	form.push_back(std::move(r));
	return form.back();
}
void SettingsScreen::section(const std::string &label) { add("", Kind::Section, tr(label)); }
void SettingsScreen::info(const std::string &label) { add("", Kind::Info, label); }
void SettingsScreen::button(const std::string &id, const std::string &label, std::function<void()> action, bool selected)
{
	auto &r = add(id, Kind::Button, label);
	r.action = std::move(action);
	r.selected = selected;
}
void SettingsScreen::choice(const std::string &id, const std::string &label, const std::string &help, int value,
							std::vector<std::string> labels, std::function<void(int)> change)
{
	auto &r = add(id, Kind::Choice, tr(label), help.empty() ? "" : tr(help));
	r.number = value;
	r.choices = std::move(labels);
	r.change = std::move(change);
	if (value >= 0 && value < int(r.choices.size()))
		r.value = r.choices[std::size_t(value)];
}
void SettingsScreen::toggle(const std::string &id, const std::string &label, const std::string &help, bool value, std::function<void(int)> change)
{
	auto &r = add(id, Kind::Toggle, tr(label), help.empty() ? "" : tr(help));
	r.number = value;
	r.change = std::move(change);
}
void SettingsScreen::number(const std::string &id, const std::string &label, int value, int minimum, int maximum, std::function<void(int)> change)
{
	auto &r = add(id, Kind::Number, label);
	r.number = value;
	r.minimum = minimum;
	r.maximum = maximum;
	r.change = std::move(change);
	r.value = std::to_string(value);
}

void SettingsScreen::resetScroll()
{
	host().state("settings/" + std::to_string(int(current))).scroll = 0;
	invalidate();
}

std::string SettingsScreen::categoryName(Category category) const
{
	const char *names[] = {
		"Display & graphics", "Audio",  "Gameplay",    "Building defaults", "Controls",
		"Language & player",  "Online", "Hive Mind",   "Recording",         "Experiments",
		"Custom AIs"};
	static_assert(std::size(names) == std::size_t(Category::CustomAIs) + 1, "a name for every settings category");
	return tr(names[int(category)]);
}

void SettingsScreen::buildRows()
{
	form.clear();
	if (modal != Modal::None)
		buildModal();
	else if (current == Category::Buildings)
		buildBuildings();
	else if (current == Category::Controls)
		buildKeyboard();
	else if (current == Category::Online)
		buildOnline();
	else if (current == Category::HiveMind)
		buildHiveMind();
	else if (current == Category::Recording)
		buildRecording();
	else if (current == Category::CustomAIs)
		buildCustomAIs();
	else
		buildGeneral();
	if (current == Category::Buildings && touchLayout)
		for (auto &row : form)
			if (row.kind == Kind::Number)
				row.kind = Kind::Slider;
}

// Rows keyed by id read their laid-out rectangles from the host.
void SettingsScreen::measureRows()
{
	host().layoutIfNeeded();
	for (auto &row : form)
	{
		auto assign = [&](const std::string &key, Rect &target)
		{
			if (key.empty())
				return;
			if (auto *node = host().find(key))
				target = {node->bounds.x, node->bounds.y, node->bounds.w, node->bounds.h};
			else if (SDL_getenv_unsafe("GLOB2_UI_DEBUG"))
				std::fprintf(stderr, "settings: no element for row %s\n", key.c_str());
		};
		assign(row.id, row.control);
		row.bounds = row.control;
		if (!row.extraId.empty())
			if (auto *node = host().find(row.extraId))
			{
				const Glob2UI::Rect extra = node->bounds;
				const int right = std::max(row.bounds.x + row.bounds.w, extra.x + extra.w);
				const int bottom = std::max(row.bounds.y + row.bounds.h, extra.y + extra.h);
				row.bounds.x = std::min(row.bounds.x, extra.x);
				row.bounds.y = std::min(row.bounds.y, extra.y);
				row.bounds.w = right - row.bounds.x;
				row.bounds.h = bottom - row.bounds.y;
			}
	}
}

const std::vector<SettingsScreen::Row> &SettingsScreen::rows()
{
	invalidate();
	measureRows();
	return form;
}

bool SettingsScreen::changeSetting(const std::string &id, int value)
{
	buildRows();
	for (auto r : form)
		if (r.id == id && r.enabled && r.change)
		{
			if (r.kind == Kind::Choice && (value < 0 || value >= int(r.choices.size())))
				return false;
			if (r.kind == Kind::Number || r.kind == Kind::Slider)
				value = std::clamp(value, r.minimum, r.maximum);
			if (r.kind == Kind::Toggle)
				value = !!value;
			r.change(value);
			invalidate();
			return true;
		}
	return false;
}

void SettingsScreen::activateSetting(const std::string &id)
{
	invalidate();
	host().layoutIfNeeded();
	if (auto *node = host().find(id))
	{
		host().focus(id, true);
		node->activate(host(), 0);
		invalidate();
	}
}

std::vector<SettingsScreen::Category> SettingsScreen::visibleCategories() const
{
	std::vector<Category> result{Category::Display, Category::Audio, Category::Gameplay, Category::Buildings};
	if (!touchLayout)
		result.push_back(Category::Controls);
	result.push_back(Category::Player);
#if !defined(GLOB2_CHINA_RELEASE) && !defined(GLOB2_AMAZON_RELEASE)
	result.push_back(Category::Online);
	result.push_back(Category::HiveMind);
#endif
	if (GAGCore::Recording::supported())
		result.push_back(Category::Recording);
	result.push_back(Category::Experiments);
	result.push_back(Category::CustomAIs);
	return result;
}

void SettingsScreen::selectCategory(Category category)
{
	const auto categories = visibleCategories();
	if (std::find(categories.begin(), categories.end(), category) == categories.end())
		return;
	host().closePopup();
	selectedBuilding = -1;
	host().endEditing();
	finishInteraction();
	current = category;
	modal = Modal::None;
	host().focus("", false);
	invalidate();
}

void SettingsScreen::commit(bool defer)
{
	settingsDirty = true;
	if (defer)
		saveAt = SDL_GetTicks() + 300;
	else
		persist();
	invalidate();
}

bool SettingsScreen::persist()
{
	saveAt = 0;
	try
	{
		// Write unconditionally, not only when locally dirty: a close with
		// nothing edited must still flush to durable storage (a prior browser
		// storage-restore failure can leave already-committed settings
		// unflushed), and every other caller only reaches persist() with at
		// least one dirty flag already set, so this adds no redundant I/O there.
		if (globalContainer->settings.save())
			settingsDirty = false;
		if (gameKeys.saveKeyboardLayout())
			keyboardDirty[0] = false;
		if (editorKeys.saveKeyboardLayout())
			keyboardDirty[1] = false;
		failed = settingsDirty || keyboardDirty[0] || keyboardDirty[1];
		// The write above is already durable on native builds. In the browser it
		// lands in Emscripten's virtual filesystem first and needs this separate
		// flush to survive a reload; poll it from onTimer rather than block here.
		persistence.reset();
		if (!failed)
		{
			if (GAGCore::ApplicationHost::storageRestoreFailed())
				failed = true;
			else
			{
				persistence = GAGCore::ApplicationHost::persistStorage();
				if (!persistence)
					failed = true;
			}
		}
	}
	catch (const std::exception &)
	{
		failed = true;
		persistence.reset();
	}
	invalidate();
	return !failed;
}

void SettingsScreen::finishInteraction()
{
	if (settingsDirty || keyboardDirty[0] || keyboardDirty[1])
		persist();
}

void SettingsScreen::done()
{
	if (customAIBusy())
		return;
	host().closePopup();
	host().endEditing();
	// Always confirm durability on close, not just when something in this
	// session is dirty: a prior browser storage-restore failure can leave
	// already-committed settings unflushed.
	persist();
	// Keep Retry available instead of silently losing local shortcut edits.
	if (!failed)
	{
		if (persistence)
			closing = true;
		else
			endExecute(1);
	}
}

void SettingsScreen::abandon()
{
	if (customAIBusy())
		return;
	// Always closes in one click, whether or not anything is dirty or
	// failed: every change is already live and auto-saved as it's made, so
	// there is nothing to discard.
	host().closePopup();
	host().endEditing();
	endExecute(1);
}

void SettingsScreen::dismiss()
{
	if (modal == Modal::Conflict)
	{
		modal = Modal::Binding;
		invalidate();
	}
	else if (modal != Modal::None)
		closeModal();
	else
		done();
}

// The fixed Back button follows the visible hierarchy; detail pages do not
// need a second, competing Back action inside their scrollable content.
void SettingsScreen::navigateBack()
{
	host().endEditing();
	finishInteraction();
	if (phonePage() && current == Category::Buildings && selectedBuilding >= 0)
	{
		selectedBuilding = -1;
		resetScroll();
		return;
	}
	abandon();
}

void SettingsScreen::onEscape()
{
	if (phonePage() && current == Category::Buildings && selectedBuilding >= 0)
		navigateBack();
	else
		dismiss();
}

// Shortcut capture must see raw keys before the framework interprets them.
bool SettingsScreen::interceptEvent(const SDL_Event &event)
{
	if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) && event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
	{
		finishInteraction();
		captureKey = -1;
		return false;
	}
	if (modal != Modal::Binding || captureKey < 0 || event.type != SDL_EVENT_KEY_DOWN)
		return false;
	const SDL_Keycode key = event.key.key;
	if (key == SDLK_ESCAPE)
	{
		captureKey = -1;
		invalidate();
		return true;
	}
	if (key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LCTRL || key == SDLK_RCTRL || key == SDLK_LALT ||
		key == SDLK_RALT || key == SDLK_LGUI || key == SDLK_RGUI)
		return true;
	bindingKeys[std::size_t(captureKey)] = KeyPress(event.key, bindingKeys[std::size_t(captureKey)].getPressed());
	captureKey = -1;
	invalidate();
	return true;
}

void SettingsScreen::onTimer(Uint32 tick)
{
	lastTick = tick;
	pollCustomAIs();
	if (current == Category::Online)
		pollOnline();
	if (current == Category::Recording && int(GAGCore::Recording::encoder()) != recordingEncoder)
		invalidate();
	if (saveAt && Sint32(tick - saveAt) >= 0)
		persist();
	if (persistence)
	{
		const auto state = persistence->state();
		if (state != GAGCore::ApplicationHost::PersistenceState::Pending)
		{
			if (state == GAGCore::ApplicationHost::PersistenceState::Failed)
			{
				failed = true;
				closing = false;
				// Retry the whole write, not just the flush: the file itself
				// may also need rewriting if this state was reached because
				// the browser evicted storage mid-session.
				settingsDirty = keyboardDirty[0] = keyboardDirty[1] = true;
				saveAt = tick + 300;
			}
			persistence.reset();
			invalidate();
			if (closing && !failed && !settingsDirty && !keyboardDirty[0] && !keyboardDirty[1])
			{
				closing = false;
				endExecute(1);
			}
		}
	}
	if (!pendingFocus.empty())
	{
		host().layoutIfNeeded();
		if (host().find(pendingFocus))
		{
			host().focus(pendingFocus, true);
			host().scrollIntoView(pendingFocus);
		}
		pendingFocus.clear();
	}
}

bool SettingsScreen::restartRequired() const
{
#ifdef GLOB2_MOBILE
	return false;
#else
	const auto &s = globalContainer->settings;
	const Uint32 mask = GraphicContext::USEGPU | GraphicContext::CUSTOMCURSOR;
	return (s.screenFlags & mask) != (globalContainer->gfx->getOptionFlags() & mask);
#endif
}

void SettingsScreen::changeUiScale(int percent)
{
	auto &s = globalContainer->settings;
	if (percent == s.uiScale) return;
	displayError = false;
	if (!globalContainer->gfx->setUiScale(percent / 100.0f))
	{
		displayError = true; invalidate(); return;
	}
	s.uiScale = percent;
	commit();
}

bool SettingsScreen::applyDisplayMode(int width, int height, Uint32 flags)
{
	return globalContainer->gfx->setFullscreen(bool(flags & GraphicContext::FULLSCREEN));
}

void SettingsScreen::changeDisplay(std::function<void(Settings &)> change)
{
	auto &s = globalContainer->settings;
	Settings candidate = s;
	change(candidate);
	displayError = false;
	const bool fullscreenChanged = bool((candidate.screenFlags ^ globalContainer->gfx->getOptionFlags()) & GraphicContext::FULLSCREEN);
	if (fullscreenChanged && !applyDisplayMode(candidate.screenWidth, candidate.screenHeight, candidate.screenFlags))
	{
		displayError = true; invalidate(); return;
	}
	candidate.screenWidth = globalContainer->gfx->getRequestedW();
	candidate.screenHeight = globalContainer->gfx->getRequestedH();
	s = candidate;
	commit();
}

// Layout ------------------------------------------------------------------

Glob2UI::Rect SettingsScreen::available(const Presentation &p, const Metrics &m)
{
	// Text editing on phones keeps the keyboard-reduced rectangle.
	return p.dialog;
}

Element SettingsScreen::categoryNavigation(const Presentation &p, bool sidebar)
{
	const auto categories = visibleCategories();
	if (sidebar)
	{
		std::vector<Element> items;
		for (auto category : categories)
		{
			ButtonOptions options;
			options.flat = true;
			options.alignLeft = true;
			options.selected = category == current && modal == Modal::None;
			options.minHeight = 42;
			// One icon per Category, in its order.
			static constexpr UIIcon icons[] = {
				UIIcon::Display,   UIIcon::Audio,       UIIcon::Gameplay,
				UIIcon::Buildings, UIIcon::Controls,    UIIcon::Player,
				UIIcon::Online,    UIIcon::Crown,       UIIcon::Display,
				UIIcon::Experiments, UIIcon::Gameplay};
			static_assert(std::size(icons) == std::size_t(Category::CustomAIs) + 1,
						  "an icon for every settings category");
			options.icon = uiIcon(icons[int(category)]);
			items.push_back(Glob2UI::button("nav." + std::to_string(int(category)), categoryName(category),
											[this, category] { selectCategory(category); }, options));
		}
		return column(std::move(items), {p.pt(4)});
	}
	std::vector<std::string> names;
	int selected = 0;
	for (std::size_t i = 0; i < categories.size(); ++i)
	{
		names.push_back(categoryName(categories[i]));
		if (categories[i] == current)
			selected = int(i);
	}
	return Glob2UI::choice("nav.current", names, selected, [this, categories](int v)
						   {
							   if (v >= 0 && v < int(categories.size()))
								   selectCategory(categories[std::size_t(v)]);
						   });
}

Element SettingsScreen::rowElement(const Row &r, const Presentation &p)
{
	const std::string help = r.help;
	switch (r.kind)
	{
	case Kind::Section:
		return padding({0, p.pt(12), 0, 0}, heading(r.label));
	case Kind::Info:
		return paragraph(r.label, {FontRole::Body, true});
	case Kind::Toggle:
	{
		auto change = r.change;
		auto control = Glob2UI::toggle(r.id, r.label, r.number != 0, [change](bool v) { if (change) change(v ? 1 : 0); }, r.enabled);
		if (help.empty())
			return control;
		return column({control, padding({p.pt(36), 0, 0, 0}, paragraph(help, {FontRole::Support, true}))}, {p.pt(2)});
	}
	case Kind::Choice:
	{
		auto change = r.change;
		ChoiceOptions options;
		options.controlEnabled = r.enabled;
		return field(r.label, Glob2UI::choice(r.id, r.choices, r.number, [change](int v) { if (change) change(v); }, options), {help, p.touch ? 220.0 : 140.0});
	}
	case Kind::Slider:
	{
		auto change = r.change;
		SliderOptions options;
		options.enabled = r.enabled;
		options.valueText = r.value;
		return field(r.label, slider(r.id, r.number, r.minimum, r.maximum, [change](int v) { if (change) change(v); }, options), {help, p.touch ? 220.0 : 140.0});
	}
	case Kind::Number:
	{
		auto change = r.change;
		StepperOptions options;
		options.enabled = r.enabled;
		options.valueText = r.value;
		auto control = stepper(r.id, r.number, r.minimum, r.maximum, [change](int v) { if (change) change(v); }, options);
		if (r.label.empty())
			return control;
		return field(r.label, control, {help, p.touch ? 220.0 : 140.0});
	}
	case Kind::Text:
	{
		TextFieldOptions options;
		options.maxLength = BasePlayer::MAX_NAME_LENGTH;
		options.commitOnSubmit = true;
		// The draft is saved as it is typed (debounced) and Escape restores the
		// previous name; Enter and focus loss save at once.
		options.preview = [this](const std::string &value)
		{
			globalContainer->settings.setUsername(value);
			commit(true);
		};
		auto control = textField(r.id, r.value,
								 [this](const std::string &value)
								 {
									 globalContainer->settings.setUsername(value);
									 commit();
								 },
								 options);
		return field(r.label, control, {help, p.touch ? 220.0 : 140.0});
	}
	case Kind::Button:
	{
		ButtonOptions options;
		options.selected = r.selected;
		options.enabled = r.enabled;
		auto action = r.action;
		auto control = Glob2UI::button(r.id, r.label, [this, action]
									   {
										   if (action)
											   action();
										   invalidate();
									   },
									   options);
		if (r.buildingIcon < 0)
			return control;
		// Building rows lead with the building's own artwork.
		const auto name = IntBuildingType::typeFromShortNumber(r.buildingIcon);
		GAGCore::Sprite *artwork = nullptr;
		int frame = -1;
		if (auto *type = globalContainer->buildingsTypes.getByType(name, 0, false))
		{
			artwork = type->miniSpriteImage >= 0 ? type->miniSpritePtr : type->gameSpritePtr;
			frame = type->miniSpriteImage >= 0 ? type->miniSpriteImage : type->gameSpriteImage;
		}
		if (!artwork || frame < 0)
			return control;
		return row({sized({p.pt(56), p.pt(56)}, sprite(artwork, frame, Size{p.pt(56), p.pt(56)})), expanded(control)}, {-1, CrossAlign::Center});
	}
	case Kind::Custom:
		return r.render ? r.render(p) : empty();
	case Kind::Binding:
	{
		ButtonOptions options;
		options.enabled = r.enabled;
		options.alignLeft = true;
		auto action = r.action;
		auto change = r.change;
		std::vector<Element> parts{expanded(Glob2UI::button(r.id, r.value, [this, action]
															{
																if (action)
																	action();
																invalidate();
															},
															options))};
		if (!r.extraId.empty())
			parts.push_back(width(p.pt(48), Glob2UI::button(r.extraId, "+", [this, change]
															{
																if (change)
																	change(0);
																invalidate();
															})));
		return field(r.label, row(std::move(parts)), {help, 300});
	}
	}
	return empty();
}

Element SettingsScreen::build(const Presentation &p)
{
	touchLayout = p.touch;
	phoneLayout = p.phone();
	wideTable = !p.compact() && p.safe.w >= p.pt(900);
	buildRows();
	// Rows sharing a column count form a grid row; everything else stacks.
	std::vector<Element> content;
	for (std::size_t i = 0; i < form.size();)
	{
		const int cols = form[i].columns;
		std::size_t end = i + 1;
		if (cols > 1 && form[i].column == 0)
			while (end < form.size() && form[end].columns == cols && form[end].column != 0)
				++end;
		if (cols > 1 && wideTable)
		{
			std::vector<Element> cells;
			for (std::size_t n = i; n < end; ++n)
				cells.push_back(rowElement(form[n], p));
			WrapOptions grid;
			grid.minChildWidth = 1;
			grid.maxColumns = cols;
			content.push_back(wrap(std::move(cells), grid));
		}
		else if (cols > 1)
		{
			std::vector<Element> cells;
			for (std::size_t n = i; n < end; ++n)
				if (form[n].kind != Kind::Info || form[n].label != "—")
					cells.push_back(rowElement(form[n], p));
			if (form[i].kind == Kind::Button)
			{
				WrapOptions grid;
				grid.minChildWidth = p.pt(140);
				content.push_back(wrap(std::move(cells), grid));
			}
			else
				content.push_back(column(std::move(cells), {p.pt(4)}));
		}
		else
			content.push_back(rowElement(form[i], p));
		// Desktop rows sit on ruled lines, as before.
		if (!p.touch && form[i].kind != Kind::Section && form[i].kind != Kind::Info && form[i].kind != Kind::Button)
			content.push_back(divider());
		i = end;
	}
	const std::string scrollKey = modal != Modal::None ? "settings/modal" : "settings/" + std::to_string(int(current));
	auto body = scroll(scrollKey, column(std::move(content), {p.pt(10)}));

	const bool cannotSave = failed || GAGCore::ApplicationHost::storageRestoreFailed();
	std::string status = cannotSave ? tr("Could not save") : settingsDirty ? tr("Saving…") : restartRequired() ? tr("Saved — restart required") : tr("Changes saved automatically");
	std::vector<MenuAction> buttons;
	if (phonePage())
	{
		if (current == Category::Buildings && selectedBuilding >= 0)
			buttons.push_back({"back", tr("Back"), [this] { navigateBack(); }, false, SDLK_ESCAPE});
		buttons.push_back({"done", tr(failed ? "Retry" : "Done"), [this] { done(); }, true});
	}
	else
	{
		// Every change applies and saves as it is made ("Changes saved
		// automatically"), so there is nothing for a Cancel to undo: Done closes.
		// Only when saving fails (or cannot last: the browser's storage did not
		// restore) is there a way to leave without trying again.
		if (modal == Modal::None && cannotSave)
			buttons.push_back({"cancel", tr("continue"), [this] { abandon(); }});
		buttons.push_back({"done", tr(modal == Modal::None ? "Done" : "Cancel"), [this] { dismiss(); }, true});
	}
	auto footerRow = row({expanded(paragraph(status, {FontRole::Support, true})), actions(std::move(buttons), p)}, {-1, CrossAlign::Center});

	// The rail only when the body keeps its room beside it (760 points at 100% text).
	const bool sidebar = modal == Modal::None && !p.compact() && p.safe.w >= p.pt(584) + p.textPt(176);
	Element page;
	if (sidebar)
	{
		// Widens with larger text and scrolls when the categories outgrow the page.
		auto rail = padding({0, 0, p.pt(8), 0}, width(p.textPt(176), scroll("nav", categoryNavigation(p, true))));
		page = column({heading(tr("Settings")), expanded(row({rail, expanded(body)}, {-1, CrossAlign::Stretch})), divider(), footerRow}, {p.pt(10)});
	}
	else
	{
		std::vector<Element> parts;
		if (!phonePage())
			parts.push_back(heading(tr("Settings")));
		if (modal == Modal::None)
			parts.push_back(categoryNavigation(p, false));
		parts.push_back(expanded(body));
		parts.push_back(divider());
		parts.push_back(footerRow);
		page = column(std::move(parts), {p.pt(10)});
	}
	CardOptions cardOptions;
	cardOptions.padding = p.pt(phonePage() ? 12 : 20);
	const int maxW = std::min(p.safe.w, p.pt(960));
	if (!p.touch)
	{
		// The desktop panel keeps its former size: up to 960x720 with a margin.
		const int h = std::min(p.safe.h - 2 * p.pt(20), p.pt(720));
		return center(sized({maxW, h}, card(page, cardOptions)));
	}
	return center(maxWidth(maxW, card(page, cardOptions)));
}
