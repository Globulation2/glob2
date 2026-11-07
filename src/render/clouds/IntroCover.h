// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>

#include <memory>
#include <string>
#include <valarray>
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
		std::valarray<unsigned char> shadow; //!< black, cast down and right
		std::valarray<unsigned char> body;   //!< the bank's coverage
		std::valarray<unsigned char> light;  //!< coverage times tone: creases dark, sunlit tops bright
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

	void draw(GAGCore::DrawableSurface &gfx, int w, int h, Uint64 now, GAGCore::Font *titleFont,
	          GAGCore::Font *playerFont, bool clouds);

	//! The bank at `part` (0 closed, 1 gone) for a w x h screen, `seconds` in.
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
	Layers layers, farLayers;
	std::unique_ptr<GAGCore::DrawableSurface> surface;
	TitleLayout layoutTitle(int w, int h, GAGCore::Font *titleFont, GAGCore::Font *playerFont) const;
	void drawTitle(GAGCore::DrawableSurface &gfx, int w, const TitleLayout &layout, float alpha,
	               GAGCore::Font *titleFont, GAGCore::Font *playerFont);
};
