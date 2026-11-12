// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "UnitEffects.h"

#include <SDLGraphicContext.h>

#include <map>
#include <memory>

//! Images for the unit spawn effect (UnitEffects.h), drawn procedurally rather
//! than shipped as assets: a team-colour star with spikes, the soft team-colour
//! glows under the dust cloud, and the white core and glint drawn over a star.
//! View-owned (MapRenderState) and created on first use, one set per team hue.
class UnitEffectArt
{
public:
	UnitEffectArt();
	~UnitEffectArt();
	UnitEffectArt(const UnitEffectArt &) = delete;
	UnitEffectArt &operator=(const UnitEffectArt &) = delete;

	//! Hue, in degrees, that the effect takes from a team colour.
	static float hueOf(const GAGCore::Color &team);
	//! The light team colour of a spawning unit's silhouette and the swarm glow.
	static GAGCore::Color tint(float hue);
	//! The colour of a serviced unit's silhouette and of its building's icons.
	static GAGCore::Color serviceTint(UnitEffect::Kind kind);

	GAGCore::DrawableSurface &star(float hue);
	//! A radial glow whose centre has HSL `lightness` (0..1); the dust uses two.
	GAGCore::DrawableSurface &glow(float hue, float lightness);
	GAGCore::DrawableSurface &core();
	GAGCore::DrawableSurface &glint();

	//! Draw a sparkle or glow offset from (x, y), at `opacity` of its own.
	void draw(GAGCore::GraphicContext &gfx, float hue, float x, float y, const UnitEffect::Sparkle &sparkle, float opacity);
	void draw(GAGCore::GraphicContext &gfx, float hue, float x, float y, const UnitEffect::Glow &glow, float opacity);
	//! A converted unit's soft heart in `hue`, whole or one half of it broken
	//! along a jagged crack; `saturation` scales its colour.
	GAGCore::DrawableSurface &heart(UnitEffect::HeartPart part, float hue, float saturation);

	//! Draw one of a building's map icons, glyph `glyph` of `icons` (mapicon), in
	//! the service's colour over a soft shadow. `pixelsPerUnit` picks the largest
	//! pre-sized frame that fits on screen rather than scaling a large one down.
	void draw(GAGCore::GraphicContext &gfx, GAGCore::Sprite &icons, int glyph, UnitEffect::Kind kind,
		float x, float y, const UnitEffect::Icon &icon, float opacity, float pixelsPerUnit);
	//! Draw part of a heart offset from (x, y), as a converted unit's icon.
	void drawHeart(GAGCore::GraphicContext &gfx, UnitEffect::HeartPart part, float hue, float saturation, float x, float y,
		const UnitEffect::Icon &icon, float opacity);

private:
	std::map<int, std::unique_ptr<GAGCore::DrawableSurface>> stars, glows;
	std::map<int, std::unique_ptr<GAGCore::DrawableSurface>> hearts;
	std::unique_ptr<GAGCore::DrawableSurface> core_, glint_;
};
