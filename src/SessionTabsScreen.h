// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

class SessionTabsScreen;

// One tab of a multi-tab network session (online lobby, game room, options...).
// Tabs own their model and network listeners; the session screen owns the tabs,
// renders the active one and forwards timers. A tab ends itself with finish().
class SessionTab
{
  public:
	virtual ~SessionTab() = default;
    virtual const char* recordingId() const { return "session_tab"; }
	virtual std::string title() const = 0;
	virtual Glob2UI::Element build(const Glob2UI::Presentation &presentation) = 0;
	virtual void onTimer(Uint32) {}
	virtual void onActivated() {}
	// Optional Escape handling; default finishes the whole session with CANCEL semantics.
	virtual bool onEscape() { return false; }
	bool finished() const { return result.has_value(); }
	int returnCode() const { return result.value_or(-1); }
	void finish(int code) { result = code; }
	bool isActivated() const { return activated; }

  protected:
	friend class SessionTabsScreen;
	SessionTabsScreen *session = nullptr;
	bool activated = false;
	std::optional<int> result;
	// Ask the session to rebuild after a model change.
	void refresh();
};

// Hosts tabs. A tab that finishes ends the session with its code when it is the
// session's primary tab; other tabs' completion is observed by whoever added them.
class SessionTabsScreen : public Glob2UI::Screen
{
  public:
    const char* recordingId() const override { return active ? active->recordingId() : "session_tabs"; }
	SessionTabsScreen();
	~SessionTabsScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;
	// The screen keeps a non-owning pointer; the caller keeps the tab alive until removed.
	void addTab(SessionTab *tab, bool primary = false);
	void removeTab(SessionTab *tab);
	void activate(SessionTab *tab);
	SessionTab *activeTab() const { return active; }
	// End the whole session.
	void finishSession(int code) { endExecute(code); }

  protected:
	void onEscape() override;

  private:
	struct Entry
	{
		SessionTab *tab;
		bool primary;
	};
	std::vector<Entry> tabs;
	SessionTab *active = nullptr;
};
