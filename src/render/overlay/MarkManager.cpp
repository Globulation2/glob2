// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "render/MapCopies.h"
#include "GameGUIViewport.h"
#include "MarkManager.h"
#include <MapCamera.h>
#include "Utilities.h"
#include "GlobalContainer.h"
#include "render/scene/Scene.h"
#include <cmath>
#include <numbers>

Mark::Mark(int px, int py, GAGCore::Color color, int time)
  : showTicks(time), totalTime(time), px(px), py(py), color(color)
{

}



Mark::Mark()
{
}



void Mark::draw(int x, int y, float scale) const
{
	// Pulsing-circle radius. showTicks counts down from totalTime to 0 over
	// the mark's lifetime, so lifetime_fraction ramps 1.0 -> 0.0. The phase
	// sweeps a full 2π over that lifetime; |sin(phase)| is the oscillation
	// envelope, and the trailing lifetime_fraction factor decays the pulse
	// to zero as the mark expires. amplitude is in pixels at scale 1.0.
	const double lifetime_fraction = static_cast<double>(showTicks) / totalTime;
	const double phase = lifetime_fraction * 2.0 * std::numbers::pi_v<double>;
	const double amplitude = totalTime / 2.0;
	const double ray = std::abs(std::sin(phase)) * amplitude * lifetime_fraction * scale;

	int pixel_ray = static_cast<int>(ray);
	int line_length = static_cast<int>(MARK_LINE_LENGTH_PX * scale);
	int line_pos = static_cast<int>(MARK_LINE_OFFSET_PX * scale);
	globalContainer->gfx->drawCircle(x, y, pixel_ray, color);
	globalContainer->gfx->drawHorzLine(x + pixel_ray-line_pos+1, y, line_length, color.r, color.g, color.b);
	globalContainer->gfx->drawHorzLine(x-pixel_ray-line_pos, y, line_length, color.r, color.g, color.b);
	globalContainer->gfx->drawVertLine(x, y+pixel_ray-line_pos+1, line_length, color.r, color.g, color.b);
	globalContainer->gfx->drawVertLine(x, y-pixel_ray-line_pos, line_length, color.r, color.g, color.b);
}



void Mark::drawInMinimap(int s, int local, int x, int y, const PresentationFrame& scene) const
{
	int mMax;
	int szX, szY;
	int decX, decY;
	int nx, ny;
	
	Utilities::computeMinimapData(s, scene.map.getW(), scene.map.getH(), &mMax, &szX, &szY, &decX, &decY);
	nx = px;
	ny = py;
	if (local >= 0 && local < scene.entities.teamCount)
	{
		nx = (px - scene.entities.teams[local].startX + (scene.map.getW() >> 1)) & scene.map.getMaskW();
		ny = (py - scene.entities.teams[local].startY + (scene.map.getH() >> 1)) & scene.map.getMaskH();
	}

	nx = (nx*s)/mMax;
	ny = (ny*s)/mMax;
	nx += x + decX;
	ny += y + decY;
	
	draw(nx, ny, 1.0);
}



void Mark::drawInMainView(int viewportX, int viewportY, const PresentationFrame& scene, const MapCamera *camera) const
{
	int nx, ny;
	scene.map.mapCaseToDisplayable(px, py, &nx, &ny, viewportX, viewportY);
	
	auto *gfx = globalContainer->gfx;
	int clipX, clipY, clipW, clipH;
	gfx->getClipRect(&clipX, &clipY, &clipW, &clipH);
	const int width = camera ? int(std::ceil(camera->visibleW()+camera->fractionX())) : gfx->getW()-GAME_GUI_RIGHT_MENU_WIDTH;
	const int height = camera ? int(std::ceil(camera->visibleH()+camera->fractionY())) : gfx->getH();
	if (camera)
		gfx->beginMapTransform(camera->zoom, camera->offsetX-camera->fractionX()*camera->zoom,
			camera->offsetY-camera->fractionY()*camera->zoom,0,16,camera->width,camera->height-16);
	gfx->setClipRect(0, 0, width, gfx->getH());
	const int radius = totalTime + 2*MARK_LINE_LENGTH_PX;
	forEachMapCopy(nx-radius, ny-radius, nx+radius, ny+radius,
		scene.map.getW()*32, scene.map.getH()*32, width, height, [&](int dx, int dy) {
			draw(nx+dx, ny+dy, 2.0);
		});
	if (camera) gfx->endMapTransform();
	gfx->setClipRect(clipX, clipY, clipW, clipH);
}



MarkManager::MarkManager()
{

}



void MarkManager::drawAll(int localTeam, int minimapX, int minimapY, int minimapSize, int viewportX, int viewportY, const PresentationFrame& scene, const MapCamera *camera)
{
	for(std::vector<Mark>::iterator i=marks.begin(); i!=marks.end();)
	{
		i->tick();
		if(i->expired())
		{
			i = marks.erase(i);
			continue;
		}
		i->drawInMinimap(minimapSize, localTeam, minimapX, minimapY, scene);
		i->drawInMainView(viewportX, viewportY, scene, camera);
		++i;
	}
}



void MarkManager::addMark(const Mark& mark)
{
	marks.push_back(mark);
}

