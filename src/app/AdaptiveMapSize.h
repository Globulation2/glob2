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

// The device's default number of colonies for a new game (the player's
// included), applied to a setup's generator request: two on the serial
// runtime, so the player faces one computer opponent. Taken only where the
// landscape accepts it; elsewhere, and on every other device, the request
// keeps its own default. Apply it before applyDeviceMapSize, which checks the
// size against the colonies.
inline void applyDeviceColonies(GenerationRequest &request)
{
	const int colonies = DeviceProfile::current().newGameColonies();
	if (!colonies || colonies == request.nbTeams)
		return;
	GenerationRequest trial = request;
	trial.nbTeams = colonies;
	if (validateGenerationRequest(trial, trial.definition()).empty())
		request.nbTeams = colonies;
}
