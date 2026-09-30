// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <TouchInput.h>
#include <memory>
#include <optional>
#include <set>
class MapEdit;
class MapEditorWidget;
class ValueScrollBox;
class PhoneEditor
{
  public:
	explicit PhoneEditor(MapEdit &editor);
	~PhoneEditor();
	bool event(SDL_Event event);
	void draw();
	void cancel();
	bool hasOverlay() const;

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
	void chooseMode(int mode);
	void paintStroke();
	void placeAt(GAGCore::ViewPoint point);
	void label(GAGCore::ViewRect rect, const std::string &text);
	double offset = 0, maximum = 0, panX = 0, panY = 0;
	int held = -1;
	struct Property
	{
		ValueScrollBox *value;
		std::string caption;
		GAGCore::ViewRect rect;
	};
	std::vector<Property> properties;
	GAGCore::ViewRect inspector, inspectorBody, brushPanel;
	double inspectorScroll = 0, inspectorMaximum = 0;
	int inspectorIdentity = -1;
	bool brushOpen = false;
	bool inspecting() const;
	void prepareInspector();
	void drawInspector();
	void drawBrushPanel();
	void drawInteractionPreview();
	void clearTool();
	void prepare();
	int hit(GAGCore::ViewPoint point) const;
	void act(const GAGCore::TouchAction &action);
};
