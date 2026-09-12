// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "Glob2Screen.h"
#include "LandscapePreviewer.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
class LobbyControls;

/// A full-window modal showing every landscape as a freshly generated map, for picking one by
/// sight. execute() returns the chosen entry's index, CANCEL, or QUIT_APPLICATION; chosenSeed()
/// then reproduces the map that was shown for it. Entries are just a localized name and the
/// request to preview, so any screen that selects a landscape can run it.
class LandscapePickerScreen : public Glob2Screen
{
	friend struct CustomGameSetupHarness;

  public:
	enum
	{
		CANCEL = -2
	};
	struct Entry
	{
		std::string name;
		GenerationRequest request;
	};
	LandscapePickerScreen(const std::string &title, std::vector<Entry> entries, int selected);
	~LandscapePickerScreen() override;
	void onAction(Widget *, Action, int, int) override;
	void onSDLEvent(SDL_Event *) override;
	void onTimer(Uint32 tick) override;
	int selection() const { return selected; }
	/// The seed behind the preview shown for the selection, once that preview is ready.
	std::optional<std::uint32_t> chosenSeed() const;
	bool busy() const { return previewer.busy(); }

  private:
	struct Tile
	{
		LandscapePreviewer::Preview preview;
		std::unique_ptr<DrawableSurface> surface;
		unsigned revision = ~0u;
	};
	static std::vector<GenerationRequest> requestsOf(const std::vector<Entry> &);
	void render();
	void refresh();
	void select(int index);
	void confirm();
	std::string title;
	std::vector<Entry> entries;
	std::vector<Tile> tiles;
	int selected, columns = 1;
	bool reveal = true;
	LobbyControls *controls;
	LandscapePreviewer previewer;
};
