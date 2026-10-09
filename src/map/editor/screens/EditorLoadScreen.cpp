// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditorLoadScreen.h"
#include "MapEdit.h"
#include "GlobalContainer.h"
#include "Utilities.h"
#include <iostream>
#include <PerformanceTelemetry.h>
EditorLoadScreen::EditorLoadScreen(const std::string &filename, GAGCore::CooperativeSlice slice)
	: EditorLoadScreen([filename](MapEdit &editor) { return editor.loadTask(filename); },
					   "[Loading headers]", std::move(slice))
{
}
EditorLoadScreen::EditorLoadScreen(Initializer initialize, const char *caption,
								   GAGCore::CooperativeSlice slice)
	: slice(std::move(slice))
{
	status = Glob2UI::tr(caption);
	task.emplace(prepare(std::move(initialize)));
}
GAGCore::CooperativeTask EditorLoadScreen::prepare(Initializer initialize)
{
	// The editor draws with the game sprites, which the browser may still be
	// downloading; natively they are loaded and this does not suspend.
	co_await globalContainer->gameGraphicsTask();
	editor = std::make_unique<MapEdit>();
	co_return co_await initialize(*editor);
}
Glob2UI::Element EditorLoadScreen::build(const Glob2UI::Presentation &p)
{
	using namespace Glob2UI;
	return page("", center(paragraph(status, {FontRole::Body, false, TextAlign::Center})),
				actions({{"cancel", tr("[Cancel]"), [this] { endExecute(0); }, false, SDLK_ESCAPE}}, p),
				p, 480);
}
EditorLoadScreen::~EditorLoadScreen()
{
	task.reset();
	editor.reset();
}
std::unique_ptr<MapEdit> EditorLoadScreen::takeEditor()
{
	if (!task->result())
		throw std::logic_error("Cannot accept failed editor load");
	accepted = true;
	task.reset();
	return std::move(editor);
}
void EditorLoadScreen::onTimer(Uint32)
{
	PERF_SCOPE_TIME(Load);
	try
	{
		if (slice.advance(*task))
		{
			endExecute(task->result() ? 1 : 2);
			return;
		}
		if (const std::string stage = Glob2UI::tr(task->stage()); stage != status)
		{
			status = stage;
			invalidate();
		}
	}
	catch (const std::exception &error)
	{
		std::cerr << "Editor preparation failed: " << error.what() << '\n';
		endExecute(2);
	}
}
