// SPDX-License-Identifier: GPL-3.0-or-later
#include "SinglePlayerFlow.h"
#include "Engine.h"
#include "CustomGameScreen.h"
#include "ChooseMapScreen.h"
#include "GameSessionScreen.h"
#include "MessageScreen.h"
#include <Toolkit.h>
#include "scripting/javascript/ScriptCommand.h"
#include "scripting/javascript/ScriptRuntime.h"
#include <cstdlib>
#include <stdexcept>
#include <ApplicationHost.h>
#include <StringTable.h>

void SinglePlayerFlow::launch(GameLoadScreen::Initializer initialize, bool repeatCustom,
							  std::shared_ptr<void> mapFile)
{
	// The stack destroys the choosing screen before this loader reads its map,
	// so mapFile lives until the loader's own entry is released.
	screens.push(
		std::make_unique<GameLoadScreen>(std::move(initialize)),
		[this, repeatCustom, mapFile = std::move(mapFile)](GAGGUI::Screen &screen, int result)
		{
			if (result == 1)
				screens.push(std::make_unique<GameSessionScreen>(
								 screens, static_cast<GameLoadScreen &>(screen).takeEngine()),
							 [this, repeatCustom](GAGGUI::Screen &, int)
							 {
								 if (repeatCustom)
									 custom();
							 });
			else if (result == 2)
			{
                if (std::getenv("GLOB2_STUDIO_PLAYTEST"))
                    GAGCore::ApplicationHost::studioError(
                        static_cast<GameLoadScreen &>(screen).failureMessage());
				auto &strings = *GAGCore::Toolkit::getStringTable();
				screens.push(std::make_unique<MessageScreen>(
								 static_cast<GameLoadScreen &>(screen).failureMessage(),
								 std::vector<std::string>{strings.getString("[ok]")}),
							 [this, repeatCustom](GAGGUI::Screen &, int)
							 {
								 if (repeatCustom)
									 custom();
							 });
			}
			else if (repeatCustom)
				custom();
		});
}

void SinglePlayerFlow::custom(const std::optional<Online::MapPlayRequest> &map)
{
	auto setup = std::make_unique<CustomGameScreen>(screens);
	if (map)
		setup->loadCatalogMap(*map);
	screens.push(std::move(setup),
				 [this](GAGGUI::Screen &screen, int result)
				 {
					 if (result != CustomGameScreen::OK)
						 return;
					 auto &selected = static_cast<CustomGameScreen &>(screen);
					 auto map = selected.getMapHeader();
					 auto players = selected.getGameHeader();
					 auto team = selected.getSelectedColor(0);
					 auto speed = selected.selectedSpeed();
					 // A generated map's bytes are already in memory (CustomGameScreen::generateMap):
					 // read them directly rather than round-tripping through a temporary file. A
					 // premade map still comes from its real file in the library.
					 if (auto bytes = selected.releaseSnapshot())
						 launch([map, players, team, speed, bytes](Engine &engine)
								{ return engine.initCustomFromBytesTask(map, players, team, speed, bytes); },
								true, bytes);
					 else
						 launch([map, players, team, speed, source = selected.sourceFile()](Engine &engine)
								{ return engine.initCustomTask(map, players, team, speed, source); },
								true, nullptr);
				 });
}

void SinglePlayerFlow::load()
{
	screens.push(
		std::make_unique<ChooseMapScreen>("games", "game", true, "replays", "replay", false),
		[this](GAGGUI::Screen &screen, int result)
		{
			if (result != ChooseMapScreen::OK)
				return;
			auto &selected = static_cast<ChooseMapScreen &>(screen);
			const bool replay = selected.getSelectedType() == ChooseMapScreen::REPLAY;
			const auto filename = replay ? selected.getMapHeader().getFileName(false, true)
										 : selected.getMapHeader().getFileName();
			launch(
				[filename, replay](Engine &engine) {
					return replay ? engine.loadReplayTask(filename)
								  : engine.initCustomTask(filename);
				},
				false);
		});
}

void SinglePlayerFlow::replay(const std::string &filename)
{
	launch([filename](Engine &engine) { return engine.loadReplayTask(filename); }, false);
}

void SinglePlayerFlow::studio()
{
    // The browser bridge has already checked the source size, pinned map and setup.
    // Repeat native validation at the engine boundary; no library installation occurs.
    launch([](Engine &engine) -> GAGCore::CooperativeTask {
        const std::string path = "/tmp/studio.map.gz";
        auto map = Engine::loadMapHeader(path);
        if (map.getNumberOfTeams() != 2) throw std::invalid_argument("Studio requires a two-team map");
        const char *seedText = std::getenv("GLOB2_STUDIO_SEED");
        const char *opponentText = std::getenv("GLOB2_STUDIO_OPPONENT");
        if (!seedText || !opponentText) throw std::invalid_argument("Missing Studio setup");
        const std::string seedString(seedText), opponent(opponentText);
        if (seedString.empty() || seedString.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("Invalid Studio seed");
        const auto seed = std::stoull(seedString);
        if (seed > 0xffffffffULL || (opponent != "numbi" && opponent != "nicowar"))
            throw std::invalid_argument("Invalid Studio setup");
        const auto source = Script::readSource("/tmp/studio.js");
        GameHeader header;
        header.setNumberOfPlayers(2);
        header.setRandomSeed(static_cast<Uint32>(seed));
        header.getBasePlayer(0) = BasePlayer(0, "Your AI", 0, BasePlayer::playerTypeFromImplementationID(AI::JAVASCRIPT));
        header.getBasePlayer(1) = BasePlayer(1, opponent, 1, BasePlayer::playerTypeFromImplementationID(opponent == "numbi" ? AI::NUMBI : AI::NICOWAR));
        header.setAIConfig(0, Script::config(source, Script::inspectAI(source).apiVersion));
        co_return co_await engine.initCustomTask(map, header, 0, -1, path);
    }, false);
}
