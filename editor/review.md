# Fresh editor UX review

Reviewed the regenerated `artifacts/mobile-gallery/editor-second-review-baseline` screenshots, every one of the 24 screen IDs across all ten profiles. Reviewed full-size small portrait/landscape captures, tablet Automatic, comparison contact sheets for the other seven profiles, and intermediate/final building-drag and terrain-stroke frames. Read interaction code in PhoneEditor, ScriptEditorScreen, MapEditDialog, CampaignEditor, NewMapScreen, ChooseMapScreen and underlying text/list widgets. This is a code and visual review, not actual finger/device validation.

## Findings to implement

1. **P1 — Campaign description editing is not a working touch workspace (code defect).** `CampaignTextArea` in CampaignEditor.cpp only reflows legacy TextArea. Its inherited pointer handler activates but does not position the cursor, and there is no native keyboard start or touch scrolling. CampaignEditor/CampaignMapEntryEditor merely translate touch into mouse. Implement cursor placement, swipe scrolling, composition and keyboard lifecycle for descriptions; keep the active field visible with a compact keyboard layout. Minimum 40-height fields can overflow when the keyboard shrinks the safe area. Test actual insertion position and keyboard draft preservation, plus short landscape geometry.
2. **P1 — Load/save inside the editor still have the old generic-form presentation (visible design defect).** `editor-load`, `editor-save`: white full-width filename rows float directly over the world and dominate the whole height; filename editing is far below the list. This is markedly worse than frontend `load-map` and the redesigned purple dialogs. Use an explicit bounded purple file view, pinned filename/actions, scrollable list, visible errors and empty states. Apply equivalent treatment to script-file and area-name child dialogs. Preserve the existing persistence implementation.
3. **P1 — No obvious switch from a brush/placement tool to inspecting existing objects (workflow defect).** PhoneEditor Move/Edit only toggles panning. While a tool is active a map tap paints/places; while Move is active it cannot inspect. The hidden workaround is changing palette category or opening the menu to invoke unselect. Provide an explicit Select mode/exit-tool action and maintain understandable mode status.
4. **P1 — Selected-object properties still use a transplanted horizontal sequence (code/design defect, missing fixture).** PhoneEditor::prepare gathers all enabled BuildingEditor/UnitEditor legacy widgets separately into 56-high cells, so a label, value control and artwork can scroll away independently. Group identity and label/value pairs into a contextual inspector with a clear close action restoring the palette. Add selected-building and selected-unit fixtures before evaluating the result.
5. **P1 — Drag/paint feedback is too weak to judge the edit before release (interaction judgment backed by frames).** `gesture-editor-build-19` shows thin red fragments over busy trees rather than a clear lifted object/footprint/status; `gesture-editor-paint-19` shows a thin red path, then the entire water strip appears on release. Keep buffered, cancellable strokes, but render pending brush cells/footprint using a contrasting translucent preview and explicit validity for objects. Replace unexplained Brush 1..N with visible geometry and active material/operation, and expose brief help for unlabeled icons. Do not mutate the map for preview.
6. **P2 — Objective/hint entries are undersized and team scrolling is undiscoverable (geometry/design defects).** Eight script entry buttons fit into ~280 points at small portrait, yielding ~31-point targets. Use minimum 44-point targets with scrolling or previous/current/next selection while retaining stable script IDs. `editor-teams` small landscape displays only three team rows with no indication that more exist; show a scrollbar/visible range. `editor-palette-teams` is currently anonymous +/− under the Terrain tab; route it to an accurately named team-management composition rather than presenting the wrong context.
7. **P2 — New-map overview still violates content-fit surface sizing (visible design defect).** `new-map` and `new-map-generated` tablet/desktop have a very large empty lower half because compose always uses safe-area bottom. Bound the overview to its preview, controls and footer; allow parameters to use an independently scrollable region. Preserve compact landscape side-by-side treatment.

## Coverage checklist

| Screen IDs | Assessment |
|---|---|
| editor-menu | Logo correctly absent; surface compact; live colony background present. No new blocking issue. |
| new-map, new-map-generated | Preview-led flow is improved; excess tablet/desktop surface height remains (#7). |
| new-map-parameters | Scrollable secondary parameters are appropriate; verify dropdown/keyboard boundaries after sizing changes. |
| editor-landscapes | Real visual tiles are appropriate; constrained landscape only exposes one tile and partial caption but scroll affordance is present. No separate blocker. |
| load-map | Preview, metadata, list and pinned actions are clear. Verify swipe list navigation (native List otherwise relies on a narrow scrollbar). |
| campaign-editor, campaign-map-entry | Better hierarchy and responsive columns; actual text and list touch mechanics need attention (#1). Capture Maps and Unlocking tabs. |
| editor-map, editor-tools, editor-tools-bottom | World stays visible and artwork palette is compact; tool exit, inspector and preview issues (#3–5). |
| editor-palette-terrain, editor-palette-resources, editor-palette-flag | Appropriate category palettes, but brush/selection help and preview insufficient (#5). Flags also contain units, which should be communicated in selected-item help/category wording. |
| editor-palette-teams | Misleading Terrain context and anonymous controls (#6). |
| editor-pause | Small landscape clipping previously reported is resolved; readable bounded menu. |
| editor-teams | Parallel swatches and rows are improved; reveal scrolling (#6). |
| editor-script, editor-script-briefing | Dominant text canvas is a major improvement. Verify touch pointer ownership; current handler has no finger identity and accepts any release for held actions. |
| editor-script-hints, editor-script-objectives | Functional editing canvas, undersized entry targets (#6). |
| editor-script-keyboard | Compact workspace remains visible. Fixture is simulated inset, not actual keyboard evidence. |
| editor-save, editor-load | Generic white adapter remains (#2). |

## Missing distinct workflow evidence

Add captures for selected building and selected unit, campaign Maps and Unlocking tabs, campaign description keyboard, script load/save child dialogs, and script-area naming. Include at least one invalid object drop and a cancelled/interrupted stroke in gesture verification. Current gallery frames alone do not demonstrate cancellation or draft persistence.

## Re-review criteria

Check revised screenshots at small portrait, small landscape and tablet, with explicit input tests for the defects above. Verify updated gesture frames expose pending footprint/validity before commitment, and that no cancelled gesture changes the map. Desktop layouts outside intentionally shared frontend changes must remain usable. Actual Android/iOS keyboards, touch comfort, long translation and enlarged-interface stress coverage remain separate evidence requirements.
