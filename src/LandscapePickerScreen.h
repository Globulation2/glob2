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
/// then reproduces the map that was shown for it, and chosenRequest() carries the parameters it
/// was rolled with - the entry's own, or a random set when "Randomize parameters" was used, so
/// the lobby can take over exactly what was shown. Entries are just a localized name and the
/// request to preview, so any screen that selects a landscape can run it. The returned index and
/// tiles[]/entries[] are always in the constructor's order; sorting and tag filtering only
/// change which entries are visible and in what order they are drawn (`visible`), not their
/// identity, so a caller's mapping from index back to its own data (a generator method, say)
/// stays valid regardless of how the sheet was browsed.
class LandscapePickerScreen : public Glob2Screen
{
	friend struct CustomGameSetupHarness;

  public:
	enum
	{
		CANCEL = -2
	};
	/// How the sheet orders its tiles. Random is the registry's own catalog order (shuffled once
	/// so browsing favours no landscape); Alphabetical sorts by each entry's shown name.
	enum class SortOrder
	{
		Random,
		Alphabetical
	};
	struct Entry
	{
		std::string name;
		GenerationRequest request;
		// The generator this entry previews, for the tag filter row and the live compatibility
		// check against the sheet's own size/colony count; -1 if the caller has none (the entry
		// is then never filtered out and never shown disabled).
		int method = -1;
		// This entry's generator's catalog tags (GeneratorDefinition::tags), for the filter row.
		std::vector<std::string> tags;
	};
	LandscapePickerScreen(const std::string &title, std::vector<Entry> entries, int selected,
						  SortOrder sortOrder = SortOrder::Random);
	~LandscapePickerScreen() override;
	void onAction(Widget *, Action, int, int) override;
	void onSDLEvent(SDL_Event *) override;
	void onTimer(Uint32 tick) override;
	int selection() const { return selected; }
	/// The seed behind the preview shown for the selection, once that preview is ready.
	std::optional<std::uint32_t> chosenSeed() const;
	/// The request the selection's preview was rolled with (see randomizeParameters).
	GenerationRequest chosenRequest() const;
	/// The sort order currently shown, random or alphabetical, as last changed by the person
	/// (starts at the constructor's `sortOrder`) - a caller persists this the way it persists any
	/// other lobby preference.
	SortOrder currentSortOrder() const { return sortOrder; }
	/// Draws every landscape's own controls at random (GenerationRequest::randomizeControls) and
	/// rolls them all again. A random set the world then refuses is drawn again, up to
	/// kRandomDraws times per landscape, so the sheet fills with maps that exist.
	void randomizeParameters();
	/// Puts every landscape back on its registered controls, at the sheet's size and colony
	/// count, and rolls them all again (the way back from randomizeParameters).
	void resetParameters();
	static constexpr int kRandomDraws = 6;
	bool busy() const { return previewer.busy(); }

  private:
	struct Tile
	{
		LandscapePreviewer::Preview preview;
		// Screen-owned shared preview widget, retained while its replacement rolls.
		MapPreview *widget = nullptr;
		unsigned revision = ~0u;
	};
	static std::vector<GenerationRequest> requestsOf(const std::vector<Entry> &);
	void render();
	void refresh();
	void select(int index);
	void confirm();
	/// Recomputes `visible` (entries[] indices, filtered by the active tag choices and ordered by
	/// `sortOrder`) and `incompatible` (each entry's live validateGenerationRequest reason against
	/// the sheet's own size/colony count, empty when it can generate). Called once at construction
	/// and again whenever the person changes the sort order or a filter.
	void rebuild();
	std::string title;
	std::vector<Entry> entries;
	std::vector<Tile> tiles;
	/// Per entry, random draws still allowed after a refused set (0 when the parameters are the
	/// entry's own or the draws are spent).
	std::vector<int> redraws;
	/// entries[] indices currently shown, filtered and ordered; empty-filter/Random means the
	/// identity order 0..entries.size()-1.
	std::vector<int> visible;
	/// Parallel to entries[]: the live reason this entry cannot generate at the sheet's size and
	/// colony count, or empty when it can. Computed once (that size/count do not change while the
	/// sheet is open), never re-rolled, so it costs no worker time.
	std::vector<std::string> incompatible;
	/// GeneratorTags::categories(), fixed at construction, and one chosen filter value per
	/// category (empty meaning "Any" - no filter on that category), parallel to it.
	std::vector<std::string> filterCategories;
	std::vector<std::string> filters;
	int selected, columns = 1;
	int activePreview = -1, pointerX = 0, pointerY = 0;
	bool reveal = true;
	SortOrder sortOrder;
	LobbyControls *controls;
	LandscapePreviewer previewer;
};
