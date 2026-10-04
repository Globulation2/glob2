// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/// Presentation commands that map scripts (SGSL, USL and JavaScript) issue to
/// the client that is watching the game. Scripts run inside the simulation
/// tick, so they must not reach into GameGUI directly: they call this
/// interface instead. GameGUI implements it and applies each command at once
/// while the game still runs on one thread; a threaded session can implement
/// it by enqueueing the same calls onto ClientEvents without touching any
/// script code.
///
/// The read methods at the bottom are legacy USL/SGSL queries of client
/// state. They are deterministic only because every client applies the same
/// script commands; do not add new reads. See docs/development/reference.md.
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
