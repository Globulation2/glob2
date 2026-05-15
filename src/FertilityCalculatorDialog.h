// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#pragma once

#include "GUIBase.h"
#include <atomic>
#include <thread>

class Map;
namespace GAGGUI
{
	class Text;
	class ProgressBar;
}
namespace GAGCore
{
	class DrawableSurface;
}

/// Modal dialog that shows fertility-computation progress while the work runs
/// on a background thread.
class FertilityCalculatorDialog : public GAGGUI::OverlayScreen
{
public:
	FertilityCalculatorDialog(GAGCore::GraphicContext* parentCtx, Map& map);
	~FertilityCalculatorDialog() override = default;
	void onAction(GAGGUI::Widget* source, GAGGUI::Action action, int par1, int par2) override;

	/// Modal: blocks until the background computation finishes.
	void runModal();

private:
	void refreshProgressDisplay();

	Map& map;
	GAGCore::GraphicContext* parentCtx;

	GAGGUI::Text* percentDone;
	GAGGUI::ProgressBar* progress;

	std::thread computeThread;
	std::atomic<float> progressFraction{0.f};
	std::atomic<bool> computeDone{false};
};
