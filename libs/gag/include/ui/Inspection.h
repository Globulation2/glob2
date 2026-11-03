// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include <SDL3/SDL.h>
#include <memory>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace GAGGUI::ui
{
// Values and actions belong to the UI, independently of any transport.
using ControlValue = std::variant<std::monostate, bool, int, std::string>;
enum class ActionKind
{
	Activate,
	Select,
	SetValue,
	Scroll
};
struct SemanticAction
{
	ActionKind kind = ActionKind::Activate;
	ControlValue value;
};
struct ControlSnapshot
{
	std::uint64_t revision = 0;
	std::string key, role, label;
	// Internal revision input; transport consumers use revision, not this value.
	std::string actionIdentity;
	ControlValue value;
	Rect bounds, visible;
	bool interactive = false, enabled = true, selected = false, focused = false, blocked = false;
	std::vector<ActionKind> actions;
	std::vector<ControlSnapshot> children;
	bool operator==(const ControlSnapshot &) const = default;
};
struct SurfaceSnapshot
{
	std::uint64_t instance = 0, revision = 0;
	Rect viewport, bounds;
	std::vector<ControlSnapshot> controls;
};
// Owned command carried through the ordinary additional-input queue. Events carry
// an opaque registry ID, never a borrowed pointer. Retiring the command retires it.
struct SemanticDispatch
{
	std::uint64_t surface = 0, revision = 0;
	std::string key, error;
	SemanticAction action;
	bool consumed = false;
	std::uint32_t id = 0;
	~SemanticDispatch();
	SDL_Event event();
};
std::shared_ptr<SemanticDispatch> semanticDispatch();
bool isSemanticEvent(const SDL_Event &event);
std::string inspectionJSON(const SurfaceSnapshot &snapshot);
const char *actionName(ActionKind kind);
// Browser diagnostic adapter: preserves the legacy flattened controls contract.
std::string diagnosticJSON(const SurfaceSnapshot &snapshot);
} // namespace GAGGUI::ui
