// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GameGUIDialog.h"
#include "UIRecordingCanvas.h"
#include "ScopedEnvironment.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <chrono>
#include <cstdio>
#include <ui/Host.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

using namespace GAGGUI::ui;
using namespace GAGCore;

namespace
{
struct CountingText : FixedTextMeasurer
{
	mutable size_t calls = 0;
	int width(FontRole role, const std::string &text) const override
	{
		++calls;
		return FixedTextMeasurer::width(role, text);
	}
};

std::vector<AITelemetry::NamedValue> stressValues(size_t count)
{
	std::vector<AITelemetry::NamedValue> values;
	for (size_t i = 0; i < count; ++i)
	{
		std::ostringstream name;
		name << "stress.field" << std::setw(5) << std::setfill('0') << i;
		values.push_back({name.str(), "42", "units", "A readable explanation of this field", 128});
	}
	return values;
}

struct DialogFixture
{
	glob2test::HeadlessGlobals globals{{.loadStrings = true}};
	glob2test::HeadlessGame world;
	Scene scene;
	CountingText text;
	InGameAITelemetryScreen dialog{&world.gui};
	DialogFixture(int width = 1280, int height = 800, bool touch = false)
	{
		world.gui.setPublishedScene(&scene);
		dialog.host().setMeasurer(&text);
		dialog.host().setPresentation(Presentation::forSurface(width, height, 1, touch));
		scene.tick = 128;
		scene.panels.aiTelemetry.push_back({0, 0, "Maxima", true, stressValues(50)});
	}
	void update(Uint32 now = 1000) { dialog.update(now); }
	Node *list() { return dialog.host().find("telemetry/fields"); }
	std::string selected() { return list() ? list()->accessibleText() : ""; }
	std::string paragraphs()
	{
		std::string result;
		dialog.host().root()->visit(
			[&](Node &node)
			{
				if (std::string(node.name()) == "paragraph")
					result += node.accessibleText() + "\n";
			});
		return result;
	}
	void key(SDL_Keycode key)
	{
		SDL_Event event{};
		event.type = SDL_EVENT_KEY_DOWN;
		event.key.key = key;
		dialog.host().event(event);
	}
	void search(const std::string &query)
	{
		dialog.host().beginEditing("telemetry/search");
		dialog.host().find("telemetry/search")->keyDown({SDLK_A, SDL_KMOD_CTRL}, dialog.host());
		dialog.host().find("telemetry/search")->textInput(query, dialog.host());
		update();
	}
};
} // namespace

TEST_CASE("telemetry layout and painting stay bounded with full schemas" *
		  doctest::test_suite("AITelemetryUI"))
{
	for (bool phone : {false, true})
	{
		glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI", phone ? "1" : "0");
		DialogFixture f(phone ? 390 : 1280, phone ? 760 : 800, phone);
		const auto checksum = f.world.checksum();
		auto measure = [&](std::vector<AITelemetry::NamedValue> values)
		{
			f.scene.panels.aiTelemetry[0].values = std::move(values);
			f.scene.tick += 32;
			f.text.calls = 0;
			f.update();
			const auto layoutCalls = f.text.calls;
			glob2test::RecordingCanvas canvas(f.dialog.host().presentation().viewport.size(),
											  f.text);
			f.dialog.host().paint(canvas, 1000);
			CHECK(canvas.texts.size() < 100);
			CHECK(f.dialog.host().bounds("telemetry/details").h <=
				  f.dialog.host().rootBounds().h / 3);
			CHECK(f.dialog.host().presentation().viewport.contains(
				f.dialog.host().bounds("telemetry/close")));
			CHECK(f.list()->scrollMaximum() > 0);
			return layoutCalls;
		};
		const auto small = measure(stressValues(50));
		const auto large = measure(stressValues(AITelemetry::MaximumPresentationValues));
		CHECK(large == small);
		std::vector<AITelemetry::NamedValue> maxima;
		const auto &schema = AITelemetry::schema(7);
		for (size_t i = 0; i < schema.size(); ++i)
		{
			if (i >= AITelemetry::OrderTypes && i < AITelemetry::Specific)
				continue;
			const auto &field = schema[i];
			maxima.push_back({field.name, "na", field.unit, field.meaning, 128});
		}
		CHECK(maxima.size() > 600);
		CHECK(measure(std::move(maxima)) < small * 4);
		CHECK(f.world.checksum() == checksum);
	}
}

TEST_CASE("telemetry selection filtering samples and access changes preserve safe state" *
		  doctest::test_suite("AITelemetryUI"))
{
	DialogFixture f;
	f.update();
	f.dialog.host().focus("telemetry/fields", true);
	f.key(SDLK_DOWN);
	f.update();
	CHECK(f.selected().find("stress.field00001") == 0);
	f.list()->scrollTo(300, f.dialog.host());
	f.update();
	const int offset = f.list()->scrollOffset();
	f.scene.panels.aiTelemetry[0].values[1].value = "99";
	f.scene.tick += 32;
	f.update();
	CHECK(f.selected() == "stress.field00001: 99 units");
	CHECK(f.list()->scrollOffset() == offset);
	CHECK(f.dialog.host().focused() == "telemetry/fields");
	f.search("FIELD00001");
	CHECK(f.selected() == "stress.field00001: 99 units");
	CHECK(f.dialog.host().editing() == "telemetry/search");
	f.search("explanation");
	CHECK(f.selected().find("stress.field00001") == 0);
	f.search("42");
	CHECK(f.selected().find("stress.field00000") == 0);
	f.search("missing");
	CHECK(f.list() == nullptr);
	CHECK(f.paragraphs().find("No fields match") != std::string::npos);
	f.search("");
	f.scene.panels.aiTelemetry.push_back(
		{1, 1, "Numbi", true, {{"secret.field", "attack", "", "secret plan", 128}}});
	f.update();
	f.dialog.host().endEditing();
	f.dialog.host().focus("telemetry/player", true);
	f.key(SDLK_RIGHT);
	f.update();
	CHECK(f.selected() == "secret.field: attack");
	CHECK(f.list()->scrollOffset() == 0);
	// A held press must defer normal sample rebuilds, but not permission loss.
	SDL_Event down{};
	down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	down.button.button = SDL_BUTTON_LEFT;
	down.button.x = f.list()->bounds.center().x;
	down.button.y = f.list()->bounds.center().y;
	f.dialog.host().event(down);
	REQUIRE(f.dialog.host().interacting());
	f.scene.tick += 32;
	f.scene.panels.aiTelemetry[1].values[0].value = "defend";
	f.update();
	CHECK(f.selected() == "secret.field: attack");
	f.scene.panels.aiTelemetry.pop_back();
	f.update();
	CHECK(f.paragraphs().find("secret") == std::string::npos);
	CHECK(f.selected().find("stress.field00000") == 0);
	f.dialog.host().cancelInput();
	f.scene.panels.aiTelemetry[0].available = false;
	f.scene.tick += 32;
	f.update();
	CHECK(f.list() == nullptr);
	CHECK(f.paragraphs().find("Telemetry unavailable") != std::string::npos);
	f.scene.panels.aiTelemetry.clear();
	f.update();
	CHECK(f.paragraphs().find("No accessible AI telemetry") != std::string::npos);
}

TEST_CASE("telemetry waits for touch coasting and scrolls long details independently" *
		  doctest::test_suite("AITelemetryUI"))
{
	DialogFixture f(390, 760, true);
	f.scene.panels.aiTelemetry[0].values[0].name = "aaa." + std::string(200, 'x');
	f.scene.panels.aiTelemetry[0].values[0].meaning = std::string(2000, 'm');
	f.update(1000);
	CHECK(f.paragraphs().find(std::string(200, 'x')) != std::string::npos);
	auto *details = f.dialog.host().find("telemetry/details");
	REQUIRE(details);
	CHECK(details->scrollMaximum() > 0);
	details->scrollTo(100, f.dialog.host());
	f.update(1000);
	CHECK(f.list()->scrollOffset() == 0);
	const auto bounds = f.list()->bounds;
	auto finger = [&](Uint32 type, int y, Uint32 now)
	{
		SDL_Event event{};
		event.type = type;
		event.tfinger.timestamp = SDL_MS_TO_NS(now);
		event.tfinger.touchID = 1;
		event.tfinger.fingerID = 1;
		event.tfinger.x = float(bounds.center().x) / 390;
		event.tfinger.y = float(y) / 760;
		f.dialog.host().event(event);
	};
	finger(SDL_EVENT_FINGER_DOWN, bounds.y + bounds.h * 3 / 4, 1000);
	finger(SDL_EVENT_FINGER_MOTION, bounds.y + bounds.h / 2, 1020);
	finger(SDL_EVENT_FINGER_MOTION, bounds.y + bounds.h / 4, 1040);
	finger(SDL_EVENT_FINGER_UP, bounds.y + bounds.h / 4, 1045);
	REQUIRE(f.dialog.host().animating());
	f.scene.tick += 32;
	f.scene.panels.aiTelemetry[0].values[0].value = "99";
	f.update(1060);
	CHECK(f.selected().find(": 42 units") != std::string::npos);
	CHECK(f.dialog.host().animating());
	for (Uint32 now = 1076; now < 6000 && f.dialog.host().animating(); now += 16)
		f.update(now);
	CHECK_FALSE(f.dialog.host().animating());
	const int offset = f.list()->scrollOffset();
	f.update(6000);
	CHECK(f.selected().find(": 99 units") != std::string::npos);
	CHECK(f.list()->scrollOffset() == offset);
	CHECK(f.dialog.host().find("telemetry/details")->scrollOffset() == 100);
}

TEST_CASE("telemetry full-game timings and desktop phone captures [display:1280x800] [artifacts]" *
		  doctest::test_suite("AITelemetryUI"))
{
	for (bool phone : {false, true})
	{
		glob2test::ScopedEnvironment mobile("GLOB2_MOBILE_UI", phone ? "1" : "0");
		glob2test::HeadlessGlobals globals({.display = true,
											.loadStrings = true,
											.width = phone ? 390 : 1280,
											.height = phone ? 760 : 800});
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true, .seed = 23});
		GameHeader header;
		header.setNumberOfPlayers(2);
		header.getBasePlayer(0) = BasePlayer(0, "Viewer", 0, BasePlayer::P_LOCAL);
		header.getBasePlayer(1) =
			BasePlayer(1, "Maxima", 0, BasePlayer::playerTypeFromImplementationID(AI::MAXIMA));
		header.setRandomSeed(23);
		world.game.setGameHeader(header, true);
		world.addBuilding("swarm", 2, 2);
		world.addBuilding("inn", 10, 10);
		for (int i = 0; i < 12; ++i)
			world.addUnit(WORKER);
		for (int i = 0; i < 64; ++i)
			glob2test::stepAI(world.game);
		const auto fixture = glob2test::artifactDir() / "telemetry-initial.game";
		{
			FILE *file = std::fopen(fixture.string().c_str(), "wb");
			REQUIRE(file);
			BinaryOutputStream output(new FileStreamBackend(file));
			world.game.save(&output, false, "telemetry benchmark");
			output.flush();
		}
		if (const char *save = SDL_getenv_unsafe("GLOB2_TELEMETRY_BENCH_SAVE"))
		{
			FILE *file = std::fopen(save, "rb");
			REQUIRE(file);
			BinaryInputStream input(new FileStreamBackend(file));
			REQUIRE(world.game.load(&input));
		}
		world.gui.init();
		for (bool stress : {false, true})
		{
			std::vector<double> opening, refresh, scrolling, searching;
			Scene presentation;
			auto drawScene = [&]
			{
				world.gui.setPublishedScene(nullptr);
				world.gui.drawAll(0);
				REQUIRE_FALSE(world.gui.drawnScene().panels.aiTelemetry.empty());
				REQUIRE(world.gui.drawnScene().panels.aiTelemetry[0].values.size() > 600);
				if (stress)
				{
					presentation = world.gui.drawnScene();
					REQUIRE_FALSE(presentation.panels.aiTelemetry.empty());
					presentation.panels.aiTelemetry[0].values =
						stressValues(AITelemetry::MaximumPresentationValues);
					world.gui.setPublishedScene(&presentation);
				}
			};
			auto timed = [](auto work)
			{
				const auto start = std::chrono::steady_clock::now();
				work();
				return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
																 start)
					.count();
			};
			const auto startTick = world.game.stepCounter;
			for (int repeat = 0; repeat < 8; ++repeat)
			{
				drawScene();
				const auto checksum = world.checksum();
				InGameAITelemetryScreen dialog(&world.gui);
				opening.push_back(timed(
					[&]
					{
						dialog.attach(*globals->gfx);
						dialog.update(1000);
						dialog.draw(1000);
					}));
				CHECK(world.checksum() == checksum);
				if (repeat == 0)
				{
					const auto filename = std::string("telemetry-") +
										  (stress ? "stress" : "maxima") +
										  (phone ? "-phone.bmp" : "-desktop.bmp");
					globals->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() + "/" +
											  filename);
					globals->gfx->nextFrame();
				}
				for (int tick = 0; tick < 32; ++tick)
				{
					world.step();
					if (SDL_getenv_unsafe("GLOB2_TELEMETRY_BENCH_TRACE"))
						std::printf("TELEMETRY_TICK phone=%d stress=%d tick=%u checksum=%u\n",
									phone, stress, world.game.stepCounter, world.checksum());
				}
				drawScene();
				const auto advancedChecksum = world.checksum();
				refresh.push_back(timed(
					[&]
					{
						dialog.update(1100);
						dialog.draw(1100);
					}));
				auto *list = dialog.host().find("telemetry/fields");
				if (!list)
					list = dialog.host().find("telemetry/scroll");
				REQUIRE(list);
				scrolling.push_back(timed(
					[&]
					{
						list->scrollTo(240, dialog.host());
						dialog.update(1200);
						dialog.draw(1200);
					}));
				searching.push_back(timed(
					[&]
					{
						dialog.host().beginEditing("telemetry/search");
						dialog.host()
							.find("telemetry/search")
							->textInput(stress ? "field00001" : "food", dialog.host());
						dialog.update(1300);
						dialog.draw(1300);
					}));
				CHECK(world.checksum() == advancedChecksum);
			}
			CHECK(world.game.stepCounter - startTick == 256);
			auto report = [&](const char *phase, std::vector<double> times)
			{
				const double first = times.front();
				std::sort(times.begin(), times.end());
				std::printf("TELEMETRY_BENCH phone=%d stress=%d phase=%s samples=%zu "
							"first_ms=%.3f median_ms=%.3f p95_ms=%.3f ticks=%u checksum=%u\n",
							phone, stress, phase, times.size(), first,
							(times[times.size() / 2 - 1] + times[times.size() / 2]) / 2,
							times.back(), world.game.stepCounter, world.checksum());
			};
			report("opening", opening);
			report("refresh", refresh);
			report("scrolling", scrolling);
			report("searching", searching);
			world.gui.setPublishedScene(nullptr);
		}
	}
}
