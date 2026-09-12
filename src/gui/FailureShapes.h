// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GraphicContext.h"
#include "Building.h"

/// A mini stop sign (a red octagon with a bar through the middle), drawn
/// centred on (cx, cy) with half-size r. The building panel puts it next to
/// the tally, the map view over every unit behind that tally, so a player
/// can tell at a glance that a unit was blocked from hiring; the specific
/// reason is spelled out in the panel row's text, not in the shape.
void drawFailureShape(GAGCore::GraphicContext* gfx, int cx, int cy, int r, Building::UnitCantWorkReason reason, const GAGCore::Color& color);

/// The colour every failure marker is drawn in.
GAGCore::Color failureShapeColor();
