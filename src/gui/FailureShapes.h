// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GraphicContext.h"
#include "Building.h"

/// One distinct outline per Building::UnitCantWorkReason, drawn centred on
/// (cx, cy) with half-size r. The building panel puts it next to the tally, the
/// map view over every unit behind that tally, so a player can tell which
/// units a selected building could not hire and why.
void drawFailureShape(GAGCore::GraphicContext* gfx, int cx, int cy, int r, Building::UnitCantWorkReason reason, const GAGCore::Color& color);

/// The colour every failure marker is drawn in.
GAGCore::Color failureShapeColor();
