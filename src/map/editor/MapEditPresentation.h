// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which editor presentation fits the window: the phone tray (PhoneEditor: map,
// bottom mode strip and scrolling card tray) or the dock (brush browser beside
// the map, the desktop sidebar until the EditorDock lands). The choice is live:
// MapEdit::syncPresentation() re-reads it whenever the viewport or the resolved
// presentation changes, so rotating a phone, resizing a window or docking a
// tablet swaps presentations without losing the selected brush.
//
// The rule is about room, not input. A touch tablet or touch laptop gets the
// dock with touch-sized targets; a phone, a phone in landscape or a small
// desktop window gets the tray. GLOB2_MOBILE_UI and the player's interface
// preference (Settings, InterfacePresentation.h) take part as described below.

#include <InterfacePresentation.h>
#include <cstdint>
#include <optional>

enum class EditorPresentation : std::uint8_t
{
	Phone,
	Dock
};

struct EditorPresentationChoice
{
	EditorPresentation presentation = EditorPresentation::Dock;
	// The dock sizes its cards and controls for fingers (48 pt targets).
	bool touchTargets = false;
	bool operator==(const EditorPresentationChoice &) const = default;
};

namespace EditorPresentationPolicy
{
// Points the dock takes beside the map (EditorDock: clamp(300 pt, 240 px, 40%)).
constexpr double dockPoints = 300;
// The map keeps at least this much beside the dock, as gameplay keeps beside its
// panel (resolvePresentation's 480 x 480 fit).
constexpr double mapPoints = 480;
// With the Spacious preference the dock is kept down to a smaller map.
constexpr double spaciousMapWidth = 320, spaciousMapHeight = 400;
} // namespace EditorPresentationPolicy

// Pure decision over host points.
//  - forced Compact (GLOB2_MOBILE_UI=1): phone tray.
//  - forced Spacious (GLOB2_MOBILE_UI=0 or touch-spacious): dock.
//  - otherwise the player's preference: Compact: phone tray; Automatic: dock iff
//    the usable area holds the dock plus a 480 x 480 map; Spacious: dock iff it
//    holds the dock plus a 320 x 400 map.
// `touch` is the host's input (or a touch-* override); it only sizes targets.
EditorPresentationChoice chooseEditorPresentation(const GAGCore::ResolvedPresentation &resolved,
												  std::optional<GAGCore::PresentationPreference> forced,
												  GAGCore::PresentationPreference preference =
													  GAGCore::PresentationPreference::Automatic,
												  bool touch = false);

// The live choice from GAGCore's resolved presentation, GLOB2_MOBILE_UI and the
// settings preference. Before any host metrics were resolved (headless tools and
// tests that never ran a frame) it keeps the legacy rule: the tray iff shared
// forms are adapted (GAGCore::phonePresentationRequested()).
EditorPresentationChoice currentEditorPresentation();

const char *editorPresentationName(EditorPresentation presentation);
