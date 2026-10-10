# Touch gameplay controls

Inspect colonies, manage work and use flags/zones through the compact gameplay interface.

## Gameplay controls

### Inspect resources

Tapping a resource in the touch HUD opens an information header with its
localized name, resource sprite and current/maximum material stocks. Compact
resource inspection uses the shared building identity header below the top stats,
with the same icon, centered text and close button layout. Close dismisses it;
choosing Build, Flags or Tools replaces it with that toolbox. A depleted resource
closes its inspector. Resource headers do not dispatch tactical commands.

### Objectives and teams

Objectives/Hints and Teams dialogs leave at least 16 screen points around the
painted panel inside the safe, keyboard-adjusted area. Short objectives and hints
size to their content; long pages scroll within the available height, with the
action button kept reachable. The Teams table, heading and explanations scroll
together in both touch and classic presentation, leaving its footer visible even
on short desktop windows. Touch widths are capped at 560 points for
Objectives/Hints and 640 for Teams.

### Pan the map

Gameplay map drags start after 8 screen points of travel. Release momentum requires
reaching 16 points from the gesture start, so small touch jitter and short
positioning drags stop on release. This distance is independent of map zoom and
display density; deliberate swipes retain the configured momentum.

### Toolbar and statistics

The gameplay toolbar opens build choices, flags/zones, tactical tools, objectives,
alliances and the session menu. Its six cells share a continuous themed surface,
with display-density outline icons and labels rather than desktop sprite buttons.
Open build, flag and tool panels have a gold indicator above their icon. Replay
pause/play and fast-forward use the same stroke style. The last cell of the stat
grid holds the game-speed
chevrons and the simulation tick rate: a tap steps through 1x, 2x, 4x, 8x and
maximum and wraps to 1x (the desktop top bar has the same control, where a right click steps
back down). The rate is a rolling three-second average refreshed once per second,
with one decimal below 25, and the cell is outlined when it falls under 75% of the
speed's target. Network games have a fixed speed, so the cell shows the rate alone.
The prestige cell pairs a trophy with the player’s score. A gold progress track
shows the combined score of all teams against the match threshold when prestige
victory is enabled. The conversions cell shows two Glob images joined by
a directional arrow. The left Glob uses the local team’s colour and the right
Glob uses a neutral colour representing other teams. A green arrow points toward
the local team for recruits; a red arrow points away for defectors. These cumulative changes of
allegiance are not births or deaths. Portrait puts speed above the minimap and
gives prestige and conversions a larger second row; landscape gives these two
readouts more width. Primary icons share a size and primary values share a type size and
baseline across the HUD. Values sit to the right of their icons with a consistent
gap and left alignment; each complete icon-and-value group is centred in its cell.
Narrow layouts scale the shared icons and type together to keep the groups inline.
Secondary prestige progress stays below that shared primary row. The continuous
opaque surface keeps the figures clear over busy terrain.
The desktop top bar shares the same Glob-and-arrow conversion renderer, with
left-aligned totals beside each icon group. Its speed control follows the wider
conversion readout, and its mouse hit area uses that same position.

### Minimap and lenses

The minimap is a separate top-right HUD component.
Tapping it centres the camera there; dragging keeps steering the camera and clamps
at the minimap's edge when the finger leaves it. A still 400 ms press on it, or the
map lens, opens a map peek: a large minimap over the dimmed map that steers the
camera while dragged, with Done, zoom out and zoom in (nearest the thumb) below
it (beside it, zoom in lowest, on landscape screens); a tap outside, Done, focus
loss or rotation closes it. On compact layouts the
Tools button opens a lens strip opposite the thumb corner instead of the tactical list:
No overlay and the four overlays (mutually exclusive), health bars, statistics,
the map peek, message history, map marks and chat, each running the same
`menuAction` as the list. Once the strip closes, a legend in the thumb-side corner names
the active overlay and shows its intensity ramp (`OverlayArea::colorOf`). The
statistics lens opens a sheet above the toolbar with the end-of-game chart for the
player's own team only (opponents' histories stay hidden until the match ends),
metric arrows under the thumb and current counters; × or pulling it down closes
it. Spacious layouts and replays keep the tactical list;
phone palettes float over the camera, while spacious touch layouts keep a
content-sized palette open at the right. Both preserve the camera framing and
leave the world visible below short panels. In-game surfaces use `InGameTouchTheme.h`, and dialogs over a match use the
matching `inGameTheme()`; frontend paper styling remains independent. A completed tap on empty map space dismisses open toolboxes,
statistics and inspection together, without reopening a previous palette. An
outside tap on the map peek likewise dismisses its underlying tools; its explicit
Done button can return to them. Tapping another object switches selection;
panning, cancelled gestures and taps inside the inspector do not dismiss it.
Painting and placement keep their tool-specific map gestures. Choosing Build,
Flags or Tools explicitly replaces the current inspector; deferred restoration or
selection invalidation cannot override that toolbox choice on the next frame.

### Zoom with one finger

The game is playable with one thumb. A completed map tap that does not dismiss
a panel arms one-finger zoom for the next contact that lands within 300 ms of the
release and 24 points of the tap.
Dragging that contact vertically zooms about the point where it landed, doubling
per 180 points of travel, with the factor shown above the finger; releasing it
without travel doubles the current zoom there, capped at 3×. The tapped world
point stays under the finger, including across map seams. A contact that sets off
mostly sideways pans instead, so a quick tap followed by a pan still pans. The drag direction
follows the platform's map app (Android: drag down zooms in; iOS and desktop:
drag up zooms in) unless the One-finger zoom setting overrides it. A second finger,
focus loss or rotation ends the gesture and keeps the zoom reached so far;
pinching still adjusts zoom continuously. Touches on controls do not arm zoom, and
placement taps never do. In paint mode a single tap is held for the same window
before it paints, because it may be the first half of a zoom; drags paint on
release as before. A held tap lands when the window closes, when any other contact
begins, or on interruption, and is dropped only if its brush is no longer active.

### Select units and flags

In the mobile/touch interface, flags on the flat map also accept selection within 30 screen points of their
centres, independent of zoom, growing linearly to 36 points within 96 points of a
screen edge. Points follow the platform's density-independent unit. The larger edge reach
keeps near-edge flags easier to select. The constants
live in `InGameTouchTheme.h`. Exact flag hits retain priority; the extra halo
does not override direct unit/building hits and chooses the nearest flag.
Desktop mouse selection retains its original exact-tile hit area.
Unit taps also accept a 30-point radius around the interpolated unit centre,
independent of zoom. Picking uses the last drawn Scene and its smooth-motion
fraction, then validates the unit identity against the live simulation; a stale
sprite cannot select a replacement unit. Exact unit hits win over nearby units; otherwise the nearest
visible unit wins (ties use its ID). Building hits, discovered resource hits and
existing flag targets keep their priority. Hidden units cannot be selected through fog. Unit selection uses the same icon/name/owner header below the top stats as
buildings and resources. A separate opaque statistics panel presents health,
food, speed and abilities in alternating rows with label and value columns,
using the published Scene. The statistics title stays above the scrollable
body; the shared identity header owns the close button. Unit and resource
inspectors share read-only inspection dismissal: close, a
blank-map tap or an invalidated selection closes the card without restoring an
older toolbox. Presentation tracks the last shown read-only card so invalidation
in a threaded client step also closes it before rendering. Dragging near a unit
still pans the map.

### Carry flags

On touch, a contact that lands on one of the player's flags, or within that
same reach, carries the flag instead of panning the map, including straight
after a tap. Below the tap threshold it is still a tap and selects the flag.
Past it the flag follows the finger at the offset it was grabbed, so a grab
beside the flag never makes it jump. It pans the map at a world edge and lands
with one dropped move order on release. Over the HUD the flag waits where it
last was. A second finger, focus loss or rotation returns it to where it was
grabbed and ignores the rest of the touch. Spectators and replays only pan.
The torus view keeps panning, as its selection has no touch reach.

### Build and flag palettes

On compact layouts the build and flag palettes are a rail rising from the
bottom corner opposite the thumb: two columns of buildings in portrait (four in
landscape), flags and zones in one column (one row in landscape), filled
row by row from the toolbox corner. The rail is inset from the side edge, and a
rail taller than its space reveals higher rows when dragged down. The Thumb side
setting (right by default) puts these toolboxes on the left for a right thumb and
on the right for a left thumb. `ThumbSide::toolboxLeft()` supplies that opposite
side; the radial inspector and placement confirmation remain on the thumb side.

### Building and flag inspectors

When safe-area gutters or a short viewport leave too little room for the minimum
thumb dial and all action chips (including confirmation and the production
legend), the building inspector uses its scrollable row layout. Drawing and
input share this fit policy; allocation controls never expand over the minimap
to satisfy the minimum ring radius.

On compact layouts the building inspector is a thumb dial: concentric quarter
rings centred on the thumb's bottom corner. Their roles never move: workers
(0–20) outside, priority immediately inside them (Low at the bottom, High at
the top), and production proportions or flag range on the third ring. Priority keeps
its middle ring even when a building has no production or range control. Thinner bands, narrower gaps and a larger preferred radius move the
controls away from the corner. Worker and range sliders have −/+ pads and commit
once on release; a thin gold edge shows assigned workers. Normal priority uses
a neutral dot icon. The worker lane has a dark track and dark bronze target fill
with a unit icon and pale assigned/target counts embedded in the arc. The compact
“assigned / target” readout leaves room for larger numbers at every setting.

Swarm production is one arc divided into worker, explorer and warrior buttons.
Tap a button to cycle its weight through 0, 1, 2, 3, 5 and back to 0; other
weights stay unchanged. Each button occupies a fraction of the arc proportional
to its weight plus one, so zero remains visible and reachable. The buttons and
three readouts directly below the building header show the actual weights. Each
ratio button pairs its number with a team-colored unit icon; both follow the arc
and scale together to fit the sector. Zero-weight buttons show only the centered
unit icon; the header readouts retain the numeric zero.
All dial lanes use display-density antialiasing, softly rounded sector corners
and subtle edge shading; a bounded inspector-owned texture cache reuses their
shapes between draws. Existing weights outside the presets
advance to the next higher preset, wrapping to zero above five. Each completed
tap sends one existing swarm-ratio order. Interruption or dragging away cancels
it. Setting all three weights to zero stops production; tapping any unit button
establishes a new mix. There is no separate Pause button. All arc labels, ratio numbers and −/+ icons follow their curved centerlines
and fit inside their buttons, including when the thumb side is mirrored. Worker
and range readouts also follow the arc; worker readouts stay inside their own
outer ring.

Flag requirements occupy the fourth ring as radio buttons: minimum warrior or
worker level, or exploration mode. Level buttons pair the number with a
team-colored worker or warrior icon. Clearing materials occupy a fifth ring as
independent toggle buttons. Selected radio and toggle segments have a brass
fill with dark text and a brass accent along their inner edge. The rings use a larger preferred radius and sweep,
falling back to rows when the full control cannot fit. Repair/upgrade and
construction actions remain chips beside the dial in landscape and above its
quadrant in portrait. Destroy confirmation replaces all dial controls and readouts
with Cancel and Destroy arc buttons on the middle (third) ring, with
“Destroy this building?” on the next outer ring. The preferred radius is 388 points, with a 256-point
minimum before falling back to rows. Short landscape screens use a shallower
sweep to keep the arcs below the header readouts on either thumb side. The center circle shows a
trash icon and opens the existing Destroy confirmation; the confirmation button
uses a stronger red fill and a curved trash icon. Cancel abandons it and
only a separate confirmed action sends the destruction order. Tapping outside
the two confirmation buttons cancels confirmation and consumes the tap, without
activating the restored controls or map beneath it. The identity
header's close button continues to dismiss the inspector.

The compact identity header sits below the stats, matching their width and
aligning its bottom with the minimap in both orientations, including when controls
use scrollable rows. On short screens with two stat rows, the minimap uses its
larger size so the header fits without overlap. Rings shrink
to fit small screens; the map stays visible and is tappable outside the controls. Thin bands retain
expanded touch areas, with the nearest band winning where targets overlap. `dialRegions()` is the single
source for drawing, hit testing, keyboard focus and the harness, and every change
uses the same requests and orders as the Spacious row inspector, whose rows group
production ratios side by side and share one set of action boxes for drawing, hit
testing and slider geometry.


## Gameplay responsibilities and action flow

- `GameGUITouch` composes explicit bounds, routes input ownership, presents the HUD,
  and restores the previous palette after explicit building-inspector closure
  (an empty-map tap dismisses both; read-only cards never restore a palette). It never draws the desktop
  sidebar or forwards touch controls to its pixel hit tests.
- `GameGUITouchPalette.cpp` reads available building/flag choices and draws artwork
  in the opposite-thumb rail (compact) or the side grid (Spacious). Zone entries enter painting mode instead of placement.
- `GameGUITouchView.cpp` draws independently bounded HUD components, the minimap,
  tutorial, tactical panel and contextual headers. It shares primitives, not the
  desktop sidebar composition.
- `TouchInteractionSession.h` holds pointer identity, the originating palette,
  preview ownership, and buffered world-space stroke points. It contains no artwork,
  layout rules, or simulation mutations.
- `GameGUITouchPlacement.cpp` interprets palette taps and drags. Both commit through
  `GameGUIToolManager::confirmBuilding`, including its existing validation, defaults,
  ghost suppression, and order serialization. A valid drag release places once and
  restores the palette; a tap selects a movable preview with Confirm/Cancel. In the
  phone HUD, OK takes the right (thumb) half of the bar and Cancel the left; the
  legacy touch layout keeps OK on the left. `confirmRect()` and `cancelRect()` are
  the single source for drawing, hit testing and keyboard focus.
  Both patterns pan continuously while the owning finger stays near an exposed
  map edge. Speed is density- and zoom-aware, and the preview follows the camera.
  Release stops preview-mode panning without committing; a second contact,
  focus loss, rotation or selection change cancels the edge-pan contact.
  UI-covered edges do not pan.
- `GameGUITouchActions.cpp` presents the selected building identity and actions,
  reading pending values through `GameGUI::displayed*` and using shared request
  methods for allocation, priority, range, construction and destruction. Enemy
  and replay selections are read-only. Specialized controls share the same panel.
  Slider drags (workers or flag range) own a local
  allocation session and emit one command on release; a second contact, selection
  change, focus loss or rotation cancels the preview. `GameGUITouchDial.cpp` and
  `TouchDial.h` hold the dial's layout, regions and sector drawing.
- In-game dialogs (`GameGUIDialog.cpp`, `LoadSaveDialog.cpp` and the message
  history) are framework dialogs hosted by `GameGUI` on every presentation;
  touch and desktop render the same tree in the in-match theme. File operations
  keep their existing persistence and error/retry state machines.
- `EndGameScreen` owns an overview, a chart, the metric picker, team filters,
  expansion and replay export on both desktop and touch. The chart itself is
  `TeamStatChart`, shared with the compact in-match statistics sheet; what it draws
  comes from the metric catalog (`src/team/stats/`, see
  [gameplay measurements](../ai/gameplay-statistics.md#what-the-player-sees)),
  including the message for metrics a save has no coverage for. Wide layouts list the
  metrics by group beside the chart; compact layouts put a group and a metric
  drop-down and the team-filter entry point in one row, with scrollable filters over
  the plot. Axis labels stay outside the curves.

A session cannot commit after a second finger, focus loss, rotation, selection
change, or release over UI. While a zone stroke is held or a paint tap waits, the
HUD draws the exact cells it will paint, tinted like the zone (dark when erasing),
using `BrushCoverage`, the same helper the phone editor's preview uses; the
desktop hover cursor is hidden in the phone HUD because touch never moves it. Drag previews are lifted above the finger; edge panning
continues while held. Tap previews survive ordinary input suspension and require a
new confirmation gesture. Painting buffers an unfinished stroke; release applies
its existing brush operations, while interruption discards it. Completed strokes
are never undone by leaving the tool. Two fingers navigate instead of painting.

Zone painting is one-thumb too. The toolbar holds Forbidden, Guard, Clear (plus
Farm in a game with the farm-areas experiment) and Done (Done under the thumb), and a brush rail on the opposite edge holds the brush
sizes as detents (smallest lowest; touching one magnifies it beside the rail and
the thumb can scrub along it), Paint/Erase at its foot and Pan at its head. Pan
makes one finger move the map. A stroke held in the 24-point band along a map
edge pans (as placement does) and keeps painting under the still finger. For six
seconds after a stroke lands an Undo chip sits beside the rail foot: it sends
inverse zone orders for exactly the cells that stroke changed in the player's
displayed zones (in 32×32 blocks, after the stroke's own orders) and restores
those cells' displayed state; a stroke that changed nothing offers no Undo.
`BrushHUD` draws and hit-tests the rail for gameplay and the phone editor alike.

To add another interaction pattern, make it update the owned preview and call the
same commit operation. Keep gesture thresholds and sizing policies in the in-game
theme, add cancellation/order-equivalence cases to `GameGUITouchHarness`, and
capture the result through the production renderer. Do not duplicate construction
rules or adapt another presentation's composed screen.

[Mobile index](README.md) · [Documentation index](../README.md).
