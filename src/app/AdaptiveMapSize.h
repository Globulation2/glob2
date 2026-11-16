// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DeviceProfile.h"
#include "GenerationRequest.h"
#include "GenerationValidation.h"

// The device's default size for a new map (DeviceProfile::newMapSize), applied
// to a setup's generator request. A smaller size is taken only where the
// landscape can still host the colonies at it: Lava shield's coastal towns, for
// example, do not fit four colonies on 128x128, and a default the setup rejects
// would leave the lobby unable to start. Lobby and editor defaults only; nothing
// in the simulation reads this.
inline void applyDeviceMapSize(GenerationRequest &request, int colonies)
{
	const int shift = DeviceProfile::current().newMapSize().shift;
	if (shift == request.wDec && shift == request.hDec)
		return;
	GenerationRequest trial = request;
	trial.wDec = trial.hDec = shift;
	trial.nbTeams = colonies;
	if (validateGenerationRequest(trial, trial.definition()).empty())
		request.wDec = request.hDec = shift;
}
