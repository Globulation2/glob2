// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once
#include "ui/FrontendUI.h"
#include <array>
#include <memory>
#include <string>

class Game;
class LoadSaveDialog;
class MapScript;
class MapScriptSGSL;

// The scenario editor: the map script with compile/load/save, the mission
// objectives, briefing and hints. OK compiles the script and commits every
// tab into the game; the script's load/save dialog is hosted by the owner.
class ScriptEditorScreen : public Glob2UI::InGameDialog
{
  public:
	enum
	{
		OK = 0,
		CANCEL = 1,
		COMPILE = 2,
		LOAD,
		SAVE,
		TAB_SCRIPT = 10,
		TAB_OBJECTIVES = 11,
		TAB_BRIEFING = 12,
		TAB_HINTS = 13,
		TAB_PRIMARY = 14,
		TAB_SECONDARY = 15,
	};

	explicit ScriptEditorScreen(Game *game);
	~ScriptEditorScreen() override;
	enum class Language
	{
		SGSL,
		USL,
		JavaScript
	};
	Language language() const { return selectedLanguage; }
	void selectLanguage(Language language);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	// Semantic entry points shared with harnesses.
	void showTab(int tab);
	int tab() const { return page; }
	bool compile() { return testCompile(); }
	void confirm();
	void loadSave(bool isLoad, const char *dir, const char *ext);
	void loadSave(bool isLoad);
	// The open script file dialog, if any; the owner routes input to it and
	// calls finishFileDialog() once it completes.
	LoadSaveDialog *fileDialog() const { return files.get(); }
	void finishFileDialog();
	const std::string &scriptText() const { return script; }
	void setScriptText(const std::string &text);
	const std::string &compilationText() const { return compilation; }
	const std::string &hint(int index) const { return hints[index]; }
	const std::string &briefingText() const { return briefing; }

  protected:
	void onEscape() override { finish(CANCEL); }
	bool fillHeight() const override { return !classic(); }
	double maxWidth() const override { return classic() ? 580 : 960; }
	bool onEvent(const SDL_Event &event) override;

  private:
	bool testCompile();
	const char *scriptExtension() const;
	Language selectedLanguage;
	std::array<std::string, 3> scriptDrafts;
	std::string script, compilation;
	bool compiled = false;
	std::string primary[8], secondary[8], hints[8], briefing;
	int page = TAB_SCRIPT;
	bool secondaryPage = false;
	MapScriptSGSL *sgslMapScript;
	MapScript *mapScript;
	Game *game;
	std::unique_ptr<LoadSaveDialog> files;
	bool loadingScript = false;
	Glob2UI::Element scriptTab(const Glob2UI::Presentation &p);
	Glob2UI::Element entriesTab(const Glob2UI::Presentation &p, std::string *entries,
								const std::string &prefix, int firstNumber);
};

//! Turn a full virtual script path into its display name.
std::string filenameToName(const std::string &fullfilename);
