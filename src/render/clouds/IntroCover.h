// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>

#include "CloudCover.h"

#include <memory>
#include <string>
#include <vector>

namespace GAGCore
{
class DrawableSurface;
class Font;
}

//! A three-second bank of cloud that covers the screen as a game starts, then
//! parts to reveal the map. Presentation only: the simulation and the map
//! renderer keep running underneath the whole time, so their caches warm up
//! before the player sees them. Once the bank has gone, what remains is the
//! game's normal cloud layer (DynamicClouds).
class IntroCover
{
public:
	static constexpr Uint32 COVER_MS = 1100; //!< closed, showing the title
	static constexpr Uint32 PART_MS = 1900;  //!< parting
	static constexpr Uint32 TOTAL_MS = COVER_MS + PART_MS;

	struct Player
	{
		std::string name;
		Uint8 r = 255, g = 255, b = 255;
	};

	//! The map name and players shown on the cover.
	struct Title
	{
		std::string map;
		std::vector<Player> players;
	};

	//! The bank's alpha layers as a w x h texture that is stretched over the
	//! whole screen. Texel (i, j) covers the screen around ((i + .5) * W / w, ...).
	struct Layers
	{
		int w = 0, h = 0;
		std::vector<unsigned char> shadow; //!< black, cast down and right
		std::vector<unsigned char> body;   //!< the bank's coverage
		std::vector<unsigned char> light;  //!< coverage times tone: creases dark, sunlit tops bright
	};

	//! One layer of the bank. The near layer rushes past as the view drops
	//! through it; the far one grows slowly and thins later.
	struct Depth
	{
		float zoom = .7f;   //!< exp(zoom) is how far the layer has grown by the end
		float lead = 1.3f;  //!< how much sooner than `part` the layer clears
		float wind = 1;     //!< drift speed
		float size = 1;     //!< billow size
		int seed = 0;
	};

	IntroCover();
	~IntroCover();
	IntroCover(IntroCover &&) noexcept;
	IntroCover &operator=(IntroCover &&) noexcept;

	void begin(Uint64 now, Title title);
	void reset();
	//! Jump to parting now (a click or key press).
	void skip(Uint64 now);

	bool active(Uint64 now) const { return start && now - start < end; }
	//! Gameplay input is withheld while the bank is still mostly closed.
	bool blocksInput(Uint64 now) const { return start && part(now) < .35f; }
	//! 0 while covered, rising to 1 as the bank parts.
	float part(Uint64 now) const;
	//! The title's opacity: full from the first frame, fading as the bank starts to part.
	float titleOpacity(Uint64 now) const;

	//! Get ready to draw on `gfx`, w x h, right after begin(), so that the
	//! first frames do not wait for the renderer or for the noise.
	void prepare(GAGCore::DrawableSurface &gfx, int w, int h);
	void draw(GAGCore::DrawableSurface &gfx, int w, int h, Uint64 now, GAGCore::Font *titleFont,
	          GAGCore::Font *playerFont, bool clouds);

	//! The darkening behind the title: an ellipse of radii (rx, ry) around (cx, cy).
	struct Shade
	{
		float alpha = 0;
		float cx = 0, cy = 0, rx = 1, ry = 1;
	};

	//! Both layers and the title's shade as straight-alpha ARGB texels, the
	//! size of the layers, for a w x h screen with the bank at `part`.
	static void composite(int w, int h, float part, const Layers &nearLayers, const Layers &farLayers,
	                      const Shade &shade, Uint32 *pixels, int pitch);

	//! The near layer rushes past; the far one, hazier, drifts slowly behind it.
	static Depth nearDepth() { return {}; }
	static Depth farDepth() { return {.25f, 1, .55f, .7f, 7}; }

	//! One layer's noise on a coarse screen-space lattice: the rank of each
	//! point among all of them in [0, 1), and the puffs' height there. Point
	//! (i, j) sits at (ox + i * cell, oy + j * cell). The lattice covers the
	//! screen with margins for the shadow and light offsets.
	struct Lattice
	{
		int w = 0, h = 0;          //!< the screen it was made for
		int cw = 0, ch = 0, cell = 1;
		float ox = 0, oy = 0;
		float shadowX = 0, shadowY = 0, lightX = 0, lightY = 0;
		std::vector<float> ranks, heights;
	};
	//! Ranks at least `base + (1 - d) * bias` become cloud, where d is the
	//! distance from the screen's centre as a fraction of the corners'.
	struct Threshold
	{
		float base = 0, bias = 0;
	};
	static Threshold threshold(float part, const Depth &depth);
	//! The layer's lattice at `part` (0 closed, 1 gone) for a w x h screen, `seconds` in.
	static void computeLattice(int w, int h, double seconds, float part, Lattice &out, const Depth &depth);
	//! The lattice carved into cloud at `threshold`, as texels.
	static void computeLayers(const Lattice &lattice, Threshold threshold, Layers &out);
	//! Both layers' lattices and the title's shade at `part`, for a renderer
	//! that draws the bank per pixel; `packed` holds the lattice texture. Its
	//! lattice is null when the two lattices were made for different screens.
	static GAGCore::CloudCoverFrame perPixelFrame(const Lattice &nearLattice, const Lattice &farLattice, float part,
	                                              const Shade &shade, std::vector<Uint16> &packed);
	//! Both steps at once.
	static void computeLayers(int w, int h, double seconds, float part, Layers &out, const Depth &depth);
	static void computeLayers(int w, int h, double seconds, float part, Layers &out);

private:
	struct TitleLayout
	{
		std::vector<std::vector<const Player *>> rows;
		std::vector<int> widths;
		int gap = 0, titleW = 0, titleH = 0, rowH = 0, blockW = 0, blockH = 0, top = 0;
	};

	Uint64 start = 0;
	Uint32 partFrom = COVER_MS, end = TOTAL_MS;
	Title title;
	//! The newest lattices of both layers, and the threads making the next ones.
	Lattice nearLattice, farLattice;
	struct Workers;
	std::unique_ptr<Workers> workers;
	Uint64 lastDraw = 0;
	std::vector<Uint16> packed;
	//! Set once the renderer cannot draw the bank per pixel; then it is a texture.
	bool textureFallback = false;
	Layers layers, farLayers;
	std::unique_ptr<GAGCore::DrawableSurface> surface;
	bool startWorkers();
	//! Ask the workers for the next frame's lattices.
	void requestNext(int w, int h, Uint64 now);
	TitleLayout layoutTitle(int w, int h, GAGCore::Font *titleFont, GAGCore::Font *playerFont) const;
	void drawTitle(GAGCore::DrawableSurface &gfx, int w, const TitleLayout &layout, float alpha,
	               GAGCore::Font *titleFont, GAGCore::Font *playerFont);
};
