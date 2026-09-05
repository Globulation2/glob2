// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#include "FertilityCalculatorDialog.h"

#include "FertilityCalculator.h"
#include "GUIProgressBar.h"
#include "GUIText.h"
#include "Map.h"
#include "StringTable.h"
#include "Toolkit.h"

#include <iomanip>
#include <sstream>

using namespace GAGCore;
using namespace GAGGUI;

namespace
{
	constexpr int kProgressResolution = 1000;
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

void FertilityCalculatorDialog::onTimer(Uint32)
{
	refreshProgressDisplay();
	if (computeDone.load(std::memory_order_acquire))
		endValue = 1;
}

void FertilityCalculatorDialog::runModal()
{
	computeThread = std::thread([this]() {
		FertilityCalculator::compute(map, [this](float p) {
			progressFraction.store(p, std::memory_order_relaxed);
		});
		computeDone.store(true, std::memory_order_release);
	});

	executeModal(parentCtx);

	if (computeThread.joinable())
		computeThread.join();
}

void FertilityCalculatorDialog::refreshProgressDisplay()
{
	const float p = progressFraction.load(std::memory_order_relaxed);
	std::stringstream s;
	s << std::setprecision(3) << (p * 100.0) << "%";
	percentDone->setText(s.str());
	progress->setValue(static_cast<int>(p * kProgressResolution));
}
