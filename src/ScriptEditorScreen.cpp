// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "ScriptEditorScreen.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapHeader.h"
#include "MapScript.h"
#include "script/ScriptRuntime.h"
#include "Utilities.h"
#include "gui/LoadSaveDialog.h"
#include <FileManager.h>
#include <Stream.h>
#include <StreamBackend.h>
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;
using namespace GAGCore;

namespace
{
bool loadText(const std::string &filename, std::string &text)
{
	std::unique_ptr<StreamBackend> stream(
		Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (!stream || stream->isEndOfStream())
		return false;
	stream->seekFromEnd(0);
	const size_t len = stream->getPosition();
	stream->seekFromStart(0);
	std::string buffer(len, '\0');
	if (!stream->readExact(buffer.data(), len))
		return false;
	// Keep every source byte so embedded NULs reach the language validator
	// instead of silently turning the file into a different, valid prefix.
	text = std::move(buffer);
	return true;
}

bool saveText(const std::string &filename, const std::string &text)
{
	std::unique_ptr<StreamBackend> stream(
		Toolkit::getFileManager()->openOutputStreamBackend(filename));
	if (!stream || stream->isEndOfStream())
		return false;
	stream->write(text.c_str(), text.length());
	return true;
}
} // namespace

ScriptEditorScreen::ScriptEditorScreen(Game *game)
	: sgslMapScript(&game->sgslScript), mapScript(&game->mapscript), game(game)
{
	scriptDrafts[0] = sgslMapScript->sourceCode;
	const bool javascript = mapScript->getMapScriptMode() == MapScript::JavaScript;
	scriptDrafts[javascript ? 2 : 1] = mapScript->getMapScript();
	if (!javascript)
		scriptDrafts[2] = "function step(ctx) {\n  return [];\n}\n";
	selectedLanguage =
		javascript ? Language::JavaScript
		: (!mapScript->getMapScript().empty() ||
		   (globalContainer->settings.optionFlags & GlobalContainer::OPTION_MAP_EDIT_USE_USL))
			? Language::USL
			: Language::SGSL;
	script = scriptDrafts[static_cast<int>(selectedLanguage)];
	for (int i = 0; i < game->objectives.getNumberOfObjectives(); ++i)
	{
		const int number = game->objectives.getScriptNumber(i);
		if (game->objectives.getObjectiveType(i) == GameObjectives::Primary)
		{
			if (number >= 1 && number <= 8)
				primary[number - 1] = game->objectives.getGameObjectiveText(i);
		}
		else if (number >= 9 && number <= 16)
			secondary[number - 9] = game->objectives.getGameObjectiveText(i);
	}
	for (int i = 0; i < game->gameHints.getNumberOfHints(); ++i)
	{
		const int number = game->gameHints.getScriptNumber(i);
		if (number >= 1 && number <= 8)
			hints[number - 1] = game->gameHints.getGameHintText(i);
	}
	briefing = game->missionBriefing;
}

ScriptEditorScreen::~ScriptEditorScreen() = default;

void ScriptEditorScreen::selectLanguage(Language language)
{
	if (files || language == selectedLanguage)
		return;
	scriptDrafts[static_cast<int>(selectedLanguage)] = script;
	selectedLanguage = language;
	script = scriptDrafts[static_cast<int>(selectedLanguage)];
	compiled = false;
	compilation.clear();
	host().state("script") = {};
	invalidate();
}

const char *ScriptEditorScreen::scriptExtension() const
{
	switch (selectedLanguage)
	{
	case Language::JavaScript:
		return "js";
	case Language::USL:
		return "usl";
	case Language::SGSL:
		return "sgsl";
	}
	return "sgsl";
}

void ScriptEditorScreen::setScriptText(const std::string &text)
{
	script = text;
	compiled = false;
	compilation.clear();
	invalidate();
}

void ScriptEditorScreen::showTab(int tab)
{
	if (tab == TAB_PRIMARY || tab == TAB_SECONDARY)
	{
		secondaryPage = tab == TAB_SECONDARY;
		page = TAB_OBJECTIVES;
	}
	else
		page = tab;
	invalidate();
}

bool ScriptEditorScreen::testCompile()
{
	try
	{
		if (selectedLanguage == Language::JavaScript)
		{
			Script::makeRuntime()->validate(script);
			compiled = true;
			compilation = "Compilation success";
		}
		else if (selectedLanguage == Language::USL)
		{
			// Compile a draft without changing the map's source, mode or saved globals.
			MapScript candidate(game, game->clientSink);
			candidate.setMapScript(script);
			if (candidate.compileCode())
			{
				compiled = true;
				compilation = "Compilation success";
			}
			else
			{
				MapScriptError error = candidate.getError();
				compiled = false;
				compilation = FormattableString("Error at %0:%1: %2")
								  .arg(error.getLine())
								  .arg(error.getColumn())
								  .arg(error.getMessage());
			}
		}
		else
		{
			MapScriptSGSL candidate;
			const ErrorReport er = candidate.compileScript(game, script.c_str());
			if (er.type == ErrorReport::ET_OK)
			{
				compiled = true;
				compilation = "Compilation success";
			}
			else
			{
				compiled = false;
				compilation = FormattableString("Compilation failure : %0:%1:(%2):%3")
								  .arg(er.line + 1)
								  .arg(er.col)
								  .arg(er.pos)
								  .arg(er.getErrorString());
				host().state("script").cursor =
					std::min<std::size_t>(std::size_t(std::max(0, int(er.pos))), script.size());
			}
		}
	}
	catch (const std::exception& error)
	{
		compiled = false;
		compilation = error.what();
	}

	invalidate();
	return compiled;
}

void ScriptEditorScreen::confirm()
{
	//Load the script
	if (!testCompile())
		return;
	try
	{
		if (selectedLanguage != Language::SGSL)
		{
			MapScriptError error;
			if (!mapScript->replaceSource(selectedLanguage == Language::JavaScript
											 ? MapScript::JavaScript : MapScript::USL, script, error))
			{
				compiled = false;
				compilation = error.getMessage();
				invalidate();
				return;
			}
		}
		else
		{
			MapScriptSGSL candidate;
			candidate.sourceCode = script;
			const ErrorReport error = candidate.compileScript(game);
			if (error.type != ErrorReport::ET_OK)
			{
				compiled = false;
				compilation = error.getErrorString();
				invalidate();
				return;
			}
			// Prepare the inactive empty USL backend before replacing either live
			// script. An unavailable USL runtime must not destroy working JS globals.
			// Released USL+SGSL maps retain their existing USL backend when edited.
			std::unique_ptr<MapScript> emptyUSL;
			if (mapScript->getMapScriptMode() == MapScript::JavaScript)
			{
				MapScriptError error;
				emptyUSL = mapScript->prepareSource(MapScript::USL, "", error);
				if (!emptyUSL)
				{
					compiled = false;
					compilation = error.getMessage();
					invalidate();
					return;
				}
			}
			// Every allocation and compilation has finished. Both swaps are noexcept.
			sgslMapScript->swap(candidate);
			if (emptyUSL)
				mapScript->commitPrepared(*emptyUSL);
		}
	}
	catch (const std::exception& error)
	{
		compiled = false;
		compilation = error.what();
		invalidate();
		return;
	}

	//Load the objectives
	int n = 0;
	for (int i = 0; i < 8; ++i)
	{
		if (primary[i] != "")
		{
			if (n >= game->objectives.getNumberOfObjectives())
				game->objectives.addNewObjective(primary[i], false, false, false,
												 GameObjectives::Primary, i + 1);
			else
			{
				game->objectives.setGameObjectiveText(n, primary[i]);
				game->objectives.setObjectiveType(n, GameObjectives::Primary);
				game->objectives.setScriptNumber(n, i + 1);
			}
			n += 1;
		}
	}
	for (int i = 0; i < 8; ++i)
	{
		if (secondary[i] != "")
		{
			if (n >= game->objectives.getNumberOfObjectives())
				game->objectives.addNewObjective(secondary[i], false, false, false,
												 GameObjectives::Secondary, i + 9);
			else
			{
				game->objectives.setGameObjectiveText(n, secondary[i]);
				game->objectives.setObjectiveType(n, GameObjectives::Secondary);
				game->objectives.setScriptNumber(n, i + 9);
			}
			n += 1;
		}
	}
	while (game->objectives.getNumberOfObjectives() > n)
		game->objectives.removeObjective(game->objectives.getNumberOfObjectives() - 1);

	//Load the briefing
	game->missionBriefing = briefing;

	//Load the hints
	n = 0;
	for (int i = 0; i < 8; ++i)
	{
		if (hints[i] != "")
		{
			if (n >= game->gameHints.getNumberOfHints())
				game->gameHints.addNewHint(hints[i], false, i + 1);
			else
			{
				game->gameHints.setGameHintText(n, hints[i]);
				game->gameHints.setScriptNumber(n, i + 1);
			}
			n += 1;
		}
	}
	while (game->gameHints.getNumberOfHints() > n)
		game->gameHints.removeHint(game->gameHints.getNumberOfHints() - 1);
	finish(OK);
}

bool ScriptEditorScreen::onEvent(const SDL_Event &event)
{
	// No unicode representation for F9 key, so putting it here.
	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_F9)
	{
		testCompile();
		return true;
	}
	return false;
}

//! LoadSaveDialog name-extractor callback for the script load/save dialog:
//! turn a full virtual path like "scripts/My_Map.js" into "My Map". Directory
//! prefix and extension are only removed when actually present, so a stray
//! file in scripts/ degrades to showing its raw name instead of throwing
//! (erase(npos) used to crash the dialog).
std::string filenameToName(const std::string &fullfilename)
{
	std::string filename = Utilities::stripPrefix(fullfilename, "scripts/");
	for (const char *suffix : {".js", ".usl", ".sgsl"})
	{
		const auto stripped = Utilities::stripSuffix(filename, suffix);
		if (stripped != filename)
		{
			filename = stripped;
			break;
		}
	}
	std::replace(filename.begin(), filename.end(), '_', ' ');
	return filename;
}

void ScriptEditorScreen::loadSave(bool isLoad)
{
	loadSave(isLoad, "scripts", scriptExtension());
}

void ScriptEditorScreen::loadSave(bool isLoad, const char *dir, const char *ext)
{
	if (files)
		return;
	loadingScript = isLoad;
	const std::string title =
		Toolkit::getStringTable()->getString(isLoad ? "[load script]" : "[save script]");
	files = std::make_unique<LoadSaveDialog>(dir, ext, isLoad, title,
											 game->mapHeader.getMapName().c_str(), filenameToName,
											 glob2NameToFilename);
	if (!globalContainer->runNoX)
		files->attach(*globalContainer->gfx);
}

void ScriptEditorScreen::finishFileDialog()
{
	// The editor stays alive and suspended while its owned child handles input.
	auto dialog = std::move(files);
	if (!dialog || !dialog->finished())
		return;
	const bool isLoad = loadingScript;
	if (dialog->result() == LoadSaveDialog::OK)
	{
		if (isLoad)
		{
			if (!loadText(dialog->getFileName(), script))
			{
				compiled = false;
				compilation =
					FormattableString("Loading script from %0 failed").arg(dialog->getName());
			}
			else
				testCompile();
		}
		else if (!saveText(dialog->getFileName(), script))
		{
			compiled = false;
			compilation = FormattableString("Saving script to %0 failed").arg(dialog->getName());
		}
	}
	invalidate();
}

Element ScriptEditorScreen::scriptTab(const Presentation &p)
{
	fe::TextEditorOptions options;
	options.lines = 14;
	auto editorControl = fe::textEditor(
		"script", script, [this](const std::string &value) { setScriptText(value); }, options);
	auto editor = classic() ? editorControl : fe::expanded(editorControl);
	fe::TextOptions resultStyle;
	resultStyle.role = fe::FontRole::Support;
	if (!compilation.empty())
		resultStyle.color = compiled ? theme().palette.success : theme().palette.danger;
	auto position =
		fe::canvas("script/cursor", {p.pt(120), p.pt(16)},
				   [this](fe::Canvas &c, fe::Rect r, const fe::Frame &frame)
				   {
					   std::size_t cursor = 0;
					   if (const auto *state = host().states().find("script"))
						   cursor = std::min(state->cursor, script.size());
					   int line = 1, column = 1;
					   for (std::size_t i = 0; i < cursor; ++i)
						   if (script[i] == '\n')
						   {
							   ++line;
							   column = 1;
						   }
						   else
							   ++column;
					   c.text({r.x, r.y}, fe::FontRole::Caption,
							  FormattableString("Line: %0 Col: %1").arg(line).arg(column),
							  frame.layout.theme.palette.muted);
				   });
	auto status = fe::row({fe::expanded(fe::label(compilation, resultStyle)), position},
						  {p.pt(8), fe::CrossAlign::Center});
	auto language = fe::field(fe::tr("[language-tr]"),
							  fe::choice("script/language", {"SGSL", "USL", "JavaScript"},
										 static_cast<int>(selectedLanguage), [this](int index)
										 { selectLanguage(static_cast<Language>(index)); }));
	std::vector<fe::MenuAction> tools;
	tools.push_back({"compile", fe::tr("[Compile]"), [this] { testCompile(); }});
	tools.push_back({"load", fe::tr("[load]"), [this] { loadSave(true); }});
	tools.push_back({"save", fe::tr("[Save]"), [this] { loadSave(false); }});
	return fe::column(
		{language, editor, status, fe::actions(std::move(tools), p, fe::ActionStyle::Compact)},
		{p.pt(6)});
}

Element ScriptEditorScreen::entriesTab(const Presentation &p, std::string *entries,
									   const std::string &prefix, int firstNumber)
{
	std::vector<Element> rows;
	for (int i = 0; i < 8; ++i)
	{
		fe::TextFieldOptions options;
		options.maxLength = 0;
		rows.push_back(
			fe::field(std::to_string(firstNumber + i),
					  fe::textField(
						  prefix + "/" + std::to_string(i), entries[i],
						  [entries, i](const std::string &value) { entries[i] = value; }, options),
					  {"", 520, false}));
	}
	return fe::scroll(prefix + "/scroll", fe::column(std::move(rows), {p.pt(6)}));
}

Element ScriptEditorScreen::build(const Presentation &p)
{
	const std::vector<std::string> tabs = {fe::tr("[map script]"), fe::tr("[objectives]"),
										   fe::tr("[briefing]"), fe::tr("[hints]")};
	const int ids[] = {TAB_SCRIPT, TAB_OBJECTIVES, TAB_BRIEFING, TAB_HINTS};
	int selected = 0;
	for (int i = 0; i < 4; ++i)
		if (ids[i] == page)
			selected = i;
	auto header =
		fe::segments("tab", tabs, selected, [this, ids](int index) { showTab(ids[index]); });
	Element body;
	if (page == TAB_SCRIPT)
		body = scriptTab(p);
	else if (page == TAB_OBJECTIVES)
	{
		auto kinds = fe::segments(
			"objectives/kind", {fe::tr("[Primary Objectives]"), fe::tr("[Secondary Objectives]")},
			secondaryPage ? 1 : 0,
			[this](int index) { showTab(index ? TAB_SECONDARY : TAB_PRIMARY); });
		body =
			fe::column({kinds, fe::expanded(secondaryPage ? entriesTab(p, secondary, "secondary", 9)
														  : entriesTab(p, primary, "primary", 1))},
					   {p.pt(6)});
	}
	else if (page == TAB_BRIEFING)
	{
		fe::TextEditorOptions options;
		options.lines = 12;
		body = fe::expanded(fe::textEditor(
			"briefing", briefing, [this](const std::string &value) { briefing = value; }, options));
	}
	else
		body = entriesTab(p, hints, "hint", 1);
	std::vector<fe::MenuAction> actions;
	actions.push_back({"ok", fe::tr("[ok]"), [this] { confirm(); }, true});
	actions.push_back(
		{"cancel", fe::tr("[Cancel]"), [this] { finish(CANCEL); }, false, SDLK_ESCAPE});
	if (classic())
		return fe::column(
			{header, body, fe::actions(std::move(actions), p, fe::ActionStyle::Compact)},
			{p.pt(8)});
	return fe::column(
		{header, fe::expanded(fe::footer(
					 body, fe::actions(std::move(actions), p, fe::ActionStyle::Compact)))},
		{p.pt(8)});
}
