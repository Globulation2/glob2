// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "../render/MapCopies.h"
#include <math.h>
#include <stdlib.h>


#include "Game.h"
#include "GameGUI.h"
#include "GameGUIViewport.h"
#include "GlobalContainer.h"
#include "ParticleCrossfade.h"

namespace
{
	// Map tiles are 32 screen pixels; building-relative particle offsets use
	// the half-tile so `size * HALF_TILE_PX` lands on the building's center.
	constexpr int TILE_PX = 32;
	constexpr int HALF_TILE_PX = 16;

	// Particle animation frames in globalContainer->particles used by both
	// building smoke and turret muzzle flashes (endImg is one past the last).
	constexpr int PARTICLE_START_IMG = 0;
	constexpr int PARTICLE_END_IMG = 2;

	// Damaged-building smoke: heavy smoke below 20% hp (every 2nd tick),
	// light smoke below 50% hp (every 4th tick).
	constexpr float SMOKE_HEAVY_HP_RATIO = 0.2f;
	constexpr float SMOKE_LIGHT_HP_RATIO = 0.5f;
	constexpr int SMOKE_LIFESPAN_TICKS = 50;

	// Turret muzzle flash: emitted every 2nd tick during the first
	// TURRET_FLASH_DURATION_TICKS after a shot.
	constexpr int TURRET_FLASH_DURATION_TICKS = 6;
	constexpr int TURRET_FLASH_LIFESPAN_TICKS = 30;

	//! Draw one particle sprite centered on (x, y). Fetches the dimensions of
	//! the same image index being drawn — the crossfade frames may differ in
	//! size, so the caller must not reuse another frame's width/height.
	void drawCenteredParticleSprite(float x, float y, int img, Uint8 alpha)
	{
		Sprite* sprite = globalContainer->particles;
		const int w = sprite->getW(img);
		const int h = sprite->getH(img);
		globalContainer->gfx->drawSprite(x - 0.5f * w, y - 0.5f * h, sprite, img, alpha);
	}
}

void GameGUI::drawParticles(void)
{
	for (ParticleSet::iterator it = particles.begin(); it != particles.end(); )
	{
		Particle* p = *it;

		// delete old particles
		if (p->age >= p->lifeSpan)
		{
			ParticleSet::iterator oldIt = it;
			++it;

			delete *oldIt;
			particles.erase(oldIt);

			continue;
		}
		else
			p->age++;

		// do stupid physics
		p->x += p->vx;
		p->y += p->vy;
		p->vx += p->ax;
		p->vy += p->ay;

		globalContainer->particles->setBaseColor(p->color);

		// crossfade between the current animation frame and the next one
		const ParticleCrossfade cf = computeParticleCrossfade(p->startImg, p->endImg, p->age, p->lifeSpan);
		int radius = std::max(globalContainer->particles->getW(cf.frameA), globalContainer->particles->getH(cf.frameA));
		if (cf.hasFrameB)
			radius = std::max({radius, globalContainer->particles->getW(cf.frameB), globalContainer->particles->getH(cf.frameB)});
		const float x=MapCamera::wrap(p->x-viewportX*32,game.map.getW()*32);
		const float y=MapCamera::wrap(p->y-viewportY*32,game.map.getH()*32);
		forEachMapCopy(int(x)-radius, int(y)-radius, int(x)+radius, int(y)+radius,
			game.map.getW()*32, game.map.getH()*32, game.map.displayViewportW ? game.map.displayViewportW : globalContainer->gfx->getW()-GAME_GUI_RIGHT_MENU_WIDTH,
			game.map.displayViewportH ? game.map.displayViewportH : globalContainer->gfx->getH(), [&](int dx, int dy) {
				drawCenteredParticleSprite(x+dx, y+dy, cf.frameA, cf.alphaA);
				if (cf.hasFrameB)
					drawCenteredParticleSprite(x+dx, y+dy, cf.frameB, cf.alphaB);
			});

		++it;
	}
}

void GameGUI::generateNewParticles(std::set<Building*> *visibleBuildings)
{
	for (std::set<Building*>::iterator it = visibleBuildings->begin(); it != visibleBuildings->end(); ++it)
	{
		Building* building = *it;
		BuildingType* type = building->type;
		int x, y;
		x=displayedPosX(*building)*32;y=displayedPosY(*building)*32;

		if (!type->isBuildingSite)
		{
			// damaged building smoke
			float hpRatio = (float)building->hp / (float)type->hpMax;
			if (
				(hpRatio < SMOKE_HEAVY_HP_RATIO && ((game.stepCounter & 0x1) == 0)) ||
				(hpRatio < SMOKE_LIGHT_HP_RATIO && ((game.stepCounter & 0x3) == 0))
			)
			{
				Particle* p = new Particle;
				p->x = x + type->width * HALF_TILE_PX;
				p->y = y + type->height * HALF_TILE_PX;
				if (hpRatio < SMOKE_HEAVY_HP_RATIO)
				{
					p->vx = 0.5f - (float)rand() / (float)RAND_MAX;
					p->vy = - 3.f * (float)rand() / (float)RAND_MAX;
				}
				else
				{
					p->vx = 0.3f - (float)rand() / (float)RAND_MAX;
					p->vy = - 1.8f * (float)rand() / (float)RAND_MAX;
				}
				p->ax = 0.f;
				p->ay = -0.01f;
				p->age = 0;
				p->lifeSpan = SMOKE_LIFESPAN_TICKS;
				p->startImg = PARTICLE_START_IMG;
				p->endImg = PARTICLE_END_IMG;
				p->color = building->owner->color;
				particles.insert(p);
			}

			// turret firing
			if (building->lastShootStep != Building::LAST_SHOOT_STEP_NEVER)
			{
				if ((game.stepCounter - building->lastShootStep < TURRET_FLASH_DURATION_TICKS) && (game.stepCounter % 2 == 0))
				{
					float norm = building->lastShootSpeedX * building->lastShootSpeedX + building->lastShootSpeedY * building->lastShootSpeedY;
					float w2 = type->width * HALF_TILE_PX;
					float h2 = type->height * HALF_TILE_PX;
					float dx = (building->lastShootSpeedX * w2) / sqrt(norm);
					float dy = (building->lastShootSpeedY * h2) / sqrt(norm);
					Particle* p = new Particle;
					p->x = x + w2 + dx;
					p->y = y + h2 + dy;
					p->vx = 0.3f - (float)rand() / (float)RAND_MAX;
					p->vy = - 1.2f * (float)rand() / (float)RAND_MAX;
					p->ax = 0.f;
					p->ay = -0.02f;
					p->age = 0;
					p->lifeSpan = TURRET_FLASH_LIFESPAN_TICKS;
					p->startImg = PARTICLE_START_IMG;
					p->endImg = PARTICLE_END_IMG;
					p->color = building->owner->color;
					particles.insert(p);
				}
			}
		}
	}
}

void GameGUI::moveParticles(int oldViewportX, int viewportX, int oldViewportY, int viewportY)
{
	// Particles now retain world positions; camera changes need no compensation.
}
