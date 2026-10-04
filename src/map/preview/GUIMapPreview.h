// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>
#include "MapThumbnail.h"
#include "MapPreviewGeometry.h"
#include <functional>
#include <memory>
#include <vector>
using namespace GAGCore;

struct MapStart
{
	int x, y;
	Color color;
};

// A map thumbnail with start markers, zoom and drag: the menus host it in a
// framework canvas element, the map command paints it into an export surface.
// The owner sets the rectangle it occupies and hands it the target surface.
class MapPreview
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
	MapPreview();
	virtual ~MapPreview();
	//! Paint into target at the rectangle set by setScreenRectangle/setDimensions.
	void paint(DrawableSurface *target);
	Sint32 getLeft() const { return x; }
	Sint32 getTop() const { return y; }
	Sint32 getWidth() const { return w; }
	Sint32 getHeight() const { return h; }
	void setDimensions(int width, int height) { w = width; h = height; }
	void setScreenRectangle(int nx, int ny, int nw, int nh) { x = nx; y = ny; w = nw; h = nh; }
	virtual void setMapThumbnail(const std::string &filename);
	virtual void setMapThumbnail(const MapThumbnail &thumbnail);
	void setState(State state);
	void setAnimateChanges(bool enabled);
	State getState() const { return state; }
	int getLastWidth() const { return thumbnail.getMapWidth(); }
	int getLastHeight() const { return thumbnail.getMapHeight(); }
	bool isThumbnailLoaded() const { return thumbnail.isLoaded(); }
	// Readiness includes the real-time crossfade, not just thumbnail delivery.
	bool isPresentationSettled() const
	{
		return state == State::Ready && !transitionPending &&
			   (!transitioning || transitionAlpha() == 0);
	}
	std::function<void()> retry;
	//! Mouse events in the preview's own surface coordinates.
	bool handlePreviewEvent(SDL_Event *event);
	void cancelDrag();
	void resetView()
	{
		view.reset();
		zoom = 1;
		cancelDrag();
	}
	MapPreviewGeometry::Rect mapArea();

  protected:
	Sint32 x = 0, y = 0, w = PreviewSize, h = PreviewSize;
	bool animateChanges = true;
	virtual void paintOverlay(DrawableSurface *, MapPreviewGeometry::Rect);
	MapThumbnail thumbnail;
	// Created on the first visible paint, independently of thumbnail readiness.
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
