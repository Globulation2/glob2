// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CommandLine.h"
#include <ostream>
#include <string>
#include <vector>
#include <SDL3/SDL_stdinc.h>

class Game;

// Versioned, production command entry points. Return -1 when another command family handles the request.
int runHeadlessCommand(const Cli::Request &request);
int runMapStudy(int argc, char **argv);
namespace Headless
{
std::string quote(const std::string &value);
void writeJson(const std::string &path, const std::string &json);
/// The "players" ... "unresolved" members of a game result.json (no braces), as
/// game run and match verify write them. eliminatedTicks is per team, -1 if alive.
void playersAndTeamsJson(std::ostream &result, Game &game, const std::vector<Sint32> &eliminatedTicks);
/// Writes <directory>/artifacts.json listing every file in it.
void writeManifest(const std::string &directory);
}
/// match verify RECORD --map-file FILE --output-dir DIR (src/app/cli/VerifyMatch.cpp).
int runVerifyMatch(const Cli::Request &request);
/// online turn-client ASSIGNMENT --map-file FILE --output-dir DIR (src/app/cli/TurnClientCommand.cpp):
/// a headless online player connected to a relay.
int runTurnClient(const Cli::Request &request);
