// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ostream>
#include <string>
#include <vector>
#include <SDL3/SDL_stdinc.h>

class Game;

// Versioned, production command entry points. Return -1 when this is a legacy invocation.
int runHeadlessCommand(int argc, char **argv);
int runMapStudy(int argc, char **argv);
namespace Headless
{
std::string quote(const std::string &value);
void writeJson(const std::string &path, const std::string &json);
/// The "players" ... "unresolved" members of a game result.json (no braces), as
/// --run-game and --verify-match write them. eliminatedTicks is per team, -1 if alive.
void playersAndTeamsJson(std::ostream &result, Game &game, const std::vector<Sint32> &eliminatedTicks);
/// Writes <directory>/artifacts.json listing every file in it.
void writeManifest(const std::string &directory);
}
/// --verify-match <record> --map <file> --out <dir> (src/VerifyMatch.cpp).
int runVerifyMatch(int argc, char **argv);
/// --turn-client <assignment.json> --map <file> --out <dir> (src/TurnClientCommand.cpp):
/// a headless online player connected to a relay.
int runTurnClient(int argc, char **argv);
