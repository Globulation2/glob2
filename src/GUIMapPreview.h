// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIBase.h>
#include "MapThumbnail.h"
#include "MapPreviewGeometry.h"
#include <functional>
using namespace GAGGUI;
using namespace GAGCore;

struct MapStart
{
	int x, y;
	Color color;
};

class MapPreview : public RectangularWidget
{
  public:
	using Start = MapStart;
	std::vector<Start> starts;
	int markerSize = 16;
	static constexpr int PreviewSize = 128;
	static constexpr Uint32 TransitionDurationMs = 200;
	enum class State
	{
		Empty,
		Loading,
		Ready,
		Failed
	};
	MapPreview(int x, int y, Uint32 hAlign, Uint32 vAlign);
	MapPreview(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string &tooltip,
			   const std::string &tooltipFont);
	~MapPreview() override;
	void paint() override;
	virtual void setMapThumbnail(const std::string &filename);
	virtual void setMapThumbnail(const MapThumbnail &thumbnail);
	void setState(State state);
	State getState() const { return state; }
	int getLastWidth() const { return thumbnail.getMapWidth(); }
	int getLastHeight() const { return thumbnail.getMapHeight(); }
	bool isThumbnailLoaded() const { return thumbnail.isLoaded(); }
	std::function<void()> retry;
	bool handlePreviewEvent(SDL_Event *event);
	void onSDLMouseButtonDown(SDL_Event *e) override { handlePreviewEvent(e); }
	void onSDLMouseButtonUp(SDL_Event *e) override { handlePreviewEvent(e); }
	void onSDLMouseMotion(SDL_Event *e) override { handlePreviewEvent(e); }
	void onSDLActive(SDL_Event *e) override { handlePreviewEvent(e); }
	void onSDLMouseWheel(SDL_Event *e) override { handlePreviewEvent(e); }
	void cancelDrag();
	void resetView()
	{
		view.reset();
		zoom = 1;
		cancelDrag();
	}
	MapPreviewGeometry::Rect mapArea();

  protected:
	bool animateChanges = true;
	virtual void paintOverlay(DrawableSurface *, MapPreviewGeometry::Rect);
	MapThumbnail thumbnail;
	DrawableSurface *surface = nullptr;
	MapPreviewGeometry view;

  private:
	friend struct MapPreviewHarness;
	friend struct CustomGameSetupHarness;
	State state = State::Empty;
	bool dragging = false;
	double zoom = 1;
	DrawableSurface *raster = nullptr;
	double rasterOffsetX = -1, rasterOffsetY = -1, rasterZoom = -1;
	int mouseX = 0, mouseY = 0;
	std::unique_ptr<DrawableSurface> previousFrame;
	bool transitioning = false, transitionPending = false;
	Uint32 transitionStarted = 0;
	Uint8 transitionAlpha() const;
	MapPreviewGeometry::Rect box();
	MapPreviewGeometry::Rect worldArea();
};
