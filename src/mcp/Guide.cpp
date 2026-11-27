// SPDX-License-Identifier: GPL-3.0-or-later
// What an MCP client needs to play without the source: keep every statement here
// true to the game's current behavior, and update docs/development/mcp-ui-server.md
// alongside it.
#include "Guide.h"
#include <algorithm>
#include <cctype>
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
3. ui_inspect: the current screen name and its controls (compact by default).
4. ui_act {token, surface, key, revision, action:"activate"}: press a control; add
   wait_for {"screen": NAME} (or "next_screen") when it changes screens.
5. ui_wait {"condition": {"screen": "game_session"}}: wait for a screen change.

How to start (first moves): from main_menu, either
- a guided first match: Tutorial (menu/1), then Introduction (tutorial/lesson/0); its
  lesson card tells you each step; or
- a custom game against the computer: Custom game (menu/3) opens custom_game; on its
  Players & Teams tab (tab/1) set colony/N/controller to AI and colony/N/ai to an AI
  (select by label, e.g. "Numbi"), then activate start with wait_for
  {"screen": "game_session"}. game_guide topic screens has the details.

Screens you will meet: main_menu, tutorial_catalog, custom_game, landscape_picker,
game_session (a match) and end_game (results).

Leaving a match (quit, surrender, abandon): game menu (ui_key Escape or game/menu),
then quit. It counts as a loss and asks nothing.

Other topics: gameplay, screens, hud, actions, control, hive, troubleshooting, links.
Strategy, in plain advice with tradeoffs: strategy-economy, strategy-food-conversion,
strategy-scouting, strategy-defence, strategy-attack, strategy-training,
strategy-terrain, strategy-timing.)"},
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

Conversion. An Inn offers happiness: 1 when it can feed (has wheat and room), plus 1
for each fruit (cherries, oranges, prunes) it holds more of than units inside; 0 when
it cannot feed. If a non-allied colony shares its food vision with you (an Alliance
dialog option), each hungry unit of yours weighs that colony's Inns it can reach before
starving. It eats at your nearest Inn offering at least the best such happiness;
when none does, it joins that colony on the spot. Inns that have just started
feeding cannot convert for 150 ticks. Keep every Inn stocked with wheat and fruit:
game_world lists each Inn's stock and happiness and counts units converted away.

Victory. A colony is eliminated when it has no units or production left that could
recover. In a custom game you win by eliminating every non-allied colony; the Game
Rules tab can add prestige, a time limit or an early win when one side's win chance
passes a threshold (95% by default). Tutorial lessons end when their objectives are
done; the Objectives button shows them.)"},
	{"screens", "Menus and how to start a tutorial or a custom game",
	 R"(inspection.screen names the active screen; ui_act wait_for and ui_wait refuse names
that no screen has, listing those there are. Common names:
introduction_offer: shown on a new profile's first launch; intro/start begins the
  introduction lesson, intro/menu goes to the main menu.
main_menu: buttons Custom game (menu/3), Play online (menu/4), Campaign (menu/0),
  Load game (menu/2), Tutorial (menu/1), LAN Game (menu/5), Settings (menu/6),
  Editor (menu/7), Credits (menu/8).
Online: with the MCP server enabled, quick match searches only the server's MCP
  queues, so you meet other players who have MCP enabled, never players without it.
  Online matches refuse game_* and hive_* tools; drive them with ui_* tools.
tutorial_catalog: one button per lesson, tutorial/lesson/0 (Introduction) to
  tutorial/lesson/9; tutorial/back returns to the main menu.
custom_game: tabs Map (tab/0), Players & Teams (tab/1), Game Rules (tab/2); start
  ("Play this map") launches the match, back returns. Its settings are kept between
  matches. Setting up a game against computer opponents:
  - Map tab: map/mode/1 (Random map), then generator/width and generator/height
    (for example select "128") and generator/colonies (select "2" for 2 colonies).
    generator/landscape opens landscape_picker (below).
  - Players & Teams tab: colony/N/controller (You or AI) and colony/N/ai, the
    AI and its strength; players/ai sets every computer colony at once. Colony
    colours are named once the map preview exists.
  - Game Rules tab: Teams Fixed and the win conditions.
  Choices list their options (long ones only option_count). select takes an option
  label, any unique start of one ignoring case (Maxima for an AI labelled
  Maxima - Hard (1873), whose rating changes), or its index; an ambiguous start is refused with the matches.
  A disabled control may carry disabled_reason, such as why start cannot launch yet.
landscape_picker: landscape/choice lists every landscape by name: ui_act select
  {"value": "Fjord continent"} picks one and scrolls its card into view, even when it
  is scrolled out of sight (a hidden landscape/N card itself cannot be activated:
  hidden controls are refused until scrolled into view). Then activate landscape/use,
  enabled once that landscape's preview map is ready (until then its disabled_reason
  says so); landscape/back cancels.
game_load: the saved games list (Load game).
game_session: a match in progress (see hud). The in-game menu, objectives and
  alliance dialogs appear as extra surfaces on this same screen.
end_game: results after a match; quit returns to custom_game with its settings kept.
  While it is shown, game_stats returns every team's statistics, the outcome and the
  overview figures; inspection reads each team's overview row (overview/team/N) with
  its column names.
Quitting a match (leave game, quit match, surrender, abandon, concede):
  open the game menu (ui_key Escape, or ui_act game/menu), then activate quit. In a
  local match it ends at once, without asking, and counts as a loss; end_game follows.
Others include settings, campaign_main_menu and map_editor.

Surface instance numbers change whenever a screen is rebuilt, so re-read them after
every transition. A control's revision changes only when its meaning does (label,
value, enabled, options, actions, shown or hidden), not when it moves. ui_act returns
the inspection from the moment its input was consumed, which can still be the old
screen: pass wait_for {"screen": NAME} (or "next_screen") so it answers once the new
screen shows, rather than a separate ui_wait. A lesson or scenario may open with a
briefing dialog: press its ok.)"},
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
game/speed is a stepper: set_value -3..10 (0 is normal speed, 2 is 1.6x, 10 unlimited;
values outside are refused before anything is sent). activate steps through the
chevron presets 1x, 2x, 4x, 8x, unlimited and back to 1x (levels 0, 3, 5, 7, 10), so
from 0 it jumps to 3; use set_value for any other level.

Construction palette buttons are game/buildings/<type>.0.site, labelled with the
building's name (disabled when the lesson or rules do not allow that building yet). Flags are game/flags/<type>.0.finished;
zones game/zone/0..2 (Forbidden, Guard, Clearing) with brush mode game/brush/mode/0..1
(paint, erase) and brush sizes game/brush/figure/0..7.
Selecting a building or flag on the map replaces the panel with its details, keyed
building/<id>/<generation>/...: workers ("Assigned workers": units requested, of
the flag's kind on flags), priority/-1..1,
range (flags), production/0..2 (Swarm ratios for Workers, Explorers, Warriors),
construction (upgrade or repair) and destroy. game_world lists your buildings with the
stock the panel shows, your units and what your colony sees, in map tiles; skills and
other details come from hive_run.
Lesson card (tutorial only): a separate surface with the step text, intro/destination
(marks the target on the map) and intro/help. Its body key (intro/body/N) changes
with each step, so wait on the new key to detect progress.)"},
	{"actions", "Semantic actions versus coordinate gestures, placing and selecting",
	 R"(Prefer ui_act for anything that is a control: it targets {surface, key, revision},
is checked again when the game consumes it, and needs no coordinates. Actions:
activate (buttons, options, tabs), select (a choice's option label, a unique start of
one, or its index), set_value
(slider/stepper integer, toggle boolean, text string) and scroll (signed pixels for a
scroll container; scroll one into view before acting on a control it hides).

In a match, think in map tiles. game_world reports your status (free/total units,
win chance), alerts with their tiles (losses and conversions first, then attacks),
losses (buildings destroyed, units converted away), your buildings with ids, stock
and Inn happiness, unit counts and warrior training, known resources with their
nearest tile, other teams' buildings in sight and those remembered under fog, and how
recent placements went; detail brief returns only status, alerts, losses and one line
per building, cheap enough to poll every few seconds; detail alerts drops the building
list too, and since_tick (the previous reply's tick) keeps only newer alerts. Alerts
include your standing Hive programs that failed (hive_program_failed: paused ones need
hive_program resume) and other teams' buildings first seen (enemy_building_sighted).
It follows fog of war: only what your colony sees or has seen, never another team's
health or training. game_place puts a building site or flag with its top-left on a
tile through the click path (near searches around it for room; buildings need
explored ground). game_building changes a building or flag by id: workers, priority,
Swarm production, flag range, upgrade, repair, destroy, move a flag. game_zone paints
or erases forbidden, guard or clearing zones over a tile rectangle. game_view centres
the camera on a tile and returns its screen point; zoom (1 is 100%, up to 5, 0 fits
the whole map) frames it for anyone watching, and width/height frame a whole tile
rectangle (a battle, a base). game_chat
sends an in-game chat message as your player, at most 254 bytes of UTF-8.
game_time pauses the match while you think (action pause), resumes it, or steps a
number of ticks (30 a second at normal speed) and pauses again; status reports the
tick, whether it is paused and the speed. While paused you can still read, place,
click and type: orders queue and apply as the next ticks run, so step to see them
take effect. speed sets the top bar's game speed (-3..10, 0 normal) with any action:
slow down for a fight, speed up while waiting. It works only in local matches without
network players.
game_wait lets time pass instead of polling: up to ticks, or until something in until
happens to your colony (attack, building_lost, conversion, enemy_sighted, hive_failure,
building_completed, or economy: inn_low_food, resource_field_low, site_stalled,
flag_unstaffed), the result is announced, or timeout_ms. It answers why it stopped
with game_world's detail alerts since it began; include adds sections (buildings,
resources) and compact: true leaves out what rarely changes. Repeated attacks in one
area are one skirmish: it wakes you once, again only after 20 s of quiet there.
Economy alerts (also in game_world) come from your own colony only: an Inn nearly out
of wheat with hungry units around it, a site with no progress for a minute, a flag
nobody has taken for 30 s, a field you saw fall to a quarter of its size. Numbers above
a tool's limit are capped and reported under settings, and building names may be
written loosely ("exploration flag").
game_alliance sets allied and shared map, food and market vision with one other team,
as the Alliance dialog does. Shared food vision lets that team's hungry units see your
Inns: they join you when your Inn offers more happiness than theirs. To concede, open
the game menu (ui_key Escape) and activate quit; it counts as a loss.
game_stats charts your colony over time (population, food, work, buildings,
military) as the Statistics panel does; after the match, on end_game, it covers
every team and the win probability history, to see why a match went the way it
did. list: true names the metrics.
The map, minimap and replay bar are canvases: use coordinates. Take ui_screenshot
(max_width keeps it small) and pass its capture_id with x/y in that image's pixels,
or pass space "logical" with x/y from ui_inspect bounds or game_view's tile_screen.
To click a control without coordinates, ui_click {key} finds its centre.
- Place by hand: activate a palette button, then ui_click on the map. The tool stays
  armed after each placement, for further clicks (game/placement shows it); ui_key
  Escape or a right click disarms it. The click's reply has a placement note: the site
  is ordered there if the ground is explored, free and buildable, and appears in
  game_world buildings; game_place instead reports refusals with reasons. The red
  outline left over a new site is the armed tool's preview: that tile is now taken.
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
game_wait, game_time status, game_stats, hive_status, hive_api, hive_result) need no control. Everything that sends input needs the token
from ui_acquire_control. Only one client holds control; a second acquisition fails
until the first releases it, its lease expires, or the person at the computer uses
Settings > MCP server > Revoke control. The lease lasts 300 seconds after the last
token-bearing call; ui_renew_control extends it. "Controller lease lost" means no one
holds control any more: acquire again. "Token does not match the current control
lease" means another token (another client, or an earlier acquisition of yours) holds
it: use the right token, or wait. Human mouse and keyboard input keeps working alongside yours.
Input reaches the game even while another app has the OS focus; switching windows
only cancels input still in flight. Holding an arrow key to scroll needs the focus:
use game_view to move the camera instead.
The match keeps running while you think; game_time pause holds it (local matches).
Never retry an input call automatically: it may have been delivered before an error
or timeout. Inspect first, then decide. ui_release_control releases held keys and
buttons and frees the lease when you finish.)"},
	{"hive", "Reading the world and scripting your colony with JavaScript",
	 R"(Hive tools run short JavaScript against your own colony during a local match.
hive_status reports availability and the current session id (a match identity);
hive_api returns the full API contract, including world queries such as
ctx.game.units(), ctx.game.buildings() and order descriptors. hive_run executes one
script (token, session, a fresh operation_key, source) and hive_result fetches its
output; pass wait: true to either to get the finished result in one call. hive_api is
a reference to browse: no arguments lists its sections, section reads one (contract,
helpers, local-control, limits, types: the TypeScript declarations of every record and
order), search finds paragraphs. It is built into the game: no source tree needed. The helpers section covers
names instead of numbers (units({team: "mine", type: "warrior"}), create orders by
building name), map.scan for cheap whole-map reads, map.findPlacement, map.distance on
the wrapping map and ctx.game.events() for what happened to your colony.
hive_program installs standing scripts that run every N ticks; a program that fails
is paused and reported in game_world alerts. Tiles give corners (terrain ids per
map: look them up in ctx.game.terrainTypes(), which says buildable) and a resource
type to look up in ctx.game.resourceTypes(); 65535 means none. The game_*
tools cover the common reads and orders without a script; use Hive for full records
(stocks, skills, terrain) and for standing logic that reacts between your turns.)"},
	{"troubleshooting", "What common errors mean and what to do next",
	 R"(available=false: the window is minimized, hidden or showing an OS dialog; ask the
person to restore it, then call ui_status again.
"Another client controls the game": wait, or ask the person to press Revoke control.
Stale control: the control's meaning changed (the error says what); ui_inspect
again and use the new values. A revision that only moved while the control kept its
meaning (a preview loading, a list scrolling) is accepted, and the reply's
revision_refreshed says so. "has no actions": a canvas (game/map, the minimap) or a label; use ui_click
with its key or coordinates instead of ui_act. Hidden control: scroll its container (ui_act scroll) first. Blocked control:
a dialog or overlay covers it; close or answer the dialog. A map canvas reports
blocked when any panel overlaps part of it; clicks on its uncovered part still work.
Stale viewport or expired capture: take a new ui_screenshot and use its capture_id.
Wait timed out: the condition did not happen; inspect the returned snapshot.
Invalid argument errors name the field and the rule it broke.
If nothing seems to happen in a match, check game_time status (paused, or steps
left) and that the building's requested unit count is above zero.)"},
	{"strategy-economy", "Opening and economy: food first, Inns and Swarms by fields",
	 R"(Food comes first. Every unit eats at Inns, and every new unit costs wheat at a
Swarm. A colony short of wheat stops growing, then starves. On some maps the food
crop is maize, rice, potatoes or fish instead of wheat; they all feed Inns.

A simple opening:
1. Find the nearest large food field (game_world lists known resources).
2. Put an Inn right beside it. Buildings cannot stand on resource tiles, so place
   it at the edge; game_place with near: true finds room.
3. Set the Swarm mostly to Workers, for example 4 Workers, 1 Explorer, 0 Warriors
   (game_building production). Give the Swarm a few assigned workers (3 to 5) so
   wheat keeps arriving.
4. Add Inns as the colony grows. A first-level Inn seats 4 units at a time and holds
   10 wheat. When Inns sit empty or units queue to eat, add workers to that Inn or
   build another one nearer the food.
5. Make sure wood is close: every building site needs wood. Stone is needed for
   upgrades and walls, algae for the School.

Distance is the hidden cost. Each worker walks from the field to the building and
back for every load. Keep Swarms within about 6 tiles of a big field: in one tested
game a Swarm 17 tiles from its food starved while the field was full. Keep Swarms
and Inns next to food, not next to each other.

Do not ask for more than you have. The top bar shows free/total units; a negative
free count means requests are unmet. Keep a few free workers: idle units are the
ones that go to training buildings.

Look after your fields. Food regrows only on tiles that still hold some and spreads
from them. Workers tend to strip a field from its near edge, and an emptied tile
comes back only if a neighbour seeds it. Painting a forbidden checkerboard over part
of a big field (game_zone) keeps seed tiles alive. Cost: less food now. Gain: the
field lasts. Do not build over the fertile strip between a field and its water.

Expanding. When the home field is fully used, or Inns keep running dry, start a
second Swarm on another big field with its own Inns. A Swarm site needs 35 wheat
delivered before it is built.
- Pros: more production, short hauls, a second base if the first is raided.
- Cons: the site costs food and workers now, and spread-out bases are harder to
  guard.

One Inn cluster or Inns at every field?
- One cluster near the main field is easy to defend and supply, but units working
  far away walk a long way to eat.
- An Inn at every field cuts walking and hunger, but each one needs its own
  workers and is one more target for raiders.
A common middle path: a cluster at home plus one or two Inns at each outlying field.

Swarm ratios over the game. Early: almost all Workers, one Explorer per four or so
Workers until the map is known, then fewer Explorers. When you start building an
army, add Warriors to the ratio (for example 4 Workers, 1 Explorer, 3 Warriors)
rather than switching every Swarm to Warriors at once: the army still needs
workers to feed it.)"},
	{"strategy-food-conversion", "Food, Inn happiness, fruit and how units change sides",
	 R"(Units do not only die in battle; they also change sides. A hungry unit can walk to
an enemy Inn and join that colony. Winning the food game often decides the match.

The rule in short (see gameplay for the exact version): a hungry unit compares the
happiness of its own Inns with the Inns of any colony that shares its food vision
with your colony. An Inn offers 1 happiness when it has wheat and a free seat, plus
1 for each fruit (cherries, oranges, prunes) it holds more of than units inside,
and 0 when it is empty or full. If an enemy Inn offers more than any of yours, the
unit joins the enemy on the spot.

What this means in play:
- Keep every Inn stocked. An empty Inn offers 0. Raise its assigned workers, set its
  priority to high, or build it closer to food.
- Keep enough seats. A full Inn also offers 0. Add Inns or upgrade them: a
  second-level Inn seats 7 and holds 30 wheat, a third-level one 17 and 50.
- Fruit counts. Each fruit kind in an Inn adds 1. An enemy whose Inns hold fruit can
  pull your units over even while your Inns have wheat. Workers bring fruit to Inns
  on their own, but only from fruit trees your colony can currently see: keep an
  Inn or other building near the trees, or an Exploration Flag on them.
- Fruit has a cost. A unit that ate fruit loses armour, 10 points per fruit kind for
  a Warrior, whose base armour is only 10. Warriors that ate fruit die faster.
  Fruit is strongest as a conversion tool and as protection against conversion.
- Never leave an army hungry near enemy Inns. Units far from home may not reach
  any of your Inns in time, and then any enemy Inn they can see wins.

Sharing your food vision. In the Alliance dialog (or game_alliance) you can share
your Inn vision with an enemy. Then the enemy's hungry units can see your Inns and
join you when yours offer more happiness. This only lets their units come to you;
it does not let your units go to them. In tested games, sharing it from the start
turned conversion from a net loss into a net gain of dozens of units. The enemy can
do the same to you, which is why your Inns must stay stocked.

Killing enemy Inns is a conversion weapon too. When an enemy loses its Inns, its
units go hungry, and if your Inns are visible to them and happier, they walk over.

Signs of trouble: the top bar's converted-from count rising, conversion alerts in
game_world or game_wait (until: conversion), and Inns showing happiness 0. Fix the
nearest Inn first: alerts give the tile where the unit left.

An Inn that has just started feeding cannot convert units for 150 ticks.)"},
	{"strategy-scouting", "Scouting: Explorers, Exploration Flags and reading what you see",
	 R"(You can only act on what your colony has seen. Scouting answers three questions:
where is more food, where is fruit, and where is the enemy.

Explorers fly, so water and forests do not stop them. They wander on their own;
an Exploration Flag (range up to 20) sends them to an area.
A flag can be placed in unexplored fog; game_world placements confirms it.

How many: one or two Explorers early; as the colony grows, about one per four
Workers while most of the map is unknown, then fewer once you know the map.
Explorers eat like everyone else, and hungry Explorers far from home can change
sides. Explorers alone do not keep a colony alive.

What to look for, in order:
1. Food fields near home, for Inns and a second Swarm.
2. Fruit trees. Fruit makes Inns happier (see strategy-food-conversion), and workers
   can only harvest fruit your colony can see right now.
3. The enemy: its Swarm and Inns. An early sighting lets you choose between
   attacking early and building up.
4. Stone and algae for upgrades, and the routes between you and the enemy.

Reading the picture:
- game_world lists other teams' buildings in sight and those remembered under fog,
  and enemy_building_sighted alerts. game_wait until: enemy_sighted wakes you when
  something new appears.
- Conversion alerts carry a tile. Units leaving toward one spot often point at an
  enemy Inn you have not found yet.
- What you see is a minimum, never the whole enemy. A colony that looks like 40 units
  can have 200 behind the fog. The win chance in the top bar includes what you cannot
  see; trust it more than a count of visible units.
- Remembered buildings may be gone, and the enemy tends to rebuild near its old
  base. Send an Explorer back now and then.

Keep looking during a war. Units parked over the enemy base show new Inns and
Swarm sites as they start, when they are still easy to kill.

Tradeoff: every Explorer is a unit that is not hauling food. Too few and you miss
fields and attacks; too many and the economy slows and you lose some to hunger. On
a famine map keep only one or two.)"},
	{"strategy-defence", "Defence: guard zones, home warriors, towers, walls and Hospitals",
	 R"(What the enemy wants most is your Inns and your Swarm. Lose the Inns and your units
starve or walk over to the enemy; lose the Swarm and you cannot replace losses.
Defend those first.

Warriors at home. Two ways to keep them there:
- Guard zone (game_zone guard): free Warriors, ones not taken by a flag, walk to
  the nearest guard zone and fight enemies that come into it. Paint it over your
  Inns, Swarm and the way in. All free Warriors go to the zone nearest them, so a
  second zone far away may stay empty.
- A War Flag at home with a set number of Warriors (for example 8) that stays when
  the rest of the army leaves. Easy to count and to move.
Keep some defence even while attacking: a raid of a few warriors can kill an
unguarded Inn.

Defense Tower. Built with 6 wood, it then shoots with stone that workers carry to
it. It holds 12, 16 or 20 shots by level and hits for 30, 40 or 50 within 5, 7 or 9
tiles of its sides. It shoots over walls and water and needs no line of sight. Its
armour (8, 12, 15 by level) means untrained Warriors barely scratch an upgraded
tower: about 5 damage a hit on the first level, 1 on the higher ones.
- Pros: eats no food, cannot be converted, strong once upgraded.
- Cons: fixed in place, useless without stone and workers to carry it, costs wood
  and stone to upgrade. A repair turns it back into a site that cannot shoot, so
  repair it only when no enemy is near.
Put towers beside Inns and on the approach, where your workers can reach stone.

Stone Wall. One tile, 4 stone, 500 hit points, armour 10. Walls stop walking, not
shooting. Use them to close a narrow gap so attackers must come past your towers or
guard zone. They block your own units too, so leave a way out to your fields.

Hospitals. Wounded units walk to a Hospital to heal. Without one, hurt Warriors
spend their time looking for healing instead of fighting. A first-level Hospital
seats 2, second 5, third 7. Build about one per 8 to 10 Warriors, close to where
fighting happens.

Towers or Warriors?
- Warriors move, chase raiders and can attack later, but eat, need Barracks
  training to be strong, and can be converted if hungry.
- Towers hold one spot cheaply in food but need stone supply and workers.
Most strong colonies use both: towers at Inns and choke points, Warriors in a guard
zone behind them.

Other habits:
- Upgraded Inns have more hit points (200, 500, 700) and armour.
- Guard zones over your fruit trees keep raiders off your happiness supply.
- game_wait until: attack or building_lost wakes you when a fight starts; alerts
  give the tile. Move your home War Flag there.)"},
	{"strategy-attack", "Attacking: when, how many, rally flags, targets and long routes",
	 R"(You win when no enemy colony has units or production left that could recover.
Warriors do the work, steered by War Flags: a flag asks for a number of Warriors
(at most 20 per flag) and they fight anything hostile within its range (up to 8).

When to attack:
- Early (a handful of Warriors, soon after you find the enemy): the enemy usually
  has few Warriors and little training, and each Inn you kill leaves its units
  hungry, ready to join you if you share your food vision. In tested games an
  early strike of 7 to 10 Warriors on the nearest enemy Inn often decided the
  match. Risk: your economy grows slower, and a failed strike wastes the units.
- Later (trained Warriors, several flags): stronger and faster attackers, but the
  enemy has had time for towers, Barracks and its own army.
A useful rule: if a push has not killed an Inn after several minutes, bring it home,
build up (Barracks, Hospitals, more Inns) and try again.

How to attack:
1. Place a War Flag on open ground near your own Inn on the way to the enemy, as a
   rally point. Set its unit count and range in one game_building call.
2. Wait until most of the Warriors have arrived.
3. Move the flag (game_building action move) onto the target. Moving it keeps the
   Warriors already assigned; a new flag has to gather them again.
4. For a bigger army use two or three flags close together.

Targets, in order:
1. Inns. Without them the enemy starves, and its hungry units can join you if you
   share your food vision (see strategy-food-conversion).
2. Swarm. It replaces losses. It has 700 hit points and slowly heals itself, so
   bring enough Warriors to finish it.
3. Construction sites. They start with few hit points and die fast.
4. Barracks, Racetrack, Hospitals, then the rest.
Avoid upgraded Defense Towers with untrained Warriors: they barely hurt them.

Long routes. A fed unit walks about 260 tiles before it is hungry and about 350
before it starves. Warriors may ignore a flag too far to reach and come back from
in time. On a big map build a forward Inn, and a Hospital, near the rally point, out
of range of enemy towers, and keep the Inn stocked. A hungry army next to enemy Inns
will change sides.

Finishing. The enemy rebuilds Swarms and Inns near its old base with its last
workers. Keep one flag parked over the base, check remembered buildings, and put a
flag on every new site you find. The match ends only when nothing that could
recover is left.

Before you send the army, keep a few Warriors or a guard zone at home, and make
sure food keeps up: an attack that starves your own colony is a loss.)"},
	{"strategy-training", "Training and upgrades: what each building improves, when it pays",
	 R"(Units train on their own. A free unit with nothing to do walks to a training
building that can improve it. So training needs two things: the building, and spare
units. If every worker is busy, nobody trains.

What each building teaches (each building level trains up to the next unit level,
so a third-level building gives level 3):
- Racetrack (6 wood, 1 stone): walking speed for Workers and Warriors, from 16 to
  21, 26 and 30. Every haul, meal and march gets faster. Pays off early on big maps
  and long food routes.
- Swimming pool (8 wood): untrained Workers and Warriors cannot enter water at
  all. One level of swimming lets them cross. Needed when water separates you from
  food, stone or the enemy; otherwise it can wait.
- School (7 wood, 2 algae): Workers build faster (8 to 12, 16, 20) and harvest a
  little faster, and gain the build level needed to upgrade buildings. The
  third-level School also teaches Explorers to attack the ground, and gives
  prestige, which matters only if the Game Rules use a prestige win.
- Barracks (7 wood): Warriors attack faster (12 to 16, 22, 28) and a little harder
  (13 to 16). A fully trained Warrior strikes more than twice as often. Build it
  before a big attack, not before your economy.

Warriors also gain experience in fights and then hit a little harder.

Upgrading buildings. Select a building and choose upgrade (game_building action
upgrade). Only Workers whose build level is at least the new building level can
work on it, and build level comes only from a School. So the usual order is:
School, then upgrade Inns, Barracks and the rest.
What upgrades give:
- Inn: 4 seats and 10 wheat, then 7 and 30, then 17 and 50.
- Hospital: 2, 5, 7 seats.
- Training buildings: more seats and a higher training level.
- Defense Tower: more shots, damage, range and armour.
Upgrades cost wood and stone (some also algae), and the building is out of service
until the upgrade is done. Upgrade one building of a kind at a time and keep the
others working, especially Inns.

When it pays:
- Early game: Racetrack (if distances are long) and maybe a School. Food first.
- Before a big push: Barracks and Hospitals, and a pool if water is in the way.
- Mid game: upgrade Inns so fewer buildings feed more units.
- A short, early fight is won with numbers; a long game is won with training.
Tradeoff: training buildings cost wood and stone and take units off work for a while.
Building them too early slows growth; building them too late means facing trained
enemies with untrained Warriors.)"},
	{"strategy-terrain", "Map and terrain: water, forests, growth, choke points and distance",
	 R"(Where you can build. Buildings need open grass: no resource and no unit on any
tile, and not sand or water. A Swarm needs a 4x4 space. Wheat, wood, stone and fruit
all block building and walking.

Water. Untrained Workers and Warriors cannot enter water; a Swimming pool teaches
them. So a lake or river is a wall early and a road later. Explorers fly over it.
Defense Towers shoot across narrow water, so a tower on one bank can hit the other.

Growth. Wheat and wood regrow and spread only near water (within about 15 tiles),
and sand nearby slows them. A field far from water is a one-time stock: when it is
eaten, it is gone. Prefer fields beside water for your Inns and Swarms, and keep
buildings off the fertile strip between the field and the water.

Forests. Trees spread slowly over open grass and can close roads and building space
over a long game. Use a Clearing zone or Clearing Flag to cut a road through, or to
keep your base open. On a map that is mostly forest, start clearing early.
Wheat, wood and algae can be cleared; stone and fruit cannot.

Stone never runs out and never grows. One reachable quarry serves you all game,
so it decides where you can upgrade and build towers and walls. Algae grows in
water near a coast and is needed for the School.

Choke points. A narrow pass, ford or gap between forests is where few defenders
stop many attackers. Hold it with a guard zone, a Defense Tower or two, and Stone
Walls to make it narrower. The same gap is where your own attack can be stopped, so
scout for a second way in. Swimming opens new routes across water later.

The map wraps. Leaving one edge brings you to the opposite edge, so a base near a
corner can be close to a colony in another corner. Measure distances with the wrap
in mind (Hive map.distance does this).

Distance. Long walks cost food and time: a fed unit walks about 260 tiles before it
is hungry and about 350 before it starves, and forced detours count. On a big map:
- Expand with a second Swarm and Inns on far food instead of hauling it home.
- A Racetrack speeds every trip.
- An attack needs forward Inns and a Hospital near the rally point.
On a small map, contact comes early: scout and keep Warriors home sooner.

Reading a map at the start: find water (growth), the nearest big field, wood,
stone, fruit, and the likely route to the enemy. game_world lists known resources;
game_view zoom 0 shows the whole map.)"},
	{"strategy-timing", "Reading the game: signs you are ahead or behind and what to do",
	 R"(Check a few numbers often instead of everything rarely. game_world detail brief is
cheap enough to poll, and include picks only the sections you need (for example
["status", "alerts"]); game_wait lets time pass until something happens; game_stats
shows trends, and series: false gives just the latest, peak and low figures.

Signs you are ahead:
- Win chance in the top bar rising (it accounts for what you cannot see).
- Converted-to-you count growing faster than converted-from.
- Population and food harvested still climbing; Inns stocked and happiness above 0.
- Enemy buildings in sight going down, and you losing none.
What to do: keep pressure on Inns and new sites, keep your own food ahead of your
army, and do not leave home empty.

Signs you are behind:
- Win chance falling while you see little: the enemy is growing out of sight.
- Units converting away, especially more than a few per minute.
- Inns empty or full (happiness 0), or many units starving.
- Free workers deeply negative: you asked for more than you have.
- Many idle workers: they have no jobs, or no Inn or field near them.
- An attack that has stalled for several minutes next to enemy Inns.
What to do, in order:
1. Fix food: add or move Inns to fields, raise Inn workers and priority, cut
   requests you do not need, add Workers to the Swarm ratio.
2. Stop the bleeding: pull a hungry army back to your own Inns, put a guard zone
   over your Inns, and keep them stocked with fruit if any is in sight.
3. Build up: Barracks, Hospitals, Inn upgrades, then attack again.

Early, middle, late:
- Early: food, Inns, a Swarm mostly of Workers, a scout. Find the enemy.
- Middle: second Swarm on a new field, Racetrack or School, first Warriors and a
  guard zone. Decide: strike early while the enemy is small, or build up.
- Late: Barracks and Hospitals, upgraded Inns and towers, several War Flags,
  forward Inns on long routes.

Tradeoffs to weigh each time you check:
- Attack now or grow? An early strike works best while the enemy has few Warriors
  and before it trains them; growing works best if the enemy is far or behind water.
- More Workers or more Warriors? Warriors do not gather food. Too many starves the
  colony; too few loses Inns to the first raid.
- New building or upgrade? New buildings add capacity now; upgrades add more per
  building but take it out of service for a while.

Use game_wait with until (attack, building_lost, conversion, enemy_sighted) so you
are woken by events instead of polling. When a game is clearly lost (win chance
near 0, conversions running against you, no food), conceding saves time; it counts
as a loss.)"},
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
		"ui_acquire_control (keep the token); ui_inspect for the "
		"screen name and controls; ui_act on a control's surface, key and revision; "
		"ui_wait for screen changes; in a match, game_world reads your colony and what it "
		"sees in map tiles, game_stats its statistics over time (every team's on end_game) "
		"and game_place / game_view act on tiles; game_wait lets game time pass until "
		"something happens; game_time pauses the "
		"match while you think or sets its speed; ui_screenshot plus "
		"ui_click with its capture_id for other map positions; ui_release_control when "
		"done. Never retry input calls.\n"
		"Screens (inspection.screen): main_menu, tutorial_catalog, custom_game, "
		"landscape_picker, game_session (a match), end_game; game_guide topic screens "
		"describes each and their keys.\n"
		"New here? From main_menu either activate Tutorial (menu/1), then Introduction "
		"(tutorial/lesson/0), for a guided match (a first launch may instead offer intro/start); "
		"or play a custom game against the computer: Custom game (menu/3), set colonies to AI "
		"on Players & Teams (tab/1), then start with wait_for {\"screen\": \"game_session\"}. "
		"To leave a match: game menu (Escape or game/menu), then quit (a loss, no confirmation). "
		"Call game_guide for rules, screens, the HUD, placement and errors (topics: ") +
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

const std::vector<std::string> &knownScreens()
{
	// Kept in step with the screens' recordingId() overrides; the in_game_* dialogs are
	// surfaces of game_session, never a screen of their own.
	static const std::vector<std::string> names = {
		"main_menu", "introduction_offer", "tutorial_catalog", "custom_game", "landscape_picker",
		"game_session", "end_game", "game_load", "settings", "match_start", "start_quality",
		"choose_map", "custom_game_choice", "custom_game_other_options", "generator_studio",
		"campaign_main_menu", "campaign_menu", "campaign_selector", "campaign_editor",
		"campaign_map_entry_editor", "online_hub", "online_profile", "online_maps",
		"online_generators", "quick_match", "match_found", "multiplayer_room", "room_map_picker",
		"map_share", "lanmenu", "lanfind", "lansession", "load_save_dialog", "message",
		"ask_for_text_input", "credit", "audio_preference", "hand_preference", "music_library",
		"music_set", "music_import", "recording_files", "building_library", "set_library",
		"map_editor", "map_edit_menu", "editor_main_menu", "editor_dock", "editor_load",
		"editor_generate", "editor_progress", "editor_confirm", "new_map", "script_editor",
		"teams_editor", "screen", "dialog", "unavailable"};
	return names;
}

std::vector<std::string> guideTopics()
{
	std::vector<std::string> names;
	for (const auto &t : Topics)
		names.emplace_back(t.name);
	return names;
}

Json guideSearch(const std::string &query, const std::string &onlyTopic)
{
	const auto lower = [](std::string text)
	{
		for (auto &c : text)
			c = char(std::tolower(static_cast<unsigned char>(c)));
		return text;
	};
	std::vector<std::string> words;
	for (std::size_t i = 0, j; i < query.size(); i = j + 1)
	{
		j = query.find(' ', i);
		if (j == std::string::npos)
			j = query.size();
		if (j > i)
			words.push_back(lower(query.substr(i, j - i)));
	}
	if (words.empty())
		throw std::invalid_argument("search needs at least one word");
	// Whole words: "ai" must not match "main". A word may end a longer form ("inn"
	// matches "Inns").
	const auto hasWord = [](const std::string &text, const std::string &word)
	{
		for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
		{
			const bool startOk = at == 0 || !std::isalnum(static_cast<unsigned char>(text[at - 1]));
			const auto end = at + word.size();
			const bool endOk = end >= text.size() || !std::isalnum(static_cast<unsigned char>(text[end])) ||
							   text[end] == 's';
			if (startOk && endOk)
				return true;
		}
		return false;
	};
	struct Hit
	{
		std::string topic, text;
		std::size_t score;
		bool lead; // a word starts the block: a heading such as "A simple opening:"
	};
	std::vector<Hit> hits;
	for (const auto &t : Topics)
	{
		if (!t.text || (!onlyTopic.empty() && onlyTopic != t.name))
			continue;
		// Each hit is the block its words sit in, so a heading comes with its steps: a
		// paragraph between blank lines when it is short; in a long one, three lines
		// extended over the list or indented lines that continue them.
		std::vector<std::string> lines;
		const std::string text = t.text;
		for (std::size_t i = 0, j; i <= text.size(); i = j + 1)
		{
			j = text.find('\n', i);
			if (j == std::string::npos)
				j = text.size();
			lines.push_back(text.substr(i, j - i));
		}
		const auto listItem = [](const std::string &line)
		{
			if (line.rfind("- ", 0) == 0)
				return true;
			std::size_t digits = 0;
			while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits])))
				++digits;
			return digits > 0 && digits + 1 < line.size() && line[digits] == '.' && line[digits + 1] == ' ';
		};
		std::vector<std::pair<std::size_t, std::size_t>> blocks; // [first, last)
		for (std::size_t start = 0; start < lines.size();)
		{
			if (lines[start].empty())
			{
				++start;
				continue;
			}
			std::size_t end = start;
			while (end < lines.size() && !lines[end].empty())
				++end;
			if (end - start <= 16)
				blocks.push_back({start, end});
			else
				for (std::size_t i = start; i < end;)
				{
					std::size_t last = std::min(end, i + 3);
					bool list = false;
					for (std::size_t k = i; k < last; ++k)
						list = list || listItem(lines[k]) || lines[k].ends_with(":");
					while (last < end && (lines[last].starts_with(" ") || (list && listItem(lines[last]))))
						++last;
					blocks.push_back({i, last});
					i = last;
				}
			start = end;
		}
		for (const auto &[first, last] : blocks)
		{
			std::string block;
			for (std::size_t k = first; k < last; ++k)
				block += (k == first ? "" : "\n") + lines[k];
			const auto folded = lower(block + " " + t.name);
			std::size_t score = 0;
			for (const auto &w : words)
				score += hasWord(folded, w);
			if (score)
			{
				const auto head = lower(lines[first]);
				bool lead = false;
				for (const auto &w : words)
					lead = lead || hasWord(head, w);
				hits.push_back({t.name, block, score, lead});
			}
		}
	}
	// Paragraphs with every word first; with none, the ones with the most words.
	// Paragraphs with every word first; among equals, those a word heads.
	std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b)
					 { return a.score != b.score ? a.score > b.score : a.lead > b.lead; });
	const bool all = !hits.empty() && hits.front().score == words.size();
	Json results = Json::array();
	for (const auto &h : hits)
		if ((!all || h.score == words.size()) && results.size() < 10)
			results.push_back({{"topic", h.topic}, {"text", h.text}});
	Json result = {{"search", query}, {"results", results}};
	result["note"] = results.empty() ? "No paragraph mentions these words; try fewer or other words, or read a topic."
				   : all			 ? "game_guide topic reads a whole topic; hive_api documents colony scripting."
									 : "No paragraph has every word; these have the most.";
	return result;
}

Json guide(const std::string &topic)
{
	for (const auto &t : Topics)
		if (topic == t.name)
		{
			Json result = {{"topic", t.name}, {"summary", t.summary}};
			if (t.text)
				result["text"] = t.text;
			if (topic == "overview" || topic == "links")
				result["links"] = links();
			// Summaries of every topic on the overview; elsewhere just their names.
			Json others = Json::array();
			for (const auto &o : Topics)
				if (std::string(o.name) != t.name)
					others.push_back(topic == "overview" ? Json{{"topic", o.name}, {"summary", o.summary}}
														 : Json(o.name));
			result["other_topics"] = std::move(others);
			return result;
		}
	std::string names;
	for (const auto &t : Topics)
		names += (names.empty() ? "" : ", ") + std::string(t.name);
	throw std::invalid_argument("Unknown guide topic \"" + topic + "\"; use one of: " + names);
}
} // namespace MCP
