// SPDX-License-Identifier: GPL-3.0-or-later
// What an MCP client needs to play without the source: keep every statement here
// true to the game's current behavior, and update docs/development/mcp-ui-server.md
// alongside it.
#include "Guide.h"
#include <stdexcept>
#include <utility>

namespace MCP
{
namespace
{
using Json = nlohmann::json;
constexpr auto Blob = "https://github.com/Globulation2/glob2/blob/master/";
std::string doc(const char *path) { return std::string(Blob) + path; }

struct Topic
{
	const char *name, *summary, *text;
};

// Topics are short so a client can read only the one it needs.
const Topic Topics[] = {
	{"overview", "What the game is and the first five calls to make",
	 R"(Globulation 2 is a real-time strategy game where you never command units directly.
Your colony's globules (Workers, Explorers, Warriors) act on their own; you steer them by
placing buildings, flags and zones and by setting how many units each one requests.

This MCP server operates the real game window: it reads the on-screen UI as typed
controls and sends mouse and keyboard input. Typical first calls:
1. ui_status: is the window available, and does anyone hold control?
2. ui_acquire_control: returns a token; pass it to every input tool.
3. ui_inspect {"compact": true}: the current screen name and its controls.
4. ui_act {token, surface, key, revision, action:"activate"}: press a control.
5. ui_wait {"condition": {"screen": "game_session"}}: wait for a screen change.
From the main menu, "Tutorial" (key menu/1) then "Introduction" (tutorial/lesson/0)
starts a guided first match whose lesson card tells you each step.

Other topics: gameplay, screens, hud, actions, control, hive, troubleshooting, links.)"},
	{"gameplay", "Rules: units, buildings, food, flags, zones and victory",
	 R"(Units. Workers harvest, carry resources and construct; Explorers fly, scout and can
learn to attack from the air; Warriors fight on foot. Units get hungry and eat wheat at
Inns; starving units die. Wounded units recover at Hospitals.
Resources lie on the map: wheat (food), wood (construction), stone (walls, towers,
upgrades), algae (in water) and fruit (happiness, traded at Markets).

Indirect control. Every building or flag has a requested unit count ("Assigned
workers" slider): a request, not units already present. Free units take jobs by
priority and distance. Construction sites request builders; finished Inns, the Swarm
and towers request suppliers who bring their resources.

Buildings (Construction panel): Swarm (consumes wheat to produce units; its
production ratio sliders choose Workers/Explorers/Warriors), Inn (feeds units),
Hospital (heals), Racetrack (walking speed), Swimming pool (swimming), Barracks
(warrior training), School (worker skills), Defense Tower (shoots, needs stone),
Stone Wall, Market. Skilled workers can upgrade buildings to higher levels.
Flags (Flags and areas panel): Exploration Flag attracts explorers; War Flag sends
warriors to fight around it; Clearing Flag sends workers to harvest the chosen
resources nearby. Each flag has a unit count and a range.
Zones, painted with a brush: Forbidden (units keep out), Guard (warriors defend it),
Clearing (workers clear resources there).

Victory. A colony is eliminated when it has no units or production left that could
recover. In a custom game you win by eliminating every non-allied colony; the Game
Rules tab can add prestige, a time limit or an early win when one side's win chance
passes a threshold (95% by default). Tutorial lessons end when their objectives are
done; the Objectives button shows them.)"},
	{"screens", "Menus and how to start a tutorial or a custom game",
	 R"(inspection.screen names the active screen. Common names:
introduction_offer: shown on a new profile's first launch; intro/start begins the
  introduction lesson, intro/menu goes to the main menu.
main_menu: buttons Custom game (menu/3), Play online (menu/4), Campaign (menu/0),
  Load game (menu/2), Tutorial (menu/1), LAN Game (menu/5), Settings (menu/6),
  Editor (menu/7), Credits (menu/8).
tutorial_catalog: one button per lesson, tutorial/lesson/0 (Introduction) to
  tutorial/lesson/9; tutorial/back returns to the main menu.
custom_game: tabs Map (tab/0), Players & Teams (tab/1), Game Rules (tab/2); start
  ("Play this map") launches the match, back returns.
game_session: a match in progress (see hud). The in-game menu, objectives and
  alliance dialogs appear as extra surfaces on this same screen.
end_game: results after a match; quit leaves it.
Others include settings, campaign_main_menu and map_editor.

Surface instance numbers and control revisions change whenever a screen is rebuilt, so
re-read them after every transition. ui_act returns the inspection from the moment its
input was consumed, which can still be the old screen: follow it with ui_wait on the
expected screen; a screen that has just opened can report no surfaces for a frame, so
inspect again. A lesson or scenario may open with a briefing dialog: press its ok.)"},
	{"hud", "Reading the in-game screen (game_session)",
	 R"(Left: the map canvas (key game/map). Right panel, top to bottom: minimap
(game/minimap), panel tabs Construction (game/panel/0), Flags and areas
(game/panel/1), Statistics (game/panel/2), Graphs (game/panel/3), then the open
panel's contents (lesson text calls these panels Build and Flags). Icons beside the minimap: Menu (game/menu), Alliance, Objectives.
game/placement (read-only) names the active placement tool: none, building:<type>,
flag:<type> or zone:<kind>:paint|erase.
Top bar labels (read-only, key game/stats/...): workers, explorers and warriors as
free/total (negative free means unmet requests), prestige, win chance in percent,
and units converted to and from your colony.
game/speed is a stepper: set_value -3..10 (0 is normal speed).

Construction palette buttons are game/buildings/<type>.0.site (disabled when the
lesson or rules do not allow that building yet). Flags are game/flags/<type>.0.finished;
zones game/zone/0..2 (Forbidden, Guard, Clearing) with brush mode game/brush/mode/0..1
(paint, erase) and brush sizes game/brush/figure/0..7.
Selecting a building or flag on the map replaces the panel with its details, keyed
building/<id>/<generation>/...: workers ("Assigned workers": units requested, of
the flag's kind on flags), priority/-1..1,
range (flags), production/0..2 (Swarm ratios for Workers, Explorers, Warriors),
construction (upgrade or repair) and destroy. game_world lists your buildings and units
and what your colony sees, in map tiles; resource stock and other details come from
hive_run.
Lesson card (tutorial only): a separate surface with the step text, intro/destination
(marks the target on the map) and intro/help. Its body key (intro/body/N) changes
with each step, so wait on the new key to detect progress.)"},
	{"actions", "Semantic actions versus coordinate gestures, placing and selecting",
	 R"(Prefer ui_act for anything that is a control: it targets {surface, key, revision},
is checked again when the game consumes it, and needs no coordinates. Actions:
activate (buttons, options, tabs), select (integer index of a choice), set_value
(slider/stepper integer, toggle boolean, text string) and scroll (signed pixels for a
scroll container; scroll one into view before acting on a control it hides).

In a match, think in map tiles. game_world reports your status (free/total units,
win chance), alerts such as attacks with their tiles, your buildings with ids, unit
counts and warrior training, known resources with their nearest tile, other teams'
buildings in sight and those remembered under fog, and how recent placements went.
It follows fog of war: only what your colony sees or has seen, never another team's
health or training. game_place puts a building site or flag with its top-left on a
tile through the click path (near searches around it for room; buildings need
explored ground). game_building changes a building or flag by id: workers, priority,
Swarm production, flag range, upgrade, repair, destroy, move a flag. game_zone paints
or erases forbidden, guard or clearing zones over a tile rectangle. game_view centres
the camera on a tile and returns its screen point.
The map, minimap and replay bar are canvases: use coordinates. Take ui_screenshot
(max_width keeps it small) and pass its capture_id with x/y in that image's pixels.
- Place by hand: activate a palette button, then ui_click on the map. Placement stays
  active for further clicks (game/placement shows it); ui_key Escape or a right click
  leaves it.
- Paint a zone: pick zone, mode and brush, then ui_click or ui_drag over the map.
- Select: with no placement active, ui_click a building, flag or unit on the map;
  its details appear in the right panel. Escape clears the selection; a further
  Escape opens the game menu.
- Move the view: ui_click on the minimap, or ui_drag across empty map; ui_scroll
  over the map zooms; holding an arrow key (ui_key action down, later up) scrolls.
Prefer the palette and panel controls to keyboard shortcuts: inspection describes
them and ui_act confirms each one.)"},
	{"control", "The control lease and token workflow",
	 R"(Reads (ui_status, ui_screenshot, ui_inspect, ui_wait, game_guide, game_world,
hive_status, hive_api, hive_result) need no control. Everything that sends input needs the token
from ui_acquire_control. Only one client holds control; a second acquisition fails
until the first releases it, its lease expires, or the person at the computer uses
Settings > MCP server > Revoke control. The lease lasts 300 seconds after the last
token-bearing call; ui_renew_control extends it. A rejected token means control was
lost: acquire again. Human mouse and keyboard input keeps working alongside yours.
Never retry an input call automatically: it may have been delivered before an error
or timeout. Inspect first, then decide. ui_release_control releases held keys and
buttons and frees the lease when you finish.)"},
	{"hive", "Reading the world and scripting your colony with JavaScript",
	 R"(Hive tools run short JavaScript against your own colony during a local match.
hive_status reports availability and the current session id (a match identity);
hive_api returns the full API contract, including world queries such as
ctx.game.units(), ctx.game.buildings() and order descriptors. hive_run executes one
script (token, session, a fresh operation_key, source) and hive_result fetches its
output. hive_program installs standing scripts that run every N ticks. The game_*
tools cover the common reads and orders without a script; use Hive for full records
(stocks, skills, terrain) and for standing logic that reacts between your turns.)"},
	{"troubleshooting", "What common errors mean and what to do next",
	 R"(available=false: the window is minimized, hidden or showing an OS dialog; ask the
person to restore it, then call ui_status again.
"Another client controls the game": wait, or ask the person to press Revoke control.
Stale control: the revision or surface changed; ui_inspect again and use the new
values. Hidden control: scroll its container (ui_act scroll) first. Blocked control:
a dialog or overlay covers it; close or answer the dialog. A map canvas reports
blocked when any panel overlaps part of it; clicks on its uncovered part still work.
Stale viewport or expired capture: take a new ui_screenshot and use its capture_id.
Wait timed out: the condition did not happen; inspect the returned snapshot.
Invalid argument errors name the field and the rule it broke.
If nothing seems to happen in a match, check the game speed is not paused and that
the building's requested unit count is above zero.)"},
	{"links", "Source code, documentation and tutorial content", nullptr},
};

Json links()
{
	return {{"repository", RepositoryUrl},
			{"mcp_server_guide", ServerGuideUrl},
			{"documentation_index", doc("docs/README.md")},
			{"game_features", doc("docs/features/README.md")},
			{"custom_game_rules", doc("docs/features/custom-game-setup/README.md")},
			{"hive_scripting", doc("docs/multiplayer/hive-mind.md")},
			{"tutorial_lessons", "https://github.com/Globulation2/glob2/tree/master/content/scripts"},
			{"tutorial_code", "https://github.com/Globulation2/glob2/tree/master/src/tutorial"},
			{"player_website", "https://glob2.online/"},
			{"browser_game", "https://app.glob2.online/"}};
}
} // namespace

const std::string &serverInstructions()
{
	static const std::string text = std::string(
		"Globulation 2 is a real-time strategy game of indirect control: your globules "
		"work, explore and fight on their own, and you steer them with buildings, flags, "
		"zones, requested unit counts and Swarm production ratios. Keep Inns supplied "
		"with wheat or units starve; win by eliminating every non-allied colony.\n"
		"These tools operate the real game window. Workflow: ui_status; "
		"ui_acquire_control (keep the token); ui_inspect {\"compact\":true} for the "
		"screen name and controls; ui_act on a control's surface, key and revision; "
		"ui_wait for screen changes; in a match, game_world reads your colony and what it "
		"sees in map tiles and game_place / game_view act on tiles; ui_screenshot plus "
		"ui_click with its capture_id for other map positions; ui_release_control when "
		"done. Never retry input calls.\n"
		"New here? From main_menu activate Tutorial (menu/1), then Introduction "
		"(tutorial/lesson/0); a first launch may instead offer intro/start. Call game_guide for rules, screens, the HUD, placement "
		"and errors (topics: ") +
		[]
		{
			std::string names;
			for (const auto &t : Topics)
				names += (names.empty() ? "" : ", ") + std::string(t.name);
			return names;
		}() +
		").\nSource: " + RepositoryUrl + " ; MCP guide: " + ServerGuideUrl;
	return text;
}

std::vector<std::string> guideTopics()
{
	std::vector<std::string> names;
	for (const auto &t : Topics)
		names.emplace_back(t.name);
	return names;
}

Json guide(const std::string &topic)
{
	for (const auto &t : Topics)
		if (topic == t.name)
		{
			Json result = {{"topic", t.name}, {"summary", t.summary}};
			if (t.text)
				result["text"] = t.text;
			result["links"] = links();
			Json others = Json::array();
			for (const auto &o : Topics)
				if (std::string(o.name) != t.name)
					others.push_back({{"topic", o.name}, {"summary", o.summary}});
			result["other_topics"] = std::move(others);
			return result;
		}
	std::string names;
	for (const auto &t : Topics)
		names += (names.empty() ? "" : ", ") + std::string(t.name);
	throw std::invalid_argument("Unknown guide topic \"" + topic + "\"; use one of: " + names);
}
} // namespace MCP
