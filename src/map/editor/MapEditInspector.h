// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The unit and building inspector shared by every editor presentation: the
// desktop/tablet dock (EditorDock) and the phone editor (PhoneEditor) show the
// same rows for the selected object. Each row is bound to the ValueScrollBox
// that MapEdit already keeps in step with the object (the update actions write
// the value back through it), so presentations only read currentValue() and
// maximumValue() and edit through setValue().

#include "ui/FrontendUI.h"
#include "render/scene/Scene.h"
#include <functional>
#include <string>
#include <vector>

class MapEdit;
class ValueScrollBox;

struct InspectorRow
{
	// Stable within one inspector: "hp", "assigned", "worker-ratio",
	// "explorer-ratio", "warrior-ratio", "bullets", "minimum-level",
	// "worker-level", "ground-attack", "material/<index>", "range" for buildings;
	// "hp", "walk", "swim", "build", "attack-speed", "attack-strength",
	// "magic-ground" for units.
	std::string key;
	// Translated caption.
	std::string caption;
	// Value binding; never null.
	ValueScrollBox *value = nullptr;
};

struct InspectorModel
{
	enum class Kind
	{
		None,
		Unit,
		Building
	};
	Kind kind = Kind::None;
	// Changes when another object is selected: the building GID, or 65536 plus
	// the unit GID.
	int identity = -1;
	// Translated object name and "Team n / Level m".
	std::string title, detail;
	SimulationSnapshot::Handle world;
	const SnapshotBuilding *building = nullptr;
	const SnapshotUnit *unit = nullptr;
	// Only the rows that apply to the selected object, in display order.
	std::vector<InspectorRow> rows;
};

// The inspector for MapEdit's current selection; Kind::None when the editor is
// not editing a unit or building (or the object disappeared).
InspectorModel buildInspectorModel(MapEdit &editor);

// Framework rows for the model: per row a caption and a stepper bound to the
// row's value, keyed "<prefix>/<row key>". Empty column for Kind::None.
// `changed` runs after a value was edited (the caller rebuilds its view).
Glob2UI::Element inspectorRows(const InspectorModel &model, const Glob2UI::Presentation &p,
							   const std::string &prefix = "dock/inspect", std::function<void()> changed = {});

// Paints the selected object's sprite (team coloured) centred in `bounds`.
void paintInspectorPicture(GAGGUI::ui::Canvas &canvas, GAGGUI::ui::Rect bounds, const InspectorModel &model);
