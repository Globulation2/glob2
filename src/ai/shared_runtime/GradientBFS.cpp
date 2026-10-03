// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// This file holds the small, Map-free pieces of the AISharedRuntime gradient system,
// kept separate so that determinism tests for `Gradient::expand_bfs` can link
// without dragging in the full game (Map, Game, GlobalContainer, ...).

#include <PerformanceTelemetry.h>
#include "shared_runtime/Runtime.h"

#include "field/UniformTraversal.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Gradients;


GradientInfo::GradientInfo()
{
	needs_updated=indeterminate;
}


GradientInfo::~GradientInfo()
{

}


Gradient::Gradient(const GradientInfo& gi)
{
	gradient_info=gi;
	width=0;
}


void Gradient::expand_bfs(field::Frontier& frontier)
{
	PERF_SCOPE_TIME(AIGradient);
	field::expandDistances(gradient,frontier,
		{width,static_cast<int>(gradient.size())/width},field::Surrounding,Sint16(0));
	frontier.clear();
}
