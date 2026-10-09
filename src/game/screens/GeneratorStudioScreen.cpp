// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorStudioScreen.h"
#include "GeneratorPackage.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "Game.h"
#include "Team.h"
#include "AI.h"
#include "Player.h"
#include "Version.h"
#include "SimRevision.h"
#include "SimVersion.h"
#include "Sha256.h"
#include <ApplicationHost.h>
#include <BinaryStream.h>
#include <ThreadSupport.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <limits>

namespace
{
std::string read(const char *path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		throw std::invalid_argument("Missing generator Studio input");
	std::string bytes;
	char block[8192];
	while (in.read(block, sizeof(block)) || in.gcount())
	{
		bytes.append(block, static_cast<size_t>(in.gcount()));
		if (bytes.size() > 262144)
			throw std::invalid_argument("Generator Studio input exceeds 256 KiB");
	}
	if (in.bad())
		throw std::invalid_argument("Cannot read generator Studio input");
	return bytes;
}
} // namespace
GeneratorStudioScreen::GeneratorStudioScreen(std::string packagePath, std::string settingsPath)
	: packagePath(std::move(packagePath)), settingsPath(std::move(settingsPath))
{
	preview.setState(MapPreview::State::Loading);
}
GeneratorStudioScreen::~GeneratorStudioScreen()
{
	if (worker.joinable())
		worker.join();
}
void GeneratorStudioScreen::generate()
{
	using Json = nlohmann::json;
	const auto begin = std::chrono::steady_clock::now();
	try
	{
		const auto package = MapGeneration::JavaScript::Package::parse(read(packagePath.c_str()));
		if (package->modules.size() != 1)
			throw std::invalid_argument("Studio supports one JavaScript module");
		auto catalog = std::make_shared<GeneratorRegistry>(
			std::vector<GeneratorDefinition>{package->definition(10000)});
		const auto settings = Json::parse(read(settingsPath.c_str()));
		if (!settings.is_object() || settings.size() != 4 || settings.at("candidates") != 1 ||
			settings.at("startingUnitLevel") != 0)
			throw std::invalid_argument(
				"Studio previews require one candidate and level-zero workers");
		const auto &seed = settings.at("seed");
		if (!seed.is_number_integer() || seed.get<std::int64_t>() < 0 ||
			seed.get<std::uint64_t>() > 0xffffffffULL)
			throw std::invalid_argument("Invalid generator seed");
		GenerationRequest request;
		request.setMethodDefaults(10000, catalog);
		request.seed = seed.get<std::uint32_t>();
		const auto &params = settings.at("params");
		if (!params.is_object() || params.size() > 72)
			throw std::invalid_argument("Invalid generator settings");
		for (const auto &[key, value] : params.items())
		{
			if (!value.is_number_integer() ||
				(value.is_number_unsigned()
					 ? value.get<std::uint64_t>() >
						   static_cast<std::uint64_t>(std::numeric_limits<int>::max())
					 : value.get<std::int64_t>() < std::numeric_limits<int>::min() ||
						   value.get<std::int64_t>() > std::numeric_limits<int>::max()))
				throw std::invalid_argument("Invalid control value: " + key);
			const int number = value.get<int>();
			const auto &control = request.control(key);
			if (control.normalize(number) != number)
				throw std::invalid_argument("Control outside legal domain: " + key);
			control.set(request, number);
		}
		auto game = std::make_unique<Game>(nullptr);
		auto result = GenerationService(*catalog).generate(*game, request, true);
		Json telemetry = Json::array();
		for (const auto &record : result.telemetry.records())
		{
			if (telemetry.size() >= 40)
				break;
			Json item{{"key", record.key}, {"kind", record.kind}, {"subject", record.subject}};
			std::visit([&](const auto &value) { item["value"] = value; }, record.value);
			telemetry.push_back(std::move(item));
		}
		Json details{{"success", static_cast<bool>(result)},
					 {"packageHash", result.packageHash},
					 {"seed", result.seed},
					 {"stage", result.stage},
					 {"diagnostic", result.diagnostic()},
					 {"telemetry", telemetry},
					 {"versionMinor", VERSION_MINOR},
					 {"simRevision", SIM_REVISION},
					 {"simVersion", Online::currentSimVersion().key()}};
		if (!result)
		{
			error = result.diagnostic();
			details["seconds"] =
				std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
			report = details.dump();
			complete.store(true, std::memory_order_release);
			return;
		}
		playable = !package->editorOnly && package->hasStartingColonies && game->teamsCount() > 0;
		players.setNumberOfPlayers(playable ? game->teamsCount() : 0);
		players.setRandomSeed(request.seed);
		for (int i = 0; i < game->teamsCount(); ++i)
		{
			if (playable)
				players.getBasePlayer(i) =
					BasePlayer(i, "Nicowar " + std::to_string(i + 1), i,
							   BasePlayer::playerTypeFromImplementationID(AI::NICOWAR));
			if (package->hasStartingColonies)
				starts.push_back(
					{game->teams[i]->startPosX, game->teams[i]->startPosY, game->teams[i]->color});
		}
		game->setGameHeader(players);
		auto *memory = new MemoryStreamBackend();
		BinaryOutputStream out(memory);
		game->save(&out, true, package->name);
		map = game->mapHeader;
		snapshot = std::make_shared<std::string>(memory->takeContents());
		details["worldFingerprint"] = Online::Sha256::hex(*snapshot);
		terrain.loadFromMap(game->map);
		if (!terrain.isLoaded())
			throw std::runtime_error("Cannot create map preview");
		details["playable"] = playable;
		details["checksum"] = game->checkSum(nullptr, nullptr, nullptr, true);
		details["seconds"] =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
		report = details.dump();
	}
	catch (const std::exception &e)
	{
		error = e.what();
		report =
			Json{{"success", false},
				 {"diagnostic", error.substr(0, 2000)},
				 {"seconds",
				  std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count()}}
				.dump();
	}
	complete.store(true, std::memory_order_release);
}
void GeneratorStudioScreen::updateExecution(Uint32)
{
	if (!started)
	{
		started = true;
		if constexpr (GAGCore::ThreadSupport::available)
		{
			try
			{
				worker = GAGCore::ThreadSupport::launch([this] { generate(); });
			}
			catch (const std::system_error &)
			{
				generate();
			}
		}
		else
			generate(); // Same bounded synchronous fallback as landscape previews.
	}
	if (!complete.load(std::memory_order_acquire))
		return;
	if (!delivered)
	{
		delivered = true;
		if (worker.joinable())
			worker.join();
		if (!error.empty())
		{
			preview.setState(MapPreview::State::Failed);
			GAGCore::ApplicationHost::studioGenerated(report);
			return;
		}
		preview.setMapThumbnail(terrain);
		preview.starts = starts;
		GAGCore::ApplicationHost::studioGenerated(report);
	}
	if (playable && error.empty() && GAGCore::ApplicationHost::studioWatchRequested())
		endExecute(1);
}
void GeneratorStudioScreen::handleExecutionEvent(SDL_Event event)
{
	preview.handlePreviewEvent(&event);
}
void GeneratorStudioScreen::paint()
{
	if (!gfx)
		return;
	gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), GAGCore::Color(20, 24, 28));
	preview.setScreenRectangle(0, 0, gfx->getW(), gfx->getH());
	preview.paint(gfx);
}
