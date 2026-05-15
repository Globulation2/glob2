// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#include "FertilityCalculatorDialog.h"

#include "FertilityCalculator.h"
#include "GUIProgressBar.h"
#include "GUIText.h"
#include "Map.h"
#include "SDLCompat.h"
#include "StringTable.h"
#include "Toolkit.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

using namespace GAGCore;
using namespace GAGGUI;

namespace
{
	constexpr int kProgressResolution = 1000;
	constexpr Sint64 kFramePeriodMs = 40;
}

FertilityCalculatorDialog::FertilityCalculatorDialog(GraphicContext* parentCtx, Map& map)
	: OverlayScreen(parentCtx, 200, 100), map(map), parentCtx(parentCtx)
{
	addWidget(new Text(0, 20, ALIGN_FILL, ALIGN_LEFT, "standard",
	                   Toolkit::getStringTable()->getString("[Computing Fertility]")));
	percentDone = new Text(0, 40, ALIGN_FILL, ALIGN_LEFT, "menu");
	progress = new ProgressBar(0, 70, 0, ALIGN_FILL, ALIGN_TOP, kProgressResolution);
	addWidget(percentDone);
	addWidget(progress);
	dispatchInit();
}

void FertilityCalculatorDialog::onAction(Widget*, Action, int, int)
{
}

void FertilityCalculatorDialog::runModal()
{
	// Save the screen behind us into a backing surface.
	parentCtx->setClipRect();
	DrawableSurface* background = new DrawableSurface(parentCtx->getW(), parentCtx->getH());
	background->drawSurface(0, 0, parentCtx);

	computeThread = std::thread([this]() {
		FertilityCalculator::compute(map, [this](float p) {
			progressFraction.store(p, std::memory_order_relaxed);
		});
		computeDone.store(true, std::memory_order_release);
	});

	dispatchPaint();

	SDL_Event event;
	while (endValue < 0)
	{
		const Uint64 frameStart = SDL_GetTicks64();
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
				break;
			// Manual integration of cmd+Q and Alt+F4.
			if (event.type == SDL_KEYDOWN)
			{
#ifdef USE_OSX
				if (event.key.keysym.sym == SDLK_q && SDL_GetModState() & KMOD_GUI)
					break;
#endif
#ifdef USE_WIN32
				if (event.key.keysym.sym == SDLK_F4 && SDL_GetModState() & KMOD_ALT)
					break;
#endif
			}
			translateAndProcessEvent(&event);
		}

		refreshProgressDisplay();
		if (computeDone.load(std::memory_order_acquire))
			endValue = 1;

		dispatchPaint();
		parentCtx->drawSurface(0, 0, background);
		parentCtx->drawSurface(decX, decY, getSurface());
		parentCtx->nextFrame();

		const Uint64 frameEnd = SDL_GetTicks64();
		const Sint64 elapsed = static_cast<Sint64>(frameEnd) - static_cast<Sint64>(frameStart);
		SDL_Delay(static_cast<Uint32>(std::max<Sint64>(kFramePeriodMs - elapsed, 0)));
	}

	if (computeThread.joinable())
		computeThread.join();
	delete background;
}

void FertilityCalculatorDialog::refreshProgressDisplay()
{
	const float p = progressFraction.load(std::memory_order_relaxed);
	std::stringstream s;
	s << std::setprecision(3) << (p * 100.0) << "%" << std::endl;
	percentDone->setText(s.str());
	progress->setValue(static_cast<int>(p * 1000.0));
}
