// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "KeyboardManager.h"
#include "Settings.h"
#include "ui/FrontendUI.h"
#include <ApplicationHost.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace GAGGUI { class ScreenStack; }

// Settings form: category builders produce rows, and build() turns the rows
// into a responsive page. Changes apply immediately and save automatically.
class SettingsScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "settings"; }
	enum class Category
	{
		Display,
		Audio,
		Gameplay,
		Buildings,
		Controls,
		Player,
		Online,
		HiveMind,
		Recording,
		Experiments,
		CustomAIs
	};
	enum class Kind
	{
		Section,
		Info,
		Toggle,
		Choice,
		Slider,
		Number,
		Text,
		Button,
		Binding,
		// A row whose element the category builds itself (Settings > Online).
		Custom
	};
	struct Rect
	{
		int x = 0, y = 0, w = 0, h = 0;
		bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
	};
	struct Row
	{
		std::string id, label, help, value, extraId;
		Kind kind = Kind::Info;
		int number = 0, minimum = 0, maximum = 1;
		int buildingIcon = -1;
		bool enabled = true, selected = false;
		std::vector<std::string> choices;
		std::function<void(int)> change;
		std::function<void()> action;
		std::function<Glob2UI::Element(const Glob2UI::Presentation &)> render;
		Rect bounds, control;
		// Table entries share a line only when the viewport is wide enough.
		int columns = 1, column = 0;
	};
	explicit SettingsScreen(GAGGUI::ScreenStack* screens = nullptr);
	~SettingsScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;

	// Stable semantic interface, also used by native integration tests.
	void selectCategory(Category category);
	std::vector<Category> visibleCategories() const;
	Category category() const { return current; }
	// Rows with their laid-out rectangles.
	const std::vector<Row> &rows();
	bool changeSetting(const std::string &id, int value);
	void activateSetting(const std::string &id);
	void finishInteraction();
	bool saveFailed() const { return failed; }
	bool restartRequired() const;
	void done();
	void abandon();

  protected:
	virtual bool applyDisplayMode(int width, int height, Uint32 flags);
	void onEscape() override;
	bool interceptEvent(const SDL_Event &event) override;
	Glob2UI::Rect available(const Glob2UI::Presentation &presentation, const Glob2UI::Metrics &metrics) override;

  private:
	enum class Modal
	{
		None,
		Binding,
		Conflict,
		Restore,
		AILibrary
	};
	GAGGUI::ScreenStack* screens = nullptr;
	Category current = Category::Display;
	Modal modal = Modal::None;
	std::vector<Row> form;
	int selectedBuilding = -1;
	int buildingTab = 0;
	bool failed = false, settingsDirty = false;
	std::array<bool, 2> keyboardDirty{};
	Uint32 saveAt = 0;
	// Acknowledges the latest writes; native persistence completes immediately.
	std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	// done() was called and is waiting on persistence to resolve before endExecute().
	bool closing = false;
	bool displayError = false;
	Row picker;
	KeyboardManager gameKeys, editorKeys;
	ShortcutMode shortcutMode = GameGUIShortcuts;
	std::string pendingFocus;
	int bindingIndex = -1, captureKey = -1;
	Uint32 bindingAction = 0;
	std::vector<KeyPress> bindingKeys;
	std::vector<int> conflicts;
	bool bindingAdvanced = false;
	// Presentation the rows were built for.
	bool touchLayout = false, phoneLayout = false, wideTable = true;
	Uint32 lastTick = 0;

	static std::string tr(const std::string &text);
	bool phonePage() const { return modal == Modal::None && phoneLayout; }
	void buildRows();
	void buildGeneral();
	void buildBuildings();
	void buildBuildingDetail(int type);
	void buildKeyboard();
	// Settings > Online (SettingsScreenOnline.cpp): server choice, account, sign-in methods.
	struct OnlineState;
	std::shared_ptr<OnlineState> online;
	void buildOnline();
	void pollOnline();
	// Settings > Hive Mind (SettingsScreenOnline.cpp): commander shortcuts and account.
	void buildHiveMind();
	// Settings > Recording (SettingsScreenRecording.cpp): controls, hotkey, FFmpeg status.
	void buildRecording();
	// Rebuild the Recording tab when session state changes.
	int recordingState = -1;
	struct CustomAIState;
	std::shared_ptr<CustomAIState> customAIs;
	void buildCustomAIs();
	void pollCustomAIs();
	bool customAIBusy() const;
	Glob2UI::Element buildCustomAILibrary(const Glob2UI::Presentation &p);
	void openCustomAILibrary();
	void closeCustomAILibrary();
	void fetchCustomAIs(bool more = false);
	void selectOnlineAI(const std::string &id);
	void installOnlineAI();
	void socialOnlineAI(bool favourite);
	void selectCustomAIFile(bool linked, const std::string &replace = {});
	// Settings > Online: removes a linked sign-in method (never the last one).
	void unlinkProvider(const std::string &provider, const std::string &name);
	void custom(const std::string &id, std::function<Glob2UI::Element(const Glob2UI::Presentation &)> render);
	void buildModal();
	void resetScroll();
	Row &add(const std::string &id, Kind kind, const std::string &label, const std::string &help = "");
	void button(const std::string &id, const std::string &label, std::function<void()> action, bool selected = false);
	void section(const std::string &label);
	void info(const std::string &label);
	void choice(const std::string &id, const std::string &label, const std::string &help, int value,
				std::vector<std::string> labels, std::function<void(int)> change);
	void toggle(const std::string &id, const std::string &label, const std::string &help, bool value, std::function<void(int)> change);
	void number(const std::string &id, const std::string &label, int value, int minimum, int maximum, std::function<void(int)> change);
	Glob2UI::Element rowElement(const Row &row, const Glob2UI::Presentation &p);
	Glob2UI::Element categoryNavigation(const Glob2UI::Presentation &p, bool sidebar);
	std::string categoryName(Category category) const;
	void commit(bool defer = false);
	bool persist();
	void closeModal();
	void changeDisplay(std::function<void(Settings &)> change);
	void changeUiScale(int percent);
	void navigateBack();
	void dismiss();
	KeyboardManager &keyboard();
	void editBinding(int index, Uint32 action);
	void saveBinding(bool replace = false);
	void deleteBinding();
	std::string bindingLabel(const KeyboardShortcut &shortcut) const;
	void measureRows();
};
