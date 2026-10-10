# Touch map and campaign editing

Editing controls, authoring responsibilities and interaction behavior on compact touch layouts.

## Map and campaign editor

### Choose the presentation

The editor has two presentations, chosen live by `chooseEditorPresentation`
(`src/map/editor/MapEditPresentation.h`) from room rather than input: the phone
tray (`PhoneEditor`) when the usable area cannot hold a 300-point dock beside a
480 x 480-point map, otherwise the dock beside the map. A 1024 x 768 touch
tablet or a touch laptop docks with touch-sized targets; a phone in either
orientation, a portrait tablet narrower than 780 points and a small pointer
window use the tray. The Compact interface preference always uses the tray;
Spacious keeps the dock down to a 320 x 400-point map. `GLOB2_MOBILE_UI=1`
forces the tray, `0` or `touch-spacious` the dock, and `touch-auto` applies the
automatic rule. Before a host has resolved any metrics (tools and tests that
never run a frame) the legacy rule applies: the tray only where shared forms
adapt. `MapEdit::syncPresentation` runs on construction and from every
`viewportResized`, which `ScreenStack` also sends when the resolved presentation
changes at the same size. Switching cancels unfinished strokes, drags and
gestures (never commits them) and keeps the brush, panel mode, team, levels and
brush size. `MapEditorScreen::usesResponsiveViewport` follows the editor's
presentation; `GraphicContext::setResponsiveViewport` grants the point-sized
viewport only where shared forms adapt, so a tray on a small pointer window
keeps the desktop canvas and scales through `logicalUnitsPerPoint`.

### Brush tray

`PhoneEditor` owns the tray presentation's map bounds, compact header, bottom
mode strip (Terrain, Resources, Buildings, Flags, Teams) and horizontally
scrolling card tray. Cards come from the brush catalogue (`MapEdit::brushCatalog`,
`PhoneEditorTray.cpp`) and carry its ids, so imports and experiment changes
rebuild the tray on the next frame (`catalogRevision`). Each card is the shared
`BrushSwatches` swatch (building, unit and zone artwork for objects) with its
name on up to two lines. Terrain lists every offered terrain group, group
variants inline and imported terrain, then areas, delete and the fertility
overlay toggle (`tool/fertility`); Resources lists every registry resource,
foundation and imported ones included. In both, header chips above the cards
name the groups, follow the scroll position and jump to a group on tap. A locked
card (an experiment the map does not carry) is dimmed with a padlock; a tap
enables the experiment for this map, says so above the tray and selects the
brush. The highlighted card is `MapEdit::currentBrushId()`. Status messages from
`MapEdit::showStatus` show above the tray. With a mouse, the wheel scrolls the
tray or inspector under the pointer and zooms the map about it.
Terrain and Resources share brush operations; Buildings and Flags expose team
and level beside the map. Done leaves the active tool and returns to object selection; Pan
switches one-finger navigation. One-finger zoom dragging and held paint taps
behave as in gameplay. In the editor, a double tap without travel still resets
to 1:1 zoom; taps that place buildings or units never arm zoom.
Brush tools use the same rail as zone painting (Paint/Erase only where it applies,
Pan, sizes). Zone, script-area and no-growth strokes offer Undo for six seconds,
restoring the covered tiles and displayed zone bits exactly; terrain, resource
and delete strokes remove units, buildings and resources, so they offer none.
The editor does not pan while a stroke is held at an edge: its strokes are
replayed in screen coordinates on release, so use Pan instead. A Map button in
the content's bottom corner away from the thumb opens the same map peek as in
gameplay (buttons below it in portrait, beside it in landscape) over the editor's
own minimap; it only moves the view. The brush rail chooses the mask. Pending
strokes draw their coverage before release without changing the map. Presentation measurements and drag thresholds are point-based
policies at the top of `PhoneEditor.cpp`.

A palette drag owns one pointer. Moving into the map lifts a placement preview;
release calls the existing named `place building` or `place unit` action once.
Horizontal movement within the tray browses choices. A tap selects the tool for
subsequent map taps. Brush strokes buffer points until release, then invoke the
existing editor brush actions. Leaving the map, losing focus, resizing or adding
a second finger cancels unfinished work. Two fingers navigate without painting.
`PhoneEditorView.cpp` renders pending gestures and contextual object inspectors.
Inspector rows bind a named property to its value and step controls; closing an
inspector restores the palette. `ValueScrollBox::setValue` invokes the existing
semantic action without desktop hit-test coordinates.
These gestures do not change map serialization or construction rules. New input
patterns should feed the same placement/brush operations and explicitly define
pointer ownership and cancellation.

Script, briefing and hints use the framework text editor with a separate IME
composition buffer; when the keyboard leaves little vertical space the action
row folds into the scrolling body without losing the draft. `ScriptEditorScreen`
composes tabs, editor and command bars; `TeamsEditor` composes rows and real
color swatches; `MapEditMenuScreen` is a bounded session menu. Save and load use
`LoadSaveDialog`, which renders the `FilePresentation` model including busy,
retry and export states and also serves child script-file dialogs. Area naming
uses `AskForTextInput` with native UTF-8 input. IME preedit is displayed
separately; only explicit confirmation commits the draft, and cancellation
retains the original name. `MapEdit` hosts these dialogs on desktop and touch
alike.

`NewMapScreen` presents Blank/Generated choices and shares the production
landscape browser with Custom Game. A chosen landscape retains its explicit seed
and generation parameters. Campaign views and map loading lay out preview and
details through the framework.
Campaign descriptions own touch cursor placement, swipe scrolling and provisional
IME text. Campaign lists distinguish completed taps from scrolls. Narrow script
entry navigation uses large Previous/entry/Next controls with stable script IDs;
multiple contacts cancel pending actions.
The editor menu uses the colony background and omits the main-menu logo. Native
lists opt into a shared minimum touch-row height so painting, hit testing and
scrolling use the same geometry; desktop retains its default row sizing.

Voice capture is disabled on mobile. Native phone-size checks do not establish
Android/iOS release readiness; real keyboard composition, safe areas, lifecycle,
and phone/tablet play sessions remain necessary.

[Mobile index](README.md) · [Documentation index](../README.md).
