// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameLoadScreen.h"
#include "Engine.h"
#include "Utilities.h"
#include <iostream>
#include <PerformanceTelemetry.h>
GameLoadScreen::GameLoadScreen(Initializer initialize, GAGCore::CooperativeSlice slice)
	: GameLoadScreen(std::make_unique<Engine>(), std::move(initialize), std::move(slice))
{
}
GameLoadScreen::GameLoadScreen(std::unique_ptr<Engine> engine, Initializer initialize,
							   GAGCore::CooperativeSlice slice)
	: slice(std::move(slice)), previousRng(getSyncRandState()), engine(std::move(engine))
{
	if (!this->engine)
		throw std::invalid_argument("A loader requires an engine");
	status = Glob2UI::tr("[Loading headers]");
	task.emplace(initialize(*this->engine));
}
Glob2UI::Element GameLoadScreen::build(const Glob2UI::Presentation &p)
{
	using namespace Glob2UI;
	return page("", center(paragraph(status, {FontRole::Body, false, TextAlign::Center})),
				actions({{"cancel", tr("[Cancel]"), [this] { endExecute(0); }, false, SDLK_ESCAPE}}, p),
				p, 480);
}
GameLoadScreen::~GameLoadScreen()
{
	task.reset();
	if (!accepted)
		engine->cancelInitialization();
	engine.reset();
	if (!accepted)
		setSyncRandState(previousRng);
}
std::unique_ptr<Engine> GameLoadScreen::takeEngine()
{
	if (!task->result())
		throw std::logic_error("Cannot accept a failed game load");
	accepted = true;
	task.reset();
	return std::move(engine);
}
std::string GameLoadScreen::failureMessage() const
{
	auto message = Glob2UI::tr("[ERROR_CANT_LOAD_MAP]");
	if (!failureDiagnostic.empty())
		message += "\n\n" + failureDiagnostic;
	return message;
}
void GameLoadScreen::onTimer(Uint32)
{
	PERF_SCOPE_TIME(Load);
	try
	{
		if (slice.advance(*task))
		{
			if (!task->result())
				failureDiagnostic = engine->getInitializationDiagnostic();
			endExecute(task->result() ? 1 : 2);
			return;
		}
		const char *stage = task->stage();
		if (*stage && status != Glob2UI::tr(stage))
		{
			status = Glob2UI::tr(stage);
			invalidate();
		}
	}
	catch (const std::exception &error)
	{
		failureDiagnostic = error.what();
		std::cerr << "Game initialization failed: " << error.what() << '\n';
		endExecute(2);
	}
}
