// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <ScrollPhysics.h>
#include <TouchInput.h>
#include "gui/BrushHUD.h"
#include <memory>
#include <optional>
#include <set>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
class MapEdit;
class MapEditorWidget;
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
	struct Row
	{
		MapEditorWidget *widget;
		GAGCore::ViewRect rect;
		double scale;
	};
	MapEdit &editor;
	GAGCore::TouchInput touch;
	std::vector<Row> rows;
	GAGCore::ViewRect safe, content;
	bool tools = true, pan = false, onMap = false;
	int paletteMode = 2; // Terrain, resources, buildings, flags/units.
	GAGCore::ViewRect tray, modeBar;
	struct Drag
	{
		Sint64 device, finger;
		MapEditorWidget *widget;
		GAGCore::ViewPoint start;
		bool moving = false, browsing = false;
	};
	std::optional<Drag> drag;
	std::set<std::pair<Sint64, Sint64>> quarantined;
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
	std::pair<Sint64, Sint64> touchKey{};
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
	double offset = 0, maximum = 0;
	// Momentum and bounce; syncTray() and syncInspector() keep the axes and
	// the plain `offset` and `inspectorScroll` variables in step.
	GAGCore::ScrollMotion mapMotion;
	GAGCore::TrackedScrollAxis trayAxis, inspectorAxis;
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
