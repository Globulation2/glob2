// SPDX-License-Identifier: GPL-3.0-or-later
#include "CommandLine.h"
#include "RecordingDefaults.h"
#include "MapRender.h"
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace Cli
{
namespace
{
Option value(std::string name, std::string type, std::string description, std::string fallback = "",
			 bool repeat = false, bool required = false)
{
	return {std::move(name), std::move(type), std::move(description), std::move(fallback), {},
			repeat,          required};
}
Option flag(std::string name, std::string description)
{
	return value(name, "flag", description, "false");
}
Option integer(std::string name, std::string description, std::int64_t low, std::int64_t high,
			   std::string fallback = "", bool repeat = false)
{
	auto o = value(name, "integer", description, fallback, repeat);
	o.minimum = low;
	o.maximum = high;
	return o;
}
Option choice(std::string name, std::string description, std::vector<std::string> values,
			  std::string fallback = "", bool repeat = false)
{
	auto o = value(name, "enum", description, fallback, repeat);
	o.choices = std::move(values);
	return o;
}
std::vector<Option> display()
{
	return {flag("--fullscreen", "Use fullscreen"),
			flag("--no-fullscreen", "Use a window"),
			flag("--resizable", "Allow window resizing"),
			flag("--no-resizable", "Disable window resizing"),
			flag("--custom-cursor", "Use the game cursor"),
			flag("--no-custom-cursor", "Use the system cursor"),
			flag("--mute", "Mute music and speech"),
			flag("--no-mute", "Unmute music and speech"),
			choice("--renderer", "Rendering backend", {"gpu", "software"}),
			choice("--graphics-detail", "Detail effects", {"full", "reduced"}),
			value("--window-size", "resolution",
				  "Initial window size in pixels; clamped to at least 640x480"),
			value("--username", "string", "Player name"),
			choice("--editor-script", "Map editor script language", {"sgsl", "usl"}, "preferences"),
			value("--record", "file", "Record menus and gameplay to MP4; refuses existing files"),
			value("--videoshot", "string", "Record to videoshots/NAME.mp4"),
			integer("--record-fps", "Recording frames per second", 1, 240,
					std::to_string(GAGCore::Recording::DefaultFps)),
			choice("--record-encoder", "Embedded encoder selection", {"auto", "software"}, "auto"),
			integer("--record-crf", "Software H.264 quality; lower means higher quality", 0, 51,
					std::to_string(GAGCore::Recording::DefaultCrf)),
			integer("--record-chapter-ticks", "Simulation ticks per chapter", 1, 1000000000,
					std::to_string(GAGCore::Recording::DefaultChapterTicks))};
}
void append(std::vector<Option> &to, const std::vector<Option> &from)
{
	to.insert(to.end(), from.begin(), from.end());
}
std::vector<Option> assets()
{
	return {value("--data-dir", "directory", "Add an asset search directory (in order)", "", true),
			value("--generator-package", "file", "Load a custom generator package or directory", "",
				  true)};
}
std::vector<Option> profile()
{
	return {value("--output-dir", "directory",
				  "Structured job directory; must not already contain result.json", "", false,
				  true),
			value("--profile", "string", "Isolated profile label", "glob2-tournament"),
			value("--building-catalog", "file", "Building catalog manifest")};
}
Option format()
{
	return choice("--format", "Output format", {"text", "json"}, "text");
}
Option compute()
{
	auto o = value("--compute-threads", "compute",
				   "Shared compute pool; auto uses logical CPU count", "auto");
	o.minimum = 1;
	o.maximum = 4294967295LL;
	return o;
}
std::vector<Option> mapSettings()
{
	return {
		integer("--seed", "Map seed (unsigned 32-bit)", 0, 4294967295LL, "1"),
		value("--set", "assignment", "Generator control KEY=VALUE; CLI overrides config", "", true),
		integer("--width", "Map width in tiles; registered values only", 1, 512),
		integer("--height", "Map height in tiles; registered values only", 1, 512),
		integer("--teams", "Number of colonies; registered values only", 1, 32),
		integer("--workers", "Initial workers per colony; registered values only", 0, 2147483647)};
}
std::vector<Option> preview()
{
	return {value("--preview", "file", "Overview PNG output"),
			integer("--preview-size", "Longest preview side in pixels", 128, 4096),
			choice("--preview-scale", "Scale retained thumbnail pixels", {"2", "4", "8"},
				   std::to_string(DefaultPreviewScale))};
}
std::string join(const std::vector<std::string> &words, const std::string &separator = " ")
{
	std::string out;
	for (const auto &word : words)
	{
		if (!out.empty())
			out += separator;
		out += word;
	}
	return out;
}
bool under(const std::string &path, const std::string &prefix)
{
	return prefix.empty() || path == prefix || path.rfind(prefix + " ", 0) == 0;
}
const std::map<std::string, std::string> &migrations()
{
	static const std::map<std::string, std::string> names = {
		{"--run-game", "game run"},
		{"--generate-map", "map generate (direct files) or map study (structured jobs)"},
		{"--headless-catalog", "info catalog --format json"},
		{"--sim-version", "info sim-version --format json"},
		{"--verify-match", "match verify"},
		{"--turn-client", "online turn-client"},
		{"--list-map-generators", "map generators"},
		{"--preview-map", "map preview"},
		{"--render-game", "map render"},
		{"--import-map-image", "map import-image"},
		{"--export-map-image", "map export-image"},
		{"--inspect-generator-package", "map inspect-package"},
		{"--validate-set", "map validate-set"},
		{"--compose-buildings", "assets compose-buildings"},
		{"--render-skin", "assets render-skin"},
		{"--skin-render-info", "assets skin-info"},
		{"--check-ai", "ai check"},
		{"--check-ai-json", "ai check --format json"},
		{"--check-script", "script check"},
		{"--attach-map-script", "script attach"},
		{"--hive-worker", "dev hive-worker"},
		{"--join", "online join"},
		{"--local-map", "online play-map"},
		{"--room-map", "online host-map"},
		{"-nox", "game repeat"},
		{"--nox", "game repeat"},
		{"-replay", "replay"},
		{"-test-games", "dev random-games --display"},
		{"-test-games-nox", "dev random-games"},
		{"-test-map-gen", "dev stress-maps"},
		{"-textshot", "dev textshots"},
		{"-dump-resources", "dev dump-resources"},
		{"-dump-wheat", "dev dump-wheat"},
		{"-dump-tiled", "dev dump-tiled"},
		{"-version", "info version"},
		{"--version", "info version"},
		{"/?", "--help"},
		{"-d", "--data-dir"},
		{"-dl", "info paths"},
		{"-s", "--window-size"},
		{"-u", "--username"},
		{"-f", "--fullscreen"},
		{"-F", "--no-fullscreen"},
		{"-r", "--resizable"},
		{"-R", "--no-resizable"},
		{"-c", "--custom-cursor"},
		{"-C", "--no-custom-cursor"},
		{"-g", "--renderer gpu"},
		{"-G", "--renderer software"},
		{"-l", "--graphics-detail reduced"},
		{"-m", "--mute"},
		{"-M", "--no-mute"},
		{"-sgsl", "--editor-script sgsl"},
		{"-usl", "--editor-script usl"},
		{"-vs", "--videoshot"},
		{"--out", "--output-dir"},
		{"--json", "--report-file"},
		{"--param", "--set"},
		{"--map", "--map-file (except dev random-games --map NAME)"},
		{"--replay", "--write-replay"},
		{"--ai-threads", "--compute-threads auto|N"},
		{"--gradient-workers", "--compute-threads auto|N"},
		{"--compute-experiments", "--compute-threads auto|N"},
		{"--record-size", "--record (full framebuffer resolution)"},
		{"--record-ffmpeg", "--record-encoder"}};
	return names;
}
std::string replacement(const std::string &old)
{
	const auto &names = migrations();
	const auto found = names.find(old);
	return found == names.end() ? "" : found->second;
}
[[noreturn]] void unknown(const std::string &word, const std::string &path)
{
	const auto next = replacement(word);
	if (!next.empty())
		throw std::invalid_argument(word + " has been removed; " +
									(next.front() == '-'
										 ? "use " + next + " on the relevant command (glob2 help)"
										 : "use glob2 " + next));
	throw std::invalid_argument("Unknown argument '" + word + "'; use glob2 help" +
								(path.empty() ? "" : " " + path));
}
void validateValue(const Option &o, const std::string &s)
{
	if (s.empty())
		throw std::invalid_argument(o.name + " requires a nonempty value");
	if (!o.choices.empty() && std::find(o.choices.begin(), o.choices.end(), s) == o.choices.end())
		throw std::invalid_argument(o.name + " expects " + join(o.choices, "|"));
	if (o.type == "integer" || o.type == "compute")
	{
		if (o.type == "compute" && s == "auto")
			return;
		std::int64_t n = 0;
		const auto parsed = std::from_chars(s.data(), s.data() + s.size(), n);
		const auto low = o.type == "compute" ? 1 : o.minimum;
		if (parsed.ec != std::errc() || parsed.ptr != s.data() + s.size() || n < low ||
			n > o.maximum)
			throw std::invalid_argument(o.name + " expects an integer in " + std::to_string(low) +
										".." + std::to_string(o.maximum));
	}
	if (o.type == "real")
	{
		std::istringstream input(s);
		double number;
		input >> std::noskipws >> number;
		if (!input || !input.eof() || !std::isfinite(number) || number < o.minimum ||
			number > o.maximum)
			throw std::invalid_argument(o.name + " expects a finite number in " +
										std::to_string(o.minimum) + ".." +
										std::to_string(o.maximum));
	}
	if (o.type == "assignment" &&
		(s.find('=') == std::string::npos || s.front() == '=' || s.back() == '='))
		throw std::invalid_argument(o.name + " expects KEY=VALUE");
	if (o.type == "resolution")
	{
		const auto x = s.find('x');
		if (x == std::string::npos)
			throw std::invalid_argument("--window-size expects WIDTHxHEIGHT");
		auto size = integer(o.name, "", 1, 2147483647);
		validateValue(size, s.substr(0, x));
		validateValue(size, s.substr(x + 1));
	}
}
void constraints(const Request &r)
{
	auto conflicts = [&](const std::vector<std::string> &keys)
	{
		unsigned present = 0;
		for (const auto &key : keys)
			present += r.has(key);
		if (present > 1)
			throw std::invalid_argument("Choose at most one of " + join(keys));
	};
	auto requireParent = [&](const std::string &key, const std::string &parent)
	{
		if (r.has(key) && !r.has(parent))
			throw std::invalid_argument(key + " requires " + parent);
	};
	const auto &metadata = definition(r.command);
	for (const auto &keys : metadata.conflicts)
		conflicts(keys);
	for (const auto &[key, parents] : metadata.requirements)
	{
		if (r.has(key) && std::none_of(parents.begin(), parents.end(),
									   [&](const auto &parent) { return r.has(parent); }))
			throw std::invalid_argument(key + " requires " + join(parents, " or "));
	}
	if (r.command == "dev random-games" && !r.has("--display") &&
		(r.has("--record") || r.has("--videoshot")))
		throw std::invalid_argument("Recording requires --display");
	if (r.has("--videoshot"))
	{
		const auto name = r.get("--videoshot");
		if (name == "." || name == ".." || name.find_first_of("/\\") != std::string::npos)
			throw std::invalid_argument("--videoshot requires a bare recording name");
	}
	if (r.command == "game run")
	{
		const unsigned sources = r.has("--generator") + r.has("--map-file") + r.has("--load-game");
		if (sources != 1)
			throw std::invalid_argument(
				"Choose exactly one of --generator, --map-file, --load-game");
		for (const auto &key : {"--map-seed", "--set", "--candidates", "--building-artwork"})
			requireParent(key, "--generator");
		requireParent("--diagnostic-interval", "--diagnostic-fields");
		requireParent("--diagnostic-png", "--diagnostic-fields");
		requireParent("--fork-rule", "--load-game");
		if (r.has("--load-game"))
			for (const auto &key :
				 {"--player", "--ai-param", "--ai-script", "--map-script", "--alliance",
				  "--win-condition", "--game-seed", "--experiment", "--rule", "--ai-order-delay"})
				if (r.has(key))
					throw std::invalid_argument(std::string(key) + " cannot override a saved game");
		if (!r.has("--load-game") && (!r.has("--game-seed") || !r.has("--player")))
			throw std::invalid_argument(
				"A new game requires --game-seed and one --player per team");
		if (r.has("--generator") && !r.has("--map-seed"))
			throw std::invalid_argument("--generator requires --map-seed");
		for (const auto &save : r.all("--save"))
			if (save != "initial" && save != "final")
			{
				if (save.rfind("every:", 0) != 0)
					throw std::invalid_argument("--save expects initial|final|every:N");
				validateValue(integer("--save", "", 1, 2147483647), save.substr(6));
			}
	}
	if (r.command == "map generate" || r.command == "map preview")
		if (!r.has("--output") && !r.has("--preview") && !r.has("--report-file") &&
			!r.has("--map-image"))
			throw std::invalid_argument(
				"Supply an output: --output, --preview, --report-file, or --map-image");
	if (r.has("--preview-size") || r.has("--preview-scale"))
		if (!r.has("--preview") && !(r.command == "map preview" && r.has("--output")))
			throw std::invalid_argument("Preview size/scale requires a PNG output");
}
} // namespace

const std::vector<Command> &commands()
{
	static const auto registry = []
	{
		std::vector<Command> all;
		auto add = [&](std::string path, std::string purpose, std::vector<std::string> positional,
					   unsigned required, std::vector<Option> opts, std::string outputs = "",
					   std::vector<std::string> examples = {}) -> Command &
		{
			opts.push_back(flag("--help", "Show this command's help (also -h)"));
			all.push_back({path, purpose, positional, required, opts, {}, examples, {}, outputs});
			return all.back();
		};
		auto launch = display();
		append(launch, assets());
		launch.push_back(value("--building-catalog", "file", "Building catalog manifest"));
		launch.push_back(compute());
		add("play", "Launch the graphical game", {}, 0, launch, "Interactive game",
			{"glob2 play --window-size 1280x720 --no-fullscreen"});
		add("replay", "Watch a recorded replay", {"FILE"}, 1, launch, "Interactive replay",
			{"glob2 replay replays/match.replay"});
		auto gen = mapSettings();
		append(gen, preview());
		append(gen, assets());
		append(gen, {value("--output", "file", "Playable gzip map output; .gz is appended"),
					 value("--report-file", "file", "Detailed map JSON report"),
					 value("--config", "file", "KEY=VALUE configuration; CLI settings override it"),
					 value("--map-image", "file", "Export categorical terrain PNG"),
					 value("--export-generator-package", "file",
						   "Export selected custom generator package")});
		add("map generate", "Generate a map at one exact seed", {"GENERATOR"}, 1, gen,
			"Requested .map.gz, overview PNG, categorical PNG and JSON report",
			{"glob2 map generate river --seed 713 --width 128 --height 128 --teams 2 --output "
			 "artifacts/river.map --preview artifacts/river.png"})
			.constraints = {"At least one output is required",
							"Registered generator controls constrain tile sizes and values",
							"A failed layout never silently retries with another seed"};
		auto study = profile();
		append(study, assets());
		append(study, {integer("--seed", "Map seed", 0, 4294967295LL, "1"),
					   value("--set", "assignment",
							 "Catalog control KEY=VALUE (study width/height use catalog exponents)",
							 "", true),
					   integer("--candidates", "Search candidate count; 0 uses the exact seed", 0,
							   10000, "0"),
					   integer("--rotations", "Team rotations", 1, 16, "1"),
					   flag("--write-map", "Write chosen map-r0.map.gz"),
					   choice("--report", "Additional report",
							  {"headroom", "diagnostics", "timing", "terrain"}, "", true),
					   value("--perturb", "string", "Study perturbation specification", "", true),
					   value("--building-artwork", "file", "Building artwork bundle")});
		add("map study", "Run a structured map-generation study", {"GENERATOR"}, 1, study,
			"result.json, progress.json, artifacts.json; optional maps and reports",
			{"glob2 map study river --seed 713 --write-map --output-dir artifacts/study"});
		add("map generators", "List generators or inspect controls", {"GENERATOR"}, 0,
			{format(), assets()[0], assets()[1]},
			"Generator IDs, defaults and allowed control values", {"glob2 map generators river"});
		auto image = assets();
		append(image, {value("--output", "file", "PNG output"),
					   value("--report-file", "file", "Map analysis JSON report")});
		append(image, preview());
		add("map preview", "Preview a map or save without stepping simulation", {"FILE"}, 1, image,
			"Overview PNG and/or map report",
			{"glob2 map preview maps/FourSquares1.map.gz --output artifacts/map.png"});
		auto render = assets();
		append(render, {value("--output", "file", "Game-view PNG output", "", false, true),
						integer("--render-max-pixels", "Maximum side in pixels", 1,
								MapRender::MaximumPixels, std::to_string(MapRender::DefaultPixels)),
						value("--render-field", "file", "Diagnostic field overlay"),
						value("--field-color", "string", "Field color as r,g,b")});
		add("map render", "Render the complete game view", {"FILE"}, 1, render,
			"PNG; requires installed game graphics");
		auto importing = mapSettings();
		append(importing, preview());
		append(importing, assets());
		append(
			importing,
			{value("--output", "file", "Playable gzip map output", "", false, true),
			 value("--report-file", "file", "Import report JSON"),
			 integer("--image-seam-width",
					 "Seam repair width in tiles; default short side / 32 clamped 2..12", 0, 16)});
		add("map import-image", "Import a categorical terrain PNG", {"FILE"}, 1, importing,
			"Playable .map.gz and optional preview/report");
		auto exporting = assets();
		exporting.push_back(value("--output", "file", "Categorical PNG output", "", false, true));
		add("map export-image", "Export categorical terrain and colony markers", {"FILE"}, 1,
			exporting, "Categorical PNG; not an exact save-state interchange");
		add("map inspect-package", "Inspect and canonicalize a generator package", {"FILE"}, 1,
			{value("--output", "file", "Canonical package JSON", "", false, true),
			 value("--report-file", "file", "Metadata JSON", "", false, true)},
			"Canonical package and metadata JSON");
		add("map validate-set", "Validate a terrain/resource set", {"FILE"}, 1,
			{value("--report-file", "file", "Validation JSON", "", false, true),
			 value("--preview", "file", "Preview PNG"),
			 choice("--gallery", "Preview gallery", {"0", "1"}, "0"),
			 integer("--phase", "Gallery phase", 0, 3, "0"),
			 integer("--variation", "Gallery variation", 0, 3, "0")},
			"Validation JSON and optional PNG");
		auto game = profile();
		append(game, assets());
		game.push_back(compute());
		append(
			game,
			{value("--map-file", "file", "Map input for a new game"),
			 value("--load-game", "file", "Saved-game input for continuation"),
			 value("--generator", "string", "Generate a map before starting"),
			 integer("--game-seed", "Game seed for a new match", 0, 4294967295LL),
			 integer("--map-seed", "Generated-map seed", 0, 4294967295LL),
			 value("--set", "assignment", "Generator control KEY=VALUE", "", true),
			 integer("--candidates", "Generation candidate count", 0, 10000, "0"),
			 value("--player", "string", "AI per map team in team order", "", true),
			 value("--ai-param", "assignment", "AI override PLAYER:KEY=VALUE", "", true),
			 value("--ai-script", "string", "JavaScript AI PLAYER:SOURCE.js", "", true),
			 value("--map-script", "file", "Map script source"),
			 integer("--alliance", "Alliance per team", 1, 16, "", true),
			 choice("--win-condition", "Winning condition",
					{"death", "allies", "prestige", "opponents", "script"}, "", true),
			 integer("--win-probability", "Early-victory threshold in permille", 501, 1000),
			 value("--experiment", "string", "Enable a registered experiment", "", true),
			 value("--rule", "assignment", "Game rule KEY=VALUE", "", true),
			 value("--fork-rule", "assignment", "Explicit saved-game fork: buildingGradientDelay=N",
				   "", true),
			 integer("--ticks", "Absolute tick limit; must exceed saved tick", 1, 2147483647,
					 std::to_string(DefaultTicks)),
			 integer("--gradient-delay", "Gradient scheduling delay in ticks", 1, 16,
					 std::to_string(DefaultGradientDelay)),
			 integer("--resource-growth-delay", "Resource scheduling delay in ticks", 1, 16),
			 integer("--ai-order-delay", "AI order delay in ticks", 0, 8),
			 value("--save", "string", "Snapshot: initial, final, or every:N", "", true),
			 choice("--telemetry", "Additional telemetry",
					{"checksums", "team-timeline", "maxima", "gradient-stats"}, "", true),
			 flag("--write-replay", "Write game.replay"),
			 integer("--benchmark-warmup", "Ticks excluded from benchmark counters", 0, 2147483647),
			 choice("--diagnostic-fields", "Diagnostic field selection", {"maxima"}),
			 integer("--diagnostic-interval", "Diagnostic interval in ticks", 1, 2147483647,
					 std::to_string(DefaultDiagnosticInterval)),
			 flag("--diagnostic-png", "Export diagnostic PNGs"),
			 value("--building-artwork", "file", "Artwork for generated maps")});
		add("game run", "Run or continue a structured headless game", {}, 0, game,
			"result.json, progress.json, artifacts.json; requested saves, replay and telemetry",
			{"glob2 game run --map-file maps/FourSquares1.map.gz --game-seed 713 --player castor "
			 "--player cortex --player castor --player cortex --ticks 64 --telemetry checksums "
			 "--output-dir artifacts/game"})
			.constraints = {
			"Exactly one of --map-file, --load-game or --generator",
			"New games require --game-seed and one --player per team",
			"Generation requires --map-seed", "Saved games reject new-match setup options",
			"Structured jobs isolate engine tuning and diagnostic environment variables"};
		auto repeat = assets();
		append(repeat,
			   {compute(),
				integer("--ticks", "Steps per run; 0 runs until the game ends", 0, 2147483647, "0"),
				integer("--runs", "Number of saved-game runs", 1, 2147483647, "1"),
				value("--building-catalog", "file", "Building catalog")});
		add("game repeat", "Repeat a saved game headlessly", {"FILE"}, 1, repeat,
			"Existing game summaries and opt-in environment telemetry")
			.environment = {"GLOB2_REPLAY_PATH", "GLOB2_CHECKSUM_SIDECAR", "GLOB2_TEAM_RESULTS",
							"GLOB2_TEAM_TIMELINE"};
		auto verify = profile();
		verify.pop_back();
		verify[1].defaultValue = "glob2-verify";
		append(verify,
			   {compute(), value("--map-file", "file", "Recorded match's map", "", false, true)});
		add("match verify", "Verify a multiplayer match record", {"RECORD"}, 1, verify,
			"result.json, verdict.json, checksums.txt, match.replay, compute.json, artifacts.json; "
			"verdict is in JSON");
		auto online = launch;
		online.push_back(value("--instance", "string", "Online instance origin"));
		add("online join", "Join an invite link or code", {"INVITE"}, 1, online,
			"Interactive online lobby");
		online.push_back(value("--hash", "string", "Catalog map SHA-256", "", false, true));
		online.push_back(value("--title", "string", "Catalog map title", "", false, true));
		add("online play-map", "Play a catalog map locally", {"ID"}, 1, online,
			"Interactive custom-game setup");
		add("online host-map", "Host a catalog map online", {"ID"}, 1, online,
			"Interactive room creation");
		auto turn = verify;
		turn[1].defaultValue = "glob2-turn-client";
		append(turn, {value("--orders-per-second", "real", "Synthetic order rate", "0.5"),
					  value("--max-seconds", "real", "Maximum runtime in seconds", "1800"),
					  integer("--seed", "Synthetic-order seed", 0, 4294967295LL, "1")});
		add("online turn-client", "Play a relay assignment headlessly", {"ASSIGNMENT"}, 1, turn,
			"result.json, network/trace artifacts and replay");
		add("ai check", "Validate JavaScript AI startup, metadata and callbacks", {"FILE"}, 1,
			{format()}, "Validation text or existing JSON validation payload");
		add("script check", "Compile a JavaScript map script", {"FILE"}, 1, {},
			"Validation message");
		add("script attach", "Attach a JavaScript map script", {"MAP", "SCRIPT", "OUTPUT"}, 3,
			assets(), "Playable .map.gz; refuses existing destination");
		add("assets compose-buildings", "Compose and validate building packages", {}, 0,
			{value("--base", "file", "Base building catalog"),
			 value("--package", "file", "Building package manifest", "", true),
			 value("--artwork-bundle", "file", "Building artwork bundle"), format()},
			"Resolved catalog/hash and optional artwork hash");
		add("assets render-skin", "Bake transparent colony sprites", {}, 0,
			{value("--manifest", "file", "Skin manifest", "", false, true),
			 value("--texture", "file", "Texture image", "", false, true),
			 value("--material", "file", "Material image", "", false, true),
			 value("--output-dir", "directory", "Sprite output directory", "", false, true)},
			"Colony sprite bundle");
		add("assets skin-info", "Inspect native skin exporter capabilities", {}, 0, {format()},
			"Exporter revision and pinned codec");
		auto random = assets();
		append(random, display());
		append(
			random,
			{compute(), flag("--display", "Show test games graphically"),
			 integer("--runs", "Game count; 0 repeats forever", 0, 2147483647, "0"),
			 integer(
				 "--ticks",
				 "Tick cap per random game; 0 runs until game end; overrides GLOB2_TEST_MAX_TICKS",
				 0, 2147483647, std::to_string(DefaultTicks)),
			 value("--ai-types", "string", "Comma-separated random AI pool"),
			 value("--map", "string", "Map name resolved as maps/NAME.map"),
			 value("--matchup", "string", "Comma-separated per-team AIs; requires --map"),
			 value("--save-game-as", "file", "Write the initial game before running"),
			 value("--building-catalog", "file", "Building catalog manifest")});
		add("dev random-games", "Exercise random AI games", {}, 0, random,
			"Game summaries and opt-in diagnostic outputs")
			.environment = {"GLOB2_TEST_SEED",    "GLOB2_TEST_MAX_TICKS", "GLOB2_TEST_RULES",
							"GLOB2_DUMP_GAME",    "GLOB2_REPLAY_PATH",    "GLOB2_CHECKSUM_SIDECAR",
							"GLOB2_TEAM_RESULTS", "GLOB2_TEAM_TIMELINE"};
		add("dev stress-maps", "Generate random maps indefinitely", {}, 0, assets(),
			"Generation diagnostics; interrupt to stop");
		auto shots = launch;
		shots.push_back(
			value("--output-dir", "directory", "Translation screenshot directory", "."));
		add("dev textshots", "Capture rendered translation texts", {}, 0, shots,
			"Translation screenshots; uses the existing screenshot workflow");
		add("dev dump-resources", "Dump map resource diagnostics", {"FILE"}, 1, assets(),
			"Resource counts on stdout");
		auto wheat = assets();
		wheat.push_back(integer("--team", "Team index", 0, 15, "0"));
		add("dev dump-wheat", "Dump AI wheat-protection plans", {"FILE"}, 1, wheat,
			"Wheat-plan diagnostics on stdout");
		auto tiled = assets();
		append(tiled, {integer("--repeat-x", "Horizontal repetitions", 1, 32, "1"),
					   integer("--repeat-y", "Vertical repetitions", 1, 32, "1"),
					   integer("--colonies", "Colonies per tile", 0, 32, "1"),
					   integer("--swarms", "Swarms per tile", 0, 32, "0")});
		add("dev dump-tiled", "Dump a repeated map", {"FILE"}, 1, tiled,
			"Tiled-map diagnostics on stdout");
		add("dev hive-worker", "Internal Hive JSON-lines worker", {}, 0, {},
			"Worker JSON on stdout; accepts stdin; internal subprocess interface");
		add("info version", "Inspect the executable build", {}, 0, {format()},
			"Build, SDL, save and network versions");
		add("info sim-version", "Inspect the simulation identity", {}, 0, {format()},
			"Existing simulation-version JSON or readable fields");
		add("info catalog", "Inspect AI, generator and job capabilities", {}, 0,
			{format(), assets()[0], assets()[1]},
			"Existing headless catalog JSON or readable fields");
		add("info paths", "Inspect asset search paths", {}, 0, {format(), assets()[0]},
			"Ordered asset directories");
		add("help", "Describe commands without loading game assets", {"COMMAND..."}, 0, {format()},
			"Text help or schema_version=1, cli_version=2 JSON");
		add("completion", "Print a shell completion script", {"SHELL"}, 1, {},
			"Bash, Zsh or Fish completion script");
		for (auto &c : all)
		{
			auto supports = [&](const std::string &name)
			{
				return std::any_of(c.options.begin(), c.options.end(),
								   [&](const auto &o) { return o.name == name; });
			};
			if (c.path == "map preview")
				c.conflicts.push_back({"--output", "--preview"});
			for (const auto &name : {"fullscreen", "resizable", "custom-cursor", "mute"})
				if (supports("--" + std::string(name)))
					c.conflicts.push_back({"--" + std::string(name), "--no-" + std::string(name)});
			for (const auto &keys :
				 std::vector<std::vector<std::string>>{{"--record", "--videoshot"},
													   {"--preview-scale", "--preview-size"},
													   {"--matchup", "--ai-types"}})
				if (supports(keys[0]))
					c.conflicts.push_back(keys);
			for (const auto &key :
				 {"--record-fps", "--record-crf", "--record-encoder", "--record-chapter-ticks"})
				if (supports(key))
					c.requirements[key] = {"--record", "--videoshot"};
			if (supports("--fullscreen"))
				c.constraints.push_back(
					"Unspecified display, audio and editor settings inherit saved preferences");
			if (supports("--field-color"))
				c.requirements["--field-color"] = {"--render-field"};
			if (supports("--matchup"))
				c.requirements["--matchup"] = {"--map"};
			if (c.path == "game run")
			{
				for (const auto &key :
					 {"--map-seed", "--set", "--candidates", "--building-artwork"})
					c.requirements[key] = {"--generator"};
				c.requirements["--diagnostic-interval"] = {"--diagnostic-fields"};
				c.requirements["--diagnostic-png"] = {"--diagnostic-fields"};
				c.requirements["--fork-rule"] = {"--load-game"};
			}
			if (c.path == "dev random-games")
				c.constraints.push_back("Recording requires --display");
			for (auto &o : c.options)
			{
				if (o.name.rfind("--record", 0) == 0 || o.name == "--videoshot")
					o.group = "Recording";
				else if (o.name == "--data-dir" || o.name == "--generator-package" ||
						 o.name == "--building-catalog")
					o.group = "Assets";
				else if (o.name == "--output" || o.name == "--output-dir" ||
						 o.name == "--report-file" || o.name.rfind("--preview", 0) == 0)
					o.group = "Outputs";
				if (o.name == "--ticks" || o.name == "--record-chapter-ticks" ||
					o.name == "--gradient-delay" || o.name == "--resource-growth-delay" ||
					o.name == "--ai-order-delay" || o.name == "--diagnostic-interval" ||
					o.name == "--benchmark-warmup")
					o.units = "ticks";
				if (o.name == "--max-seconds")
					o.units = "seconds";
				if (o.name == "--record-fps")
					o.units = "frames/second";
				if (o.name == "--window-size" || o.name == "--preview-size" ||
					o.name == "--render-max-pixels")
					o.units = "pixels";
			}
			if (c.path == "dev hive-worker")
			{
				c.platform = "native subprocess (browser uses its dedicated worker)";
#ifdef __EMSCRIPTEN__
				c.available = false;
#endif
			}
			if (c.path == "assets render-skin" || c.path == "assets skin-info")
			{
				c.platform = "native OpenGL client";
#if !defined(HAVE_OPENGL) || defined(__EMSCRIPTEN__) || defined(GLOB2_MOBILE)
				c.available = false;
#endif
			}
			if (c.examples.empty())
			{
				static const std::map<std::string, std::string> examples = {
					{"map render",
					 "glob2 map render maps/FourSquares1.map.gz --output artifacts/game-view.png"},
					{"map import-image", "glob2 map import-image terrain.png --width 128 --height "
										 "128 --output artifacts/imported.map"},
					{"map export-image", "glob2 map export-image maps/FourSquares1.map.gz --output "
										 "artifacts/terrain.png"},
					{"map inspect-package",
					 "glob2 map inspect-package generator.json --output artifacts/canonical.json "
					 "--report-file artifacts/package.json"},
					{"map validate-set", "glob2 map validate-set set.json --report-file "
										 "artifacts/set-report.json --preview artifacts/set.png"},
					{"game repeat", "glob2 game repeat saved.game.gz --ticks 64 --runs 1"},
					{"match verify",
					 "glob2 match verify test/fixtures/multiplayer/FourSquares1.g2mr --map-file "
					 "maps/FourSquares1.map.gz --output-dir artifacts/verified"},
					{"online join",
					 "glob2 online join ABCDEF --instance https://app.glob2online.com"},
					{"online play-map",
					 "glob2 online play-map MAP_ID --hash SHA256 --title 'Catalog map'"},
					{"online host-map",
					 "glob2 online host-map MAP_ID --hash SHA256 --title 'Catalog map'"},
					{"online turn-client", "glob2 online turn-client assignment.json --map-file "
										   "map.map.gz --output-dir artifacts/client"},
					{"ai check", "glob2 ai check examples/javascript/ai.js --format json"},
					{"script check", "glob2 script check examples/javascript/scenario.js"},
					{"script attach", "glob2 script attach maps/FourSquares1.map.gz "
									  "examples/javascript/scenario.js artifacts/scripted.map.gz"},
					{"assets compose-buildings", "glob2 assets compose-buildings --format json"},
					{"assets render-skin",
					 "glob2 assets render-skin --manifest skin.json --texture texture.png "
					 "--material material.png --output-dir artifacts/sprites"},
					{"assets skin-info", "glob2 assets skin-info --format json"},
					{"dev random-games",
					 "glob2 dev random-games --runs 1 --ticks 64 --map Playground --matchup "
					 "castor,warrush,castor,warrush,castor,warrush,castor,warrush"},
					{"dev stress-maps", "glob2 dev stress-maps"},
					{"dev textshots", "glob2 dev textshots --output-dir artifacts/textshots"},
					{"dev dump-resources", "glob2 dev dump-resources maps/FourSquares1.map.gz"},
					{"dev dump-wheat", "glob2 dev dump-wheat maps/FourSquares1.map.gz --team 0"},
					{"dev dump-tiled",
					 "glob2 dev dump-tiled maps/FourSquares1.map.gz --repeat-x 2 --repeat-y 2"},
					{"dev hive-worker", "glob2 dev hive-worker < requests.jsonl"},
					{"info version", "glob2 info version"},
					{"info sim-version", "glob2 info sim-version --format json"},
					{"info catalog", "glob2 info catalog --format json"},
					{"info paths", "glob2 info paths --data-dir ./data"},
					{"help", "glob2 help map generate --format json"},
					{"completion", "glob2 completion bash"}};
				c.examples = {examples.at(c.path)};
			}
		}
		return all;
	}();
	return registry;
}
const Command &definition(const std::string &path)
{
	for (const auto &c : commands())
		if (c.path == path)
			return c;
	throw std::invalid_argument("Unknown command '" + path + "'; use glob2 --help");
}
bool Request::has(const std::string &key) const
{
	return options.count(key) != 0;
}
std::string Request::get(const std::string &key, const std::string &fallback) const
{
	const auto it = options.find(key);
	if (it != options.end())
		return it->second.at(0);
	if (!fallback.empty())
		return fallback;
	for (const auto &o : definition(command).options)
		if (o.name == key)
			return o.defaultValue;
	return "";
}
std::vector<std::string> Request::all(const std::string &key) const
{
	const auto it = options.find(key);
	return it == options.end() ? std::vector<std::string>{} : it->second;
}
Request parse(const std::vector<std::string> &args)
{
	Request r;
	if (args.empty())
	{
		r.command = "play";
		return r;
	}
	std::string scheme = args[0].substr(0, 8);
	std::transform(scheme.begin(), scheme.end(), scheme.begin(),
				   [](unsigned char c) { return char(std::tolower(c)); });
	if (scheme.rfind("glob2:", 0) == 0 || scheme == "https://" || scheme.rfind("http://", 0) == 0)
	{
		r.command = "online join";
		r.positionals = {args[0]};
		if (args.size() != 1)
			throw std::invalid_argument("A bare launch URL must be the only argument");
		return r;
	}
	size_t i = 0;
	if (args[0] == "--help" || args[0] == "-h")
	{
		r.command = "help";
		r.help = true;
		i = 1;
	}
	else if (args[0] == "help")
	{
		r.command = "help";
		r.help = true;
		i = 1;
	}
	else
	{
		std::string path = args[0];
		i = 1;
		const bool group = std::any_of(commands().begin(), commands().end(),
									   [&](const Command &c) { return under(c.path, path); });
		if (!group)
			unknown(args[0], "");
		while (std::none_of(commands().begin(), commands().end(),
							[&](const Command &c) { return c.path == path; }))
		{
			if (i == args.size() || args[i] == "--help" || args[i] == "-h")
			{
				r.command = "help";
				r.help = true;
				r.positionals = {path};
				if (i < args.size())
					++i;
				break;
			}
			path += " " + args[i++];
			if (std::none_of(commands().begin(), commands().end(),
							 [&](const Command &c) { return under(c.path, path); }))
				unknown(path, "");
		}
		if (r.command.empty())
			r.command = path;
	}
	const auto &c = definition(r.command);
	bool positionalOnly = false;
	const bool wantsHelp = r.help ||
						   std::find(args.begin() + i, args.end(), "--help") != args.end() ||
						   std::find(args.begin() + i, args.end(), "-h") != args.end();
	for (; i < args.size(); ++i)
	{
		const auto token = args[i];
		if (token == "--" && !positionalOnly)
		{
			positionalOnly = true;
			continue;
		}
		if (!positionalOnly && token.size() > 1 && token[0] == '-')
		{
			const auto eq = token.find('=');
			const auto key = token.substr(0, eq);
			if (key == "--help" || key == "-h")
			{
				if (eq != std::string::npos)
					throw std::invalid_argument("--help does not take a value");
				r.help = true;
				continue;
			}
			const auto o = std::find_if(c.options.begin(), c.options.end(),
										[&](const Option &o) { return o.name == key; });
			if (o == c.options.end() && key == "--format" && wantsHelp)
			{
				const std::string v = eq == std::string::npos ? (++i < args.size() ? args[i] : "")
															  : token.substr(eq + 1);
				validateValue(format(), v);
				if (r.has(key))
					throw std::invalid_argument("Duplicate option " + key);
				r.options[key] = {v};
				continue;
			}
			if (o == c.options.end())
				unknown(key, r.command);
			std::string v;
			if (o->type == "flag")
			{
				if (eq != std::string::npos)
					throw std::invalid_argument(key + " is a switch and takes no value");
				v = "true";
			}
			else if (eq != std::string::npos)
				v = token.substr(eq + 1);
			else
			{
				if (i + 1 == args.size() ||
					(args[i + 1].rfind('-', 0) == 0 && o->type != "integer" && o->type != "real"))
					throw std::invalid_argument("Missing value for " + key + "; use " + key +
												"=VALUE for a value starting with '-'");
				v = args[++i];
			}
			validateValue(*o, v);
			if (r.has(key) && !o->repeatable)
				throw std::invalid_argument("Duplicate option " + key);
			r.options[key].push_back(v);
			r.occurrences.emplace_back(key, v);
		}
		else
			r.positionals.push_back(token);
	}
	if (r.help || r.command == "help")
	{
		if (r.command == "help")
		{
			const auto target = join(r.positionals);
			if (!target.empty() &&
				std::none_of(commands().begin(), commands().end(),
							 [&](const Command &c) { return under(c.path, target); }))
				unknown(target, "");
		}
		return r;
	}
	if (r.positionals.size() < c.minimumPositionals || r.positionals.size() > c.positionals.size())
		throw std::invalid_argument(
			(r.positionals.size() > c.positionals.size()
				 ? "Unexpected positional argument '" + r.positionals.back() + "'"
				 : "Expected " + join(c.positionals)) +
			"; use glob2 " + c.path + " --help");
	for (const auto &o : c.options)
		if (o.required && !r.has(o.name))
			throw std::invalid_argument(o.name + " is required; use glob2 " + c.path + " --help");
	constraints(r);
	if (r.command == "completion" && r.positionals[0] != "bash" && r.positionals[0] != "zsh" &&
		r.positionals[0] != "fish")
		throw std::invalid_argument("completion expects bash|zsh|fish");
	return r;
}
Request parse(int argc, char **argv)
{
	return parse(std::vector<std::string>(argv + 1, argv + argc));
}
nlohmann::json describe(const std::string &path)
{
	nlohmann::json result = {{"schema_version", 1},
							 {"cli_version", Version},
							 {"command", path},
							 {"commands", nlohmann::json::array()},
							 {"exit_codes",
							  {{"0", "success/help"},
							   {"2", "invalid arguments or rejected input"},
							   {"3", "operational failure"}}}};
	result["removed_arguments"] = migrations();
	for (const auto &c : commands())
		if (under(c.path, path))
		{
			nlohmann::json row = {{"path", c.path},
								  {"description", c.description},
								  {"positionals", c.positionals},
								  {"minimum_positionals", c.minimumPositionals},
								  {"options", nlohmann::json::array()},
								  {"constraints", c.constraints},
								  {"examples", c.examples},
								  {"environment", c.environment},
								  {"outputs", c.outputs},
								  {"platform", c.platform},
								  {"available", c.available},
								  {"conflicts", c.conflicts},
								  {"requirements", c.requirements}};
			for (const auto &o : c.options)
				row["options"].push_back({{"name", o.name},
										  {"type", o.type},
										  {"description", o.description},
										  {"default", o.defaultValue},
										  {"choices", o.choices},
										  {"repeatable", o.repeatable},
										  {"required", o.required},
										  {"minimum", o.minimum},
										  {"maximum", o.maximum},
										  {"group", o.group},
										  {"units", o.units}});
			result["commands"].push_back(row);
		}
	if (result["commands"].empty())
		throw std::invalid_argument("Unknown help target " + path);
	return result;
}
std::string help(const std::string &path)
{
	std::ostringstream out;
	out << "Globulation 2 — CLI " << Version << "\n\n";
	auto c = std::find_if(commands().begin(), commands().end(),
						  [&](const Command &c) { return c.path == path; });
	if (c == commands().end())
	{
		out << "Usage: glob2" << (path.empty() ? "" : " " + path) << " COMMAND [OPTIONS]\n";
		if (path.empty())
			out << "Run glob2 without arguments to launch the game. Supported launch URLs also "
				   "work.\n";
		out << "\nCommands:\n";
		for (const auto &row : commands())
			if (under(row.path, path))
				out << "  " << row.path << "  " << row.description
					<< (row.available ? "" : " [unavailable on this build]") << "\n";
		out << "\nUse glob2 COMMAND --help for complete options and examples.\nUse glob2 help "
			   "--format json for the machine-readable command tree.\n";
		if (path.empty())
			out << "\nExamples:\n  glob2 play --window-size 1280x720\n  glob2 map generators\n  "
				   "glob2 map generate river --seed 713 --output artifacts/river.map\n  glob2 game "
				   "run --help\n";
	}
	else
	{
		out << c->description << "\n\nUsage: glob2 " << c->path;
		for (size_t i = 0; i < c->positionals.size(); ++i)
			out << " "
				<< (i < c->minimumPositionals ? c->positionals[i] : "[" + c->positionals[i] + "]");
		out << " [OPTIONS]\n\nOptions:\n";
		std::vector<std::string> groups;
		for (const auto &o : c->options)
			if (std::find(groups.begin(), groups.end(), o.group) == groups.end())
				groups.push_back(o.group);
		for (const auto &group : groups)
		{
			out << "\n" << group << ":\n";
			for (const auto &o : c->options)
			{
				if (o.group != group)
					continue;
				out << "  " << o.name
					<< (o.type == "flag"
							? ""
							: " <" + (o.choices.empty() ? o.type : join(o.choices, "|")) + ">")
					<< "\n    " << o.description;
				if (!o.defaultValue.empty())
					out << " (default: " << o.defaultValue << ")";
				if (o.type == "integer" || o.type == "real")
					out << " [" << o.minimum << ".." << o.maximum << "]";
				if (!o.units.empty())
					out << " [units: " << o.units << "]";
				if (o.required)
					out << " [required]";
				if (o.repeatable)
					out << " [repeatable]";
				out << "\n";
			}
		}
		out << "\nConstraints:\n  Scalar options cannot repeat; switches and their --no- "
			   "counterparts conflict.\n  Use -- before positional filenames starting with '-'.\n";
		for (const auto &s : c->constraints)
			out << "  " << s << "\n";
		for (const auto &keys : c->conflicts)
			out << "  Choose at most one: " << join(keys) << "\n";
		for (const auto &[key, parents] : c->requirements)
			out << "  " << key << " requires " << join(parents, " or ") << "\n";
		out << "\nOutputs: " << c->outputs << "\nPlatform: " << c->platform
			<< (c->available ? "" : " (unavailable in this build)") << "\n";
		if (!c->environment.empty())
			out << "Environment: " << join(c->environment, ", ") << "\n";
		out << "\nExamples:\n";
		for (const auto &s : c->examples)
			out << "  " << s << "\n";
	}
	out << "\nExit codes: 0 success/help; 2 invalid arguments or rejected input; 3 operational "
		   "failure.\n";
	return out.str();
}
std::string completion(const std::string &shell)
{
	// Completion is generated entirely from static metadata; never initialize game state.
	std::ostringstream out;
	if (shell == "zsh")
		out << "#compdef glob2\n";
	out << "# Generated from Glob2 CLI " << Version << " command definitions.\n";
	std::map<std::string, std::vector<std::string>> children;
	for (const auto &c : commands())
	{
		std::istringstream words(c.path);
		std::string word, parent;
		while (words >> word)
		{
			auto &v = children[parent];
			if (std::find(v.begin(), v.end(), word) == v.end())
				v.push_back(word);
			parent += (parent.empty() ? "" : " ") + word;
		}
	}
	if (shell == "bash")
	{
		out << "_glob2_complete() {\n  local cur prev path='' word opts='' values='' kind='' "
			   "prefix='' i\n  cur=${COMP_WORDS[COMP_CWORD]}; prev=${COMP_WORDS[COMP_CWORD-1]}\n  "
			   "if [[ $cur == --*=* ]]; then prev=${cur%%=*}; prefix=$prev=; cur=${cur#*=};\n  "
			   "elif [[ $prev == = ]]; then prev=${COMP_WORDS[COMP_CWORD-2]}; fi\n  for "
			   "((i=1;i<COMP_CWORD;i++)); do\n    word=${COMP_WORDS[i]}\n    if [[ $i == 1 && "
			   "$word == help ]]; then continue; fi\n    case \"$path|$word\" in\n";
		for (const auto &[parent, kids] : children)
			for (const auto &kid : kids)
				out << "      '" << parent << "|" << kid << "') path='" << parent
					<< (parent.empty() ? "" : " ") << kid << "';;\n";
		out << "    esac\n  done\n  case \"$path\" in\n";
		for (const auto &[parent, kids] : children)
			out << "    '" << parent << "') opts='" << join(kids) << "';;\n";
		for (const auto &c : commands())
		{
			out << "    '" << c.path << "') opts='";
			for (const auto &o : c.options)
				out << o.name << " ";
			out << "';;\n";
		}
		out << "  esac\n  case \"$path|$prev\" in\n";
		for (const auto &c : commands())
			for (const auto &o : c.options)
				if (o.type != "flag")
					out << "    '" << c.path << "|" << o.name << "') kind='" << o.type
						<< "'; values='" << join(o.choices) << "';;\n";
		out << "  esac\n  COMPREPLY=()\n  if [[ -n $values ]]; then while IFS= read -r word; do "
			   "COMPREPLY+=(\"$prefix$word\"); done < <(compgen -W \"$values\" -- \"$cur\");\n  "
			   "elif [[ $kind == directory ]]; then while IFS= read -r word; do "
			   "COMPREPLY+=(\"$prefix$word\"); done < <(compgen -d -- \"$cur\");\n  elif [[ $kind "
			   "== file ]]; then while IFS= read -r word; do COMPREPLY+=(\"$prefix$word\"); done < "
			   "<(compgen -f -- \"$cur\");\n  elif [[ -z $kind ]]; then while IFS= read -r word; "
			   "do COMPREPLY+=(\"$word\"); done < <(compgen -W \"$opts\" -- \"$cur\");\n    if [[ "
			   "$cur != -* && -n $path ]]; then while IFS= read -r word; do "
			   "COMPREPLY+=(\"$word\"); done < <(compgen -f -- \"$cur\"); fi\n  fi\n}\ncomplete -F "
			   "_glob2_complete glob2\n";
	}
	else if (shell == "zsh")
	{
		out << "_glob2() {\n  local command_path='' word i\n  for ((i=2;i<CURRENT;i++)); do\n    "
			   "word=$words[i]\n    if ((i == 2)) && [[ $word == help ]]; then continue; fi\n    "
			   "case \"$command_path|$word\" in\n";
		for (const auto &[parent, kids] : children)
			for (const auto &kid : kids)
				out << "      '" << parent << "|" << kid << "') command_path='" << parent
					<< (parent.empty() ? "" : " ") << kid << "';;\n";
		out << "    esac\n  done\n  case \"$command_path\" in\n";
		for (const auto &[parent, kids] : children)
			out << "    '" << parent << "') compadd " << join(kids) << ";;\n";
		for (const auto &c : commands())
		{
			out << "    '" << c.path << "') _arguments -s \\\n";
			for (const auto &o : c.options)
				out << "      '" << (o.repeatable ? "*" : "") << o.name << "[" << o.name.substr(2)
					<< "]"
					<< (o.type == "flag"
							? ""
							: ":value:" + (o.choices.empty() ? (o.type == "directory" ? "_files -/"
																: o.type == "file"    ? "_files"
																					  : "")
															 : "(" + join(o.choices) + ")"))
					<< "' \\\n";
			out << "      '*:file:_files';;\n";
		}
		out << "  esac\n}\ncompdef _glob2 glob2\n";
	}
	else if (shell == "fish")
	{
		out << "function __glob2_path\n  set -l path ''\n  set -l words (commandline -opc)\n"
			   "  if test (count $words) -ge 2; and test \"$words[2]\" = help\n"
			   "    set -e words[2]\n  end\n  for word in $words[2..-1]\n    switch "
			   "\"$path|$word\"\n";
		for (const auto &[parent, kids] : children)
			for (const auto &kid : kids)
				out << "      case '" << parent << "|" << kid << "'\n        set path '" << parent
					<< (parent.empty() ? "" : " ") << kid << "'\n";
		out << "    end\n  end\n  echo $path\nend\nfunction __glob2_at\n  set -l current "
			   "(__glob2_path)\n  test \"$current\" = \"$argv[1]\"\nend\n";
		for (const auto &[parent, kids] : children)
			out << "complete -c glob2 -n \"__glob2_at '" << parent << "'\" -f -a '" << join(kids)
				<< "'\n";
		for (const auto &c : commands())
			for (const auto &o : c.options)
				out << "complete -c glob2 -n \"__glob2_at '" << c.path << "'\" -l "
					<< o.name.substr(2) << (o.type == "flag" ? "" : " -r")
					<< (o.type == "directory" ? " -f -a '(__fish_complete_directories)'"
						: o.choices.empty()   ? ""
											  : " -f -a '" + join(o.choices) + "'")
					<< "\n";
	}
	else
		throw std::invalid_argument("completion expects bash|zsh|fish");
	return out.str();
}
int runStatic(const Request &r)
{
	if (r.command == "completion" && !r.help)
	{
		std::cout << completion(r.positionals.at(0));
		return 0;
	}
	if (r.help || r.command == "help")
	{
		const auto path = r.command == "help" ? join(r.positionals) : r.command;
		if (r.get("--format") == "json")
			std::cout << describe(path).dump(2) << '\n';
		else
			std::cout << help(path);
		return 0;
	}
	if (!definition(r.command).available)
		throw std::runtime_error(r.command + " requires " + definition(r.command).platform);
	return -1;
}
} // namespace Cli
