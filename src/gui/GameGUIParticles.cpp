// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <stdlib.h>

#include <GraphicContext.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"

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

		// get image
		float img = (float)p->startImg + (float)((p->endImg - p->startImg) * p->age) / ((float)p->lifeSpan + 1);
		Uint8 alpha = (Uint8)(255.f * (img - truncf(img)));
		int imgA = (int)img;

		globalContainer->particles->setBaseColor(p->color);

		// first image
		int w = globalContainer->particles->getW(imgA);
		int h = globalContainer->particles->getH(imgA);
		globalContainer->gfx->drawSprite(p->x - 0.5f * w, p->y - 0.5f * h, globalContainer->particles, imgA, 255-alpha);

		// second image
		int imgB = imgA + 1;
		if (imgB < p->endImg)
		{
			w = globalContainer->particles->getW(imgA);
			h = globalContainer->particles->getH(imgA);
			globalContainer->gfx->drawSprite(p->x - 0.5f * w, p->y - 0.5f * h, globalContainer->particles, imgB, alpha);
		}

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
		game.map.mapCaseToDisplayable(displayedPosX(*building), displayedPosY(*building), &x, &y, viewportX, viewportY);

		if (!type->isBuildingSite)
		{
			// damaged building smoke
			float hpRatio = (float)building->hp / (float)type->hpMax;
			if (
				(hpRatio < 0.2 && ((game.stepCounter & 0x1) == 0)) ||
				(hpRatio < 0.5 && ((game.stepCounter & 0x3) == 0))
			)
			{
				Particle* p = new Particle;
				p->x = x + type->width * 16;
				p->y = y + type->height * 16;
				if (hpRatio < 0.2)
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
				p->lifeSpan = 50;
				p->startImg = 0;
				p->endImg = 2;
				p->color = building->owner->color;
				particles.insert(p);
			}

			// turret firing
			if (building->lastShootStep != Building::LAST_SHOOT_STEP_NEVER)
			{
				if ((game.stepCounter - building->lastShootStep < 6) && (game.stepCounter % 2 == 0))
				{
					float norm = building->lastShootSpeedX * building->lastShootSpeedX + building->lastShootSpeedY * building->lastShootSpeedY;
					float w2 = type->width * 16;
					float h2 = type->height * 16;
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
					p->lifeSpan = 30;
					p->startImg = 0;
					p->endImg = 2;
					p->color = building->owner->color;
					particles.insert(p);
				}
			}
		}
	}
}

void GameGUI::moveParticles(int oldViewportX, int viewportX, int oldViewportY, int viewportY)
{
	if ((viewportX==oldViewportX) && (viewportY==oldViewportY))
		return;

	int dx = viewportX - oldViewportX;
	if (dx > game.map.getW() / 2)
		dx -= game.map.getW();
	else if (dx < -game.map.getW() / 2)
		dx += game.map.getW();

	int dy = viewportY - oldViewportY;
	if (dy > game.map.getH() / 2)
		dy -= game.map.getH();
	else if (dy < -game.map.getH() / 2)
		dy += game.map.getH();

	for (ParticleSet::iterator it = particles.begin(); it != particles.end(); ++it)
	{
		Particle* p = *it;
		p->x -= dx * 32;
		p->y -= dy * 32;
	}
}
