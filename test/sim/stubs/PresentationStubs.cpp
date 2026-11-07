// SPDX-License-Identifier: GPL-3.0-or-later
// Presentation symbols that GlobalContainer.o references and glob2-sim-tests does not
// link (audio, keyboard shortcuts, menu style and themes, unit skins, terrain
// compositing). HeadlessGlobals never calls GlobalContainer::load(), which is where
// all but the theme selection, the key tables and the sprite cache reset run. The
// no-ops below are the calls a headless construction and destruction make; every
// other entry aborts, and a test that reaches one belongs in glob2-engine-tests.

#include "FrontendUI.h"
#include "GameGUIKeyActions.h"
#include "Glob2Style.h"
#include "KeyboardManager.h"
#include "MapEditKeyActions.h"
#include "render/ResourceSprites.h"
#include "SoundMixer.h"
#include "ThemeCatalog.h"
#include "unit/render/UnitSkin.h"
#include "VoiceRecorder.h"
#include "render/terrain/TerrainCompositor.h"

#include <cstdlib>

// Called by GlobalContainer's constructor and destructor, and by HeadlessGlobals.
void Glob2UI::applyThemes(const std::string&, const std::string&) {}
void ResourceSprites::clear() {}
void GameGUIKeyActions::init() {}
void MapEditKeyActions::init() {}

// GlobalContainer::load() and the drawing paths only.
bool Glob2UI::touchPresentation() { std::abort(); }
void initUnitSkins() { std::abort(); }
// SoundMixer.cpp, which defines the real Impl, is not linked into this binary.
struct SoundMixer::Impl {};
SoundMixer::SoundMixer(unsigned, unsigned, bool) { std::abort(); }
SoundMixer::~SoundMixer() { std::abort(); }
bool SoundMixer::loadTracks(const std::vector<std::pair<std::string, MusicTrack>>&) { std::abort(); }
void SoundMixer::setNextTrack(MusicTrack, bool) { std::abort(); }
VoiceRecorder::VoiceRecorder() { std::abort(); }
VoiceRecorder::~VoiceRecorder() { std::abort(); }
KeyboardManager::KeyboardManager(ShortcutMode) { std::abort(); }
void KeyboardManager::loadDefaultShortcuts() { std::abort(); }
bool KeyboardManager::saveKeyboardLayout() const { std::abort(); }
Glob2Style::Glob2Style() { std::abort(); }
Glob2Style::~Glob2Style() { std::abort(); }
void Glob2Style::drawTextButtonBackground(GAGCore::DrawableSurface*, int, int, int, int, unsigned) { std::abort(); }
void Glob2Style::drawFrame(GAGCore::DrawableSurface*, int, int, int, int, unsigned) { std::abort(); }
void Glob2Style::drawProgressBar(GAGCore::DrawableSurface*, int, int, int, int, int) { std::abort(); }
int Glob2Style::getStyleMetric(StyleMetrics) { std::abort(); }
TerrainVisual::Compositor::Compositor(Catalog, std::shared_ptr<const MapAssetBundle> assets)
	: customSprites(std::move(assets)) { std::abort(); }
