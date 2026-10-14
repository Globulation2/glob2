// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ReplayTelemetry.h"
#include "Version.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <limits>
#include <utility>

namespace
{
using namespace GAGCore;
using ReplayTelemetry::Frame;
using ReplayTelemetry::Row;
using TelemetryStream = ReplayTelemetry::Stream;

void text(OutputStream &out, const std::string &value)
{
	out.writeUint32(value.size(), "length");
	if (!value.empty())
		out.write(value.data(), value.size(), "text");
}
void frame(OutputStream &out, const Frame &value)
{
	out.writeUint32(value.step, "step");
	out.writeUint32(value.changes.size(), "changes");
	for (const auto &row : value.changes)
	{
		out.writeUint32(row.team, "team");
		out.writeUint32(row.player, "player");
		text(out, row.name);
		out.writeUint8(row.available, "available");
		out.writeUint32(row.values.size(), "fields");
		for (const auto &v : row.values)
		{
			text(out, v.name);
			text(out, v.value);
			text(out, v.unit);
			text(out, v.meaning);
			out.writeUint32(v.updated, "updated");
		}
	}
}
// Independent encoder of the existing AIT1 contract, using public legacy rows.
std::string legacyBytes(const std::vector<Frame> &frames, Uint32 until, bool truncated = false)
{
	auto *memory = new MemoryStreamBackend;
	BinaryOutputStream out(memory);
	out.writeUint32(0x41495431, "magic");
	out.writeUint32(1, "version");
	out.writeUint32(until, "until");
	out.writeUint8(truncated, "truncated");
	out.writeUint32(frames.size(), "frames");
	for (const auto &f : frames)
		frame(out, f);
	return memory->takeContents();
}
std::string encoded(const TelemetryStream &stream)
{
	auto *memory = new MemoryStreamBackend;
	BinaryOutputStream out(memory);
	stream.write(&out);
	return memory->takeContents();
}
void read(TelemetryStream &stream, const std::string &bytes)
{
	BinaryInputStream in(new MemoryStreamBackend(bytes.data(), bytes.size()));
	in.seekFromStart(0);
	BinaryInputStream::CheckedReads checked(&in);
	stream.read(&in);
	CHECK(in.getPosition() == bytes.size());
}
std::shared_ptr<AITelemetry::Series> attach(Game &game, int team = 0)
{
	auto series = std::make_shared<AITelemetry::Series>();
	series->player = team;
	series->playerName = "AI";
	series->current.available = true;
	game.teams[team]->stats.aiTelemetry = {series};
	return series;
}
std::vector<Frame> unusualFrames()
{
	Row a{0, 0, std::string("A\0I", 3), true,
		{{"same", "0", "units", "first", 0},
		 {"same", std::string("1\0x", 3), "units", "first", 1},
		 {std::string("N\0", 2), "", std::string("U\0", 2), std::string("M\0", 2), 2}}};
	Row b{1, 1, "other", true, {{"same", "2", "units", "second", 3}}};
	std::vector<Frame> frames{{0, {a, b}}};
	std::swap(a.values[0], a.values[2]);
	a.values[1].updated = 32; // Same text; update stamp is part of the payload.
	frames.push_back({32, {a}});
	a.available = false;
	a.values.clear();
	frames.push_back({64, {a}});
	a.available = true;
	a.values = {{"new", "value", "", "new schema", 96},
				{"new", "value", "", "new schema", 97}};
	frames.push_back({96, {a, b}});
	return frames;
}
} // namespace

TEST_SUITE("ReplayTelemetry")
{
	TEST_CASE("legacy bytes preserve duplicate fields NUL order schemas copy and move")
	{
		const auto bytes = legacyBytes(unusualFrames(), 128);
		TelemetryStream stream;
		read(stream, bytes);
		CHECK(encoded(stream) == bytes);
		TelemetryStream copy = stream;
		stream = {};
		CHECK(encoded(copy) == bytes);
		TelemetryStream moved = std::move(copy);
		CHECK(encoded(moved) == bytes);
		TelemetryStream assigned;
		assigned = moved;
		moved = {};
		CHECK(encoded(assigned) == bytes);
	}
	TEST_CASE("playback expands exact values stamps unavailability and reloaded history")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 2});
		const auto frames = unusualFrames();
		const auto bytes = legacyBytes(frames, 128, true);
		TelemetryStream playback;
		read(playback, bytes);
		const auto checksum = world.checksum();
		for (Uint32 step : {0u, 31u, 32u, 64u, 96u, 128u, 129u})
		{
			playback.apply(world.game, step);
			const Row *expected = nullptr;
			for (const auto &f : frames)
				if (f.step <= step)
					for (const auto &r : f.changes)
						if (r.player == 0)
							expected = &r;
			REQUIRE(expected);
			const auto &series = *world.game.teams[0]->stats.aiTelemetry.at(0);
			CHECK(series.playerName == expected->name);
			CHECK(series.current.available == (expected->available && step <= 128));
			CHECK(series.named == (series.current.available ? expected->values
															   : std::vector<AITelemetry::NamedValue>{}));
			CHECK(world.checksum() == checksum);
		}
		// Replay rewind loads a fresh stream, rather than rewinding apply's cursor.
		TelemetryStream reloaded;
		read(reloaded, bytes);
		// The synthetic AIT1 row includes stamps 1 and 2 at replay step zero.
		// Saves independently require stamps not to exceed the world's current tick.
		world.game.stepCounter = 128;
		reloaded.apply(world.game, 0);
		CHECK(world.game.teams[0]->stats.aiTelemetry[0]->named == frames[0].changes[0].values);
		// Presentation-only values keep their existing final-state save representation.
		auto *memory = new MemoryStreamBackend;
		BinaryOutputStream saved(memory);
		AITelemetry::save(&saved, world.game.teams[0]->stats.aiTelemetry);
		const auto savedBytes = memory->takeContents();
		BinaryInputStream in(new MemoryStreamBackend(savedBytes.data(), savedBytes.size()));
		in.seekFromStart(0);
		std::vector<std::shared_ptr<AITelemetry::Series>> restored;
		AITelemetry::load(&in, restored, VERSION_MINOR);
		REQUIRE(restored.size() == 1);
		CHECK(restored[0]->named == frames[0].changes[0].values);
	}
	TEST_CASE("capture skips invalid fields and retains exact changes and disappearance")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world;
		auto series = attach(world.game);
		series->fields = {{"signed", "x", "signed", AITelemetry::Signed},
						  {"omitted", "", "invalid", AITelemetry::Unsigned}};
		series->current.values = {{static_cast<Uint64>(Sint64(-4)), 0, true}, {9, 0, false}};
		series->named = {{"named", std::string("v\0", 2), "", "named", 0},
						 {"named", "duplicate", "", "named", 0}};
		TelemetryStream recording;
		Row row{0, 0, "AI", true,
			{{"signed", "-4", "x", "signed", 0}, series->named[0], series->named[1]}};
		std::vector<Frame> expected{{0, {row}}};
		recording.capture(world.game, 0);
		recording.capture(world.game, 31); // No changed row, but coverage advances.
		CHECK(encoded(recording) == legacyBytes(expected, 31));
		series->named[0].updated = 32;
		row.values[1].updated = 32;
		expected.push_back({32, {row}});
		recording.capture(world.game, 32);
		series->fields[0].meaning = "changed metadata";
		row.values[0].meaning = "changed metadata";
		expected.push_back({64, {row}});
		recording.capture(world.game, 64);
		series->current.values[0].valid = false;
		row.values.erase(row.values.begin());
		expected.push_back({96, {row}});
		recording.capture(world.game, 96);
		series->active = false;
		row.available = false;
		row.values.clear();
		expected.push_back({128, {row}});
		recording.capture(world.game, 128);
		CHECK(encoded(recording) == legacyBytes(expected, 128));
		series->active = true;
		series->current.available = false;
		expected.push_back({160, {row}});
		recording.capture(world.game, 160);
		CHECK(encoded(recording) == legacyBytes(expected, 160));
	}
	TEST_CASE("duplicate players compare with prior capture and schemas can return")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world;
		auto first = attach(world.game);
		first->playerName = "first";
		first->fields = {{"typed", "u", "original", AITelemetry::Unsigned},
						 {"missing", "", "no value", AITelemetry::Unsigned}};
		first->current.values = {{1, 0, true}}; // Extra descriptors are ignored.
		Row a{0, 0, "first", true, {{"typed", "1", "u", "original", 0}}};
		TelemetryStream recording;
		std::vector<Frame> expected{{0, {a}}};
		recording.capture(world.game, 0);
		auto second = std::make_shared<AITelemetry::Series>();
		second->player = 0;
		second->playerName = "second";
		second->current.available = true;
		second->named = {{"other", "2", "", "second", 0}};
		world.game.teams[0]->stats.aiTelemetry.push_back(second);
		Row b{0, 0, "second", true, second->named};
		expected.push_back({32, {b}});
		recording.capture(world.game, 32);
		first->fields[0].meaning = "changed";
		a.values[0].meaning = "changed";
		// Each duplicate compares against the previous capture, not the preceding
		// row in this capture. The second row equals old current and is omitted,
		// while still becoming current for the next capture.
		expected.push_back({64, {a}});
		recording.capture(world.game, 64);
		CHECK(encoded(recording) == legacyBytes(expected, 64));
		world.game.teams[0]->stats.aiTelemetry.pop_back();
		expected.push_back({96, {a}});
		recording.capture(world.game, 96);
		first->fields.resize(1);
		first->current.values.push_back({999, 99, true}); // Extra values are ignored.
		first->fields[0].meaning = "original";
		a.values[0].meaning = "original";
		expected.push_back({128, {a}});
		recording.capture(world.game, 128);
		recording.capture(world.game, 160); // Returning schema is canonical too.
		CHECK(encoded(recording) == legacyBytes(expected, 160));
	}
	TEST_CASE("stable capture descriptors retain changing payloads and stamps")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world;
		auto series = attach(world.game);
		series->named = {{std::string("N\0", 2), "initial", "u", "m", 0},
						 {std::string("N\0", 2), "duplicate", "u", "m", 0}};
		TelemetryStream recording;
		std::vector<Frame> expected;
		for (Uint32 step = 0; step < 4; ++step)
		{
			// Only payloads/stamps change: every descriptor matches prior current.
			series->named[0].value = std::to_string(step);
			series->named[1].updated = step;
			expected.push_back({step, {{0, 0, "AI", true, series->named}}});
			recording.capture(world.game, step);
			CHECK(encoded(recording) == legacyBytes(expected, step));
		}
		// A copied stream has its own current row and shared immutable schemas.
		auto copy = recording;
		series->named[0].meaning = "changed";
		expected.push_back({4, {{0, 0, "AI", true, series->named}}});
		copy.capture(world.game, 4);
		CHECK(encoded(copy) == legacyBytes(expected, 4));
		CHECK(encoded(recording) == legacyBytes(std::vector<Frame>(expected.begin(), expected.end() - 1), 3));
		series->named[0].meaning = "m";
		series->named[0].updated = 5;
		expected.push_back({5, {{0, 0, "AI", true, series->named}}});
		copy.capture(world.game, 5); // Changed schema falls back, then an old schema returns.
		CHECK(encoded(copy) == legacyBytes(expected, 5));
	}
	TEST_CASE("schema lookup distinguishes prefixes and each metadata component")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world;
		auto series = attach(world.game);
		TelemetryStream recording;
		std::vector<Frame> expected;
		// Inserting in mixed order exercises both lookup comparison directions;
		// revisiting every schema also verifies canonical equality after lookup.
		const std::vector<std::vector<AITelemetry::NamedValue>> variants{
			{{"b", "v", "u", "m", 0}},
			{{"a", "v", "u", "m", 0}},
			{{"b", "v", "u", "m", 0}, {"b", "v", "u", "m", 0}},
			{{"b", "v", "t", "m", 0}},
			{{"b", "v", "u", "n", 0}},
			{}};
		Uint32 step = 0;
		for (int repeat = 0; repeat < 2; ++repeat)
			for (const auto &values : variants)
			{
				series->named = values;
				expected.push_back({step, {{0, 0, "AI", true, values}}});
				recording.capture(world.game, step);
				recording.capture(world.game, step + 1);
				CHECK(encoded(recording) == legacyBytes(expected, step + 1));
				step += 2;
			}
	}
	TEST_CASE("logical byte budget and frame limit retain legacy truncation points")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world;
		for (bool large : {false, true})
		{
			auto series = attach(world.game);
			const std::string part = large ? std::string(4096, 'x') : "x";
			series->named = {{part, part, part, part, 0}};
			const size_t cost = 8 + 32 + series->playerName.size() + 24 + 4 * part.size();
			const Uint32 accepted = std::min<size_t>(65536, (16 * 1024 * 1024) / cost);
			TelemetryStream recording;
			for (Uint32 step = 0; step <= accepted + 2; ++step)
			{
				series->named[0].updated = step;
				recording.capture(world.game, step);
			}
			auto *memory = new MemoryStreamBackend;
			BinaryOutputStream out(memory);
			out.writeUint32(0x41495431, "magic");
			out.writeUint32(1, "version");
			out.writeUint32(accepted - 1, "until");
			out.writeUint8(1, "truncated");
			out.writeUint32(accepted, "frames");
			for (Uint32 step = 0; step < accepted; ++step)
				frame(out, {step, {{0, 0, "AI", true, {{part, part, part, part, step}}}}});
			const auto expected = memory->takeContents();
			CHECK(encoded(recording) == expected);
			TelemetryStream restored;
			read(restored, expected);
			CHECK(encoded(restored) == expected);
		}
	}
	TEST_CASE("loader limits and incomplete-frame state keep the existing contract")
	{
		const auto frames = unusualFrames();
		const auto valid = legacyBytes(frames, 128);
		for (size_t offset : {size_t(0), size_t(4), size_t(13)})
		{
			auto bad = valid;
			for (size_t i = 0; i < 4; ++i)
				bad[offset + i] = '\xff'; // Magic/version/count must fail before row allocation.
			TelemetryStream rejected;
			CHECK_THROWS(read(rejected, bad));
			CHECK_FALSE(rejected.available());
		}
		for (int malformed : {0, 1, 2, 3, 4, 5})
		{
			auto badFrames = frames;
			if (malformed == 0)
				badFrames[1].step = 0;
			if (malformed == 1)
				badFrames[1].changes[0].team = Team::MAX_COUNT;
			if (malformed == 2)
				badFrames[1].changes[0].values[0].name.assign(4097, 'x');
			if (malformed == 3)
				badFrames[1].changes[0].values.resize(AITelemetry::MaximumPresentationValues + 1);
			if (malformed == 4)
				badFrames[1].changes[0].player = Team::MAX_COUNT;
			if (malformed == 5)
				badFrames[1].changes.resize(Team::MAX_COUNT * 2 + 1);
			TelemetryStream rejected;
			CHECK_THROWS(read(rejected, legacyBytes(badFrames, 128)));
			CHECK_FALSE(rejected.available());
			// Previously completed frames survive; the incomplete second frame does not.
			CHECK(encoded(rejected) == legacyBytes({frames[0]}, 128));
		}
		TelemetryStream empty;
		CHECK_FALSE(empty.available());
		CHECK(encoded(empty) == legacyBytes({}, 0));
	}
}
