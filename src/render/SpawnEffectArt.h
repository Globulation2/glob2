// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SpawnEffects.h"

#include <SDLGraphicContext.h>

#include <map>
#include <memory>

//! Images for the unit spawn effect (SpawnEffects.h), drawn procedurally rather
//! than shipped as assets: a team-colour star with spikes, the soft team-colour
//! glows under the dust cloud, and the white core and glint drawn over a star.
//! View-owned (MapRenderState) and created on first use, one set per team hue.
class SpawnEffectArt
{
public:
	SpawnEffectArt();
	~SpawnEffectArt();
	SpawnEffectArt(const SpawnEffectArt &) = delete;
	SpawnEffectArt &operator=(const SpawnEffectArt &) = delete;

	//! Hue, in degrees, that the effect takes from a team colour.
	static float hueOf(const GAGCore::Color &team);
	//! The light team colour of a spawning unit's silhouette and the swarm glow.
	static GAGCore::Color tint(float hue);

	GAGCore::DrawableSurface &star(float hue);
	//! A radial glow whose centre has HSL `lightness` (0..1); the dust uses two.
	GAGCore::DrawableSurface &glow(float hue, float lightness);
	GAGCore::DrawableSurface &core();
	GAGCore::DrawableSurface &glint();

	//! Draw a sparkle or glow offset from (x, y), at `opacity` of its own.
	void draw(GAGCore::GraphicContext &gfx, float hue, float x, float y, const SpawnEffect::Sparkle &sparkle, float opacity);
	void draw(GAGCore::GraphicContext &gfx, float hue, float x, float y, const SpawnEffect::Glow &glow, float opacity);

private:
	std::map<int, std::unique_ptr<GAGCore::DrawableSurface>> stars, glows;
	std::unique_ptr<GAGCore::DrawableSurface> core_, glint_;
};
