// SPDX-License-Identifier: GPL-3.0-or-later
// ReplayWriter::init takes a GameGUI& only to write the game header, and
// ReplayReader::loadReplay(stream, true) constructs one only to skip that header.
// The unit tests use neither path, so these link-level stand-ins produce the same
// mangled names as the real out-of-line members without the GameGUI.h surface.
#include "GameGUIStubs.h"
#include <string>
GameGUI::GameGUI(bool) {}
GameGUI::~GameGUI() {}
bool GameGUI::load(GAGCore::InputStream*, bool) { return false; }
void GameGUI::save(GAGCore::OutputStream*, const std::string, DeferredGameSHA1*) {}
