// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <cstdint>
#include <math.h>

//! Fully opaque alpha value for particle sprite blending.
constexpr int PARTICLE_ALPHA_OPAQUE = 255;

//! Which sprite frames to draw for one particle this tick, and at what opacity.
//! frameA is always drawn; frameB (the next animation frame, blending in) is
//! drawn only while hasFrameB is true. alphaA + alphaB == PARTICLE_ALPHA_OPAQUE,
//! so the crossfade keeps constant total opacity. Each frame must be centered
//! using its *own* sprite dimensions — consecutive frames may differ in size.
struct ParticleCrossfade
{
	int frameA;       //!< sprite index of the fading-out frame
	uint8_t alphaA;   //!< opacity of frameA (255 = opaque)
	bool hasFrameB;   //!< whether the blend-in frame should be drawn
	int frameB;       //!< sprite index of the fading-in frame (frameA + 1)
	uint8_t alphaB;   //!< opacity of frameB
};

//! Compute the crossfade state of a particle animation: the particle linearly
//! interpolates from startImg at age 0 toward endImg at age lifeSpan, drawing
//! the current frame fading out and the next frame fading in. The blend-in
//! frame is suppressed once it would reach endImg (endImg itself is one past
//! the last drawable frame). Ages and lifeSpan are in GUI ticks. Pure eye-candy
//! rendering math — float use is fine here, this never touches the simulation.
inline ParticleCrossfade computeParticleCrossfade(int startImg, int endImg, int age, int lifeSpan)
{
	// C++ original: GameGUIParticles.cpp drawParticles() interpolation
	const float img = (float)startImg + (float)((endImg - startImg) * age) / ((float)lifeSpan + 1);
	const uint8_t alpha = (uint8_t)((float)PARTICLE_ALPHA_OPAQUE * (img - truncf(img)));

	ParticleCrossfade c;
	c.frameA = (int)img;
	c.alphaA = (uint8_t)(PARTICLE_ALPHA_OPAQUE - alpha);
	c.frameB = c.frameA + 1;
	c.alphaB = alpha;
	c.hasFrameB = (c.frameB < endImg);
	return c;
}
