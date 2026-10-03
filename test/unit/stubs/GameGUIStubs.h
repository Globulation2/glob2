// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Stream.h>
#include <string>

// Link-level stand-in for GameGUI (see GameGUIStubs.cpp): the replay reader and
// writer take one only to skip or write the game header, which no unit test does.
struct DeferredGameSHA1;
class Game;
class GameGUI
{
public:
  Game *replayTelemetryGame();
  explicit GameGUI(bool persistPreferences = true);
  ~GameGUI();
  bool load(GAGCore::InputStream *stream, bool ignoreGUIData = false);
  void save(GAGCore::OutputStream *stream, const std::string name,
			DeferredGameSHA1 *deferredSHA1 = nullptr);
};
