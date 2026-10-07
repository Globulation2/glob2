// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/// Presentation commands that map scripts (SGSL, USL and JavaScript) issue to
/// the client that is watching the game. Scripts run inside the simulation
/// tick, so they must not reach into GameGUI directly: they call this
/// interface instead. ScriptClientChannel provides a stable endpoint, forwarding
/// to GameGUI in serial mode and enqueueing values in threaded mode.
///
/// The read methods at the bottom are legacy USL/SGSL queries of client
/// state. Threaded execution answers from the simulation-owned choice mirror,
/// independently of GUI consumption. Do not add new reads. See
/// docs/development/reference.md.
class ClientCommandSink
{
public:
	virtual ~ClientCommandSink() = default;

	virtual void enableBuildingsChoice(const std::string &name) = 0;
	virtual void disableBuildingsChoice(const std::string &name) = 0;
	virtual void enableFlagsChoice(const std::string &name) = 0;
	virtual void disableFlagsChoice(const std::string &name) = 0;
	virtual void enableGUIElement(int id) = 0;
	virtual void disableGUIElement(int id) = 0;
	//! Add or remove one GameGUI::HighlightObject value (tutorial arrows).
	virtual void setHighlight(int highlight, bool on) = 0;
	//! While set, the client routes the Space key to the script (see
	//! ClientRequests::requestScriptSpace) instead of its normal action.
	virtual void setSwallowSpaceKey(bool value) = 0;
	virtual void showScriptText(const std::string &text) = 0;
	virtual void showScriptTextTr(const std::string &text, const std::string &lang) = 0;
	virtual void hideScriptText() = 0;
	virtual void setScriptPresentationText(std::string text, bool publishHistory) = 0;

	// Legacy reads (USL only).
	virtual bool isBuildingEnabled(const std::string &name) = 0;
	virtual bool isFlagEnabled(const std::string &name) = 0;
};

// A value-only script command. Kept in the same FIFO as other client notices.
struct ScriptPresentation
{
	enum Kind { BuildingChoice, FlagChoice, Element, Highlight, SwallowSpace,
		ShowText, TranslatedText, HideText, SetText } kind;
	int id = 0;
	bool enabled = false;
	std::string text, language;
	void apply(ClientCommandSink& target) const
	{
		switch (kind)
		{
		case BuildingChoice: if (enabled) target.enableBuildingsChoice(text); else target.disableBuildingsChoice(text); break;
		case FlagChoice: if (enabled) target.enableFlagsChoice(text); else target.disableFlagsChoice(text); break;
		case Element: if (enabled) target.enableGUIElement(id); else target.disableGUIElement(id); break;
		case Highlight: target.setHighlight(id, enabled); break;
		case SwallowSpace: target.setSwallowSpaceKey(enabled); break;
		case ShowText: target.showScriptText(text); break;
		case TranslatedText: target.showScriptTextTr(text, language); break;
		case HideText: target.hideScriptText(); break;
		case SetText: target.setScriptPresentationText(text, enabled); break;
		}
	}
};
