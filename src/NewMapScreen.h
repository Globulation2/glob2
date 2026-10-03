// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include <memory>
#include "GenerationRequest.h"
#include "GeneratorRegistry.h"
#include "ui/FrontendUI.h"
#include <ScreenStack.h>

class MapPreview;
class LandscapePickerScreen;

//! Chooses the size, landscape and parameters of a new editor map.
class NewMapScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "new_map"; }
	enum
	{
		OK = 1,
		CANCEL = 2,
		TOGGLE = 3
	};
	GenerationRequest descriptor;

	explicit NewMapScreen(const GeneratorRegistry &registry = GeneratorRegistry::builtins(), GAGGUI::ScreenStack *screens = nullptr);
	~NewMapScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32) override;
	// Validate and finish with OK, or show the validation error.
	void create();

  protected:
	void onEscape() override { endExecute(CANCEL); }

  private:
	friend class MapGeneratorDefaultsTest;
	friend struct MobileGallerySetup;
	GenerationHistory history;
	const GeneratorRegistry &registry;
	std::unique_ptr<MapPreview> preview;
	bool parameters = false, previewDirty = true;
	Uint32 previewDue = 0;
	std::string error;
	void chooseMethod(int method);
	void invalidatePreview();
	LandscapePickerScreen *chooseLandscape();
	GAGGUI::ScreenStack *screens;
};
