// SPDX-License-Identifier: GPL-3.0-or-later
// Live choice between the phone tray and the dock (MapEditPresentation.h), and
// MapEdit's switching between them.
#include "MapEditPresentation.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "PhoneEditor.h"
#include <string_view>

using GAGCore::PresentationPreference;

EditorPresentationChoice chooseEditorPresentation(const GAGCore::ResolvedPresentation &resolved,
												  std::optional<PresentationPreference> forced,
												  PresentationPreference preference, bool touch)
{
	using namespace EditorPresentationPolicy;
	EditorPresentationChoice choice;
	choice.touchTargets = touch || resolved.touch;
	const double width = resolved.usable.w, height = resolved.usable.h;
	bool dock;
	if (forced && *forced != PresentationPreference::Automatic)
		dock = *forced == PresentationPreference::Spacious;
	else if (const auto wanted = forced.value_or(preference); wanted == PresentationPreference::Compact)
		dock = false;
	else if (wanted == PresentationPreference::Spacious)
		dock = width >= dockPoints + spaciousMapWidth && height >= spaciousMapHeight;
	else
		dock = width >= dockPoints + mapPoints && height >= mapPoints;
	choice.presentation = dock ? EditorPresentation::Dock : EditorPresentation::Phone;
	return choice;
}

EditorPresentationChoice currentEditorPresentation()
{
	const char *mobile = SDL_getenv_unsafe("GLOB2_MOBILE_UI");
	const bool touchOverride = mobile && std::string_view(mobile).starts_with("touch-");
	const auto &resolved = GAGCore::presentationState;
	if (resolved.usable.w <= 0 || resolved.usable.h <= 0)
	{
		EditorPresentationChoice legacy;
		legacy.presentation =
			GAGCore::phonePresentationRequested() ? EditorPresentation::Phone : EditorPresentation::Dock;
		legacy.touchTargets = resolved.touch || touchOverride;
		return legacy;
	}
	return chooseEditorPresentation(resolved, GAGCore::presentationOverride(), GAGCore::presentationPreference,
									touchOverride);
}

const char *editorPresentationName(EditorPresentation presentation)
{
	return presentation == EditorPresentation::Phone ? "phone" : "dock";
}

// --- MapEdit ---

EditorPresentation MapEdit::wantedPresentation() const
{
	auto choice = currentEditorPresentation();
	// The tray draws through the portable renderer's UI transforms.
	if (!globalContainer->gfx || !globalContainer->gfx->hasPortableRenderer())
		choice.presentation = EditorPresentation::Dock;
	return choice.presentation;
}

EditorPresentation MapEdit::presentation() const
{
	return phone ? EditorPresentation::Phone : EditorPresentation::Dock;
}

bool MapEdit::presentationTouchTargets() const
{
	return currentEditorPresentation().touchTargets;
}

bool MapEdit::wantsResponsiveViewport() const
{
	// The graphic context only grants a point-sized viewport where shared forms
	// adapt (GraphicContext::setResponsiveViewport); the tray asks for one too
	// so the phone layout never sits on a fixed desktop canvas when it may not.
	return phone || GAGCore::phonePresentationRequested();
}

bool MapEdit::syncPresentation()
{
	const auto wanted = wantedPresentation();
	if (wanted == presentation())
		return false;
	// Unfinished work belongs to the presentation that started it: strokes,
	// drags, rail scrubs and gestures are cancelled, never committed. The brush,
	// terrain type, panel mode, team and levels are editor state and stay.
	suspendInput();
	resetPlacementTracking();
	strokeCoveredCells = strokePlacedResources = 0;
	if (wanted == EditorPresentation::Phone)
	{
		leaveDockPresentation();
		phone = std::make_unique<PhoneEditor>(*this);
	}
	else
	{
		phone.reset();
		enterDockPresentation();
	}
	return true;
}

// Seam for the EditorDock (WS-C, EditorDock.h). The desktop sidebar widgets
// exist for the editor's whole life and lay themselves out against the window
// width, so until the dock lands entering or leaving it needs nothing. With the
// dock, enterDockPresentation() calls createDock() and leaveDockPresentation()
// destroyDock(); hasDock() then mirrors !phone, and the constructor's
// "if (!phone) createDock()" stays as WS-C wrote it.
void MapEdit::enterDockPresentation() {}
void MapEdit::leaveDockPresentation() {}
