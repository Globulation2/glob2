// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Glob2Screen.h"
#include "GenerationRequest.h"
#include "GeneratorRegistry.h"

namespace GAGGUI
{
class Number;
class Text;
class Ratio;
class List;
} // namespace GAGGUI

//! This screen allows to choose the size of the map and the default background
class NewMapScreen : public Glob2Screen
{
  public:
	enum
	{
		OK = 1,
		CANCEL = 2
	};

  public:
	GenerationRequest descriptor;

  private:
	friend class MapGeneratorDefaultsTest;
	struct ControlWidget
	{
		GenerationRequest::Control definition;
		int method; // -1 for shared controls
		Number *number;
		Text *label;
	};
	std::vector<ControlWidget> controlWidgets;
	GenerationHistory history;
	const GeneratorRegistry &registry;
	List *methods, *terrains;
	void updateControls();

  public:
	//! Constructor
	explicit NewMapScreen(const GeneratorRegistry &registry = GeneratorRegistry::builtins());
	//! Destructor
	virtual ~NewMapScreen() {};
	//! Action handler
	void onAction(Widget *source, Action action, int par1, int par2);
};
