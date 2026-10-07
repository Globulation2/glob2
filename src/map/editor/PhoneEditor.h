// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <ScrollPhysics.h>
#include <GestureScroll.h>
#include <TouchInput.h>
#include "BrushHUD.h"
#include <memory>
#include <optional>
#include <set>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
class MapEdit;
class MapEditorWidget;
struct BrushEntry;
class ValueScrollBox;
class Minimap;
struct Tile;
namespace Utilities
{
class BitArray;
}
class PhoneEditor
{
  public:
	explicit PhoneEditor(MapEdit &editor);
	~PhoneEditor();
	bool event(SDL_Event event);
	void draw();
	void cancel();
	bool hasOverlay() const;
	// Per-frame momentum for the map, the tool tray and the inspector.
	void advance(Uint32 tick);
	bool animating() const;

  private:
	friend class GameGUITouchHarness;
	friend class MobileGalleryGameplay;
	// One card of the tray. Cards come from the brush catalogue
	// (MapEdit::brushCatalog) and are identified by entry id, so they survive
	// catalogue rebuilds; the script-area number and name are the only shared
	// desktop widgets the tray still hosts.
	struct Row
	{
		enum class Kind : std::uint8_t
		{
			Entry,     // a catalogue brush
			Fertility, // the fertility overlay toggle ("tool/fertility")
			Widget     // script-area number cycler ("area/number") and name ("area/name")
		};
		Kind kind = Kind::Entry;
		std::string id;
		MapEditorWidget *widget = nullptr;
		GAGCore::ViewRect rect; // on screen, scrolled
		double x = 0, width = 0; // tray-relative layout
		std::vector<std::string> lines; // label, at most two lines
		int chip = -1; // the group header chip this card belongs to
	};
	// Group headers of the Terrain and Resources trays; a tap jumps to the group.
	struct Chip
	{
		std::string title;
		int first = 0; // first row of the group
		double x = 0, width = 0;
		GAGCore::ViewRect rect;
	};
	MapEdit &editor;
	GAGCore::TouchInput touch;
	std::vector<Row> rows;
	std::vector<Chip> chips;
	GAGCore::ViewRect safe, content;
	bool tools = true, pan = false, onMap = false;
	// Terrain, resources, buildings, flags/units, teams.
	static constexpr int modeCount = 5;
	// Points: the card strip, and the group chips above it in Terrain and Resources.
	static constexpr double cardHeight = 68, chipHeight = 36;
	int paletteMode = 2;
	GAGCore::ViewRect tray, modeBar, chipBar, cardBar;
	double chipOffset = 0;
	// Card layout is measured once per catalogue revision, mode and size.
	std::string trayLayoutKey;
	void prepareTray(double unit);
	void layoutTray(double unit);
	void drawTray();
	void drawCard(const Row &row);
	void drawSwatch(const BrushEntry &entry, GAGCore::ViewRect box);
	void activateRow(const Row &row, GAGCore::ViewPoint point);
	int activeChip() const;
	// Index of the card with this catalogue id in the current tray, or -1.
	int rowOf(const std::string &id) const;
	// Read-only diagnostic for browser tests (ApplicationHost::controlsChanged):
	// "tray/mode/<n>", "tray/chip/<n>" and "tray/<catalogue id>" bounds.
	void publishControls(bool shown);
	std::string publishedControls;
	void jumpToChip(int chip);
	// Whether a card's brush is placed by dragging it onto the map.
	bool dragPlaces(const Row &row);
	struct Drag
	{
		SDL_TouchID device;
		SDL_FingerID finger;
		std::string id, action;
		GAGCore::ViewPoint start;
		bool moving = false, browsing = false;
	};
	std::optional<Drag> drag;
	std::set<std::pair<SDL_TouchID, SDL_FingerID>> quarantined;
	std::vector<GAGCore::ViewPoint> stroke;
	// One-finger zoom: a completed map tap arms the next contact. A painted tap
	// waits one double-tap window, since it may be the first half of a zoom.
	struct DeferredStroke
	{
		std::vector<GAGCore::ViewPoint> points;
		int selection, terrain, figure, type;
		std::uint64_t ticks;
	};
	std::optional<DeferredStroke> deferred;
	std::optional<Uint32> lastTapTicks;
	Uint32 eventTicks = 0;
	std::pair<SDL_TouchID, SDL_FingerID> touchKey{};
	GAGCore::ViewPoint lastTapPoint{}, touchStart{}, touchPoint{};
	bool touchTravelled = false;
	bool zoomArmed(Uint32 ticks, GAGCore::ViewPoint point) const;
	bool deferredMatchesTool(const DeferredStroke &candidate) const;
	void commitDeferred();
	void drawZoomReadout();
	void chooseMode(int mode);
	void paintStroke();
	void placeAt(GAGCore::ViewPoint point);
	void label(GAGCore::ViewRect rect, const std::string &text);
	void drawStatusToast();
	void centredLabel(GAGCore::ViewRect rect, const std::string &text);
	double offset = 0, maximum = 0;
	// Momentum and bounce; syncTray() and syncInspector() keep the axes and
	// the plain `offset` and `inspectorScroll` variables in step.
	GAGCore::ScrollMotion mapMotion;
	GAGCore::TrackedScrollAxis trayAxis, inspectorAxis;
	GAGCore::GestureScrollController nativeScroll;
	Uint64 nativeSequence = 0;
	int nativeSurface = 0; // 1 tray, 2 inspector; retained for cancelled tails
	bool nativeScrolling = false;
	GAGCore::TrackedScrollAxis &nativeAxis() { return nativeSurface == 2 ? inspectorAxis : trayAxis; }
	double &nativeOffset() { return nativeSurface == 2 ? inspectorScroll : offset; }
	Uint64 lastTick = 0;
	bool fingerIsTouch = false; // momentum follows real fingers, not the mouse
	void syncTray();
	void syncInspector();
	void stopScrolling();
	int held = -1;
	struct Property
	{
		ValueScrollBox *value;
		std::string caption;
		GAGCore::ViewRect rect;
	};
	std::vector<Property> properties;
	GAGCore::ViewRect inspector, inspectorBody;
	double inspectorScroll = 0, inspectorMaximum = 0;
	int inspectorIdentity = -1;
	bool inspecting() const;
	void prepareInspector();
	void drawInspector();
	// Brush tools show the shared rail on the thumb edge: sizes, Paint/Erase
	// where it applies, Pan, and Undo for zone, area and no-growth strokes.
	bool paintMode() const;
	BrushHUD::Layout rail() const;
	int railTouched = -1;
	struct EditorUndo
	{
		std::vector<std::pair<int, int>> cells;
		std::vector<Tile> tiles;
		std::vector<char> view; // Displayed zone bits, zone strokes only.
		Utilities::BitArray *zoneView = nullptr;
		std::uint64_t expires = 0;
	};
	std::optional<EditorUndo> undo;
	void applyUndo();
	// The map peek: a large minimap for one-thumb navigation, opened from a
	// Map button in the content's far bottom corner.
	bool peekOpen = false;
	std::unique_ptr<Minimap> peekMinimap;
	bool showsMapButton() const;
	GAGCore::ViewRect mapButton() const;
	GAGCore::ViewRect peekRect() const;
	std::vector<GAGCore::ViewRect> peekButtons() const; // Done, zoom out, zoom in.
	void navigatePeek(GAGCore::ViewPoint point);
	void drawPeek();
	void drawInteractionPreview();
	void clearTool();
	void prepare();
	int hit(GAGCore::ViewPoint point) const;
	void act(const GAGCore::TouchAction &action);
};
