// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "CommandLine.h"
#include <sstream>
#include <algorithm>
TEST_SUITE("CommandLine")
{
	TEST_CASE("registry describes every executable command and help is asset independent")
	{
		const auto schema = Cli::describe();
		CHECK_EQ(schema.at("cli_version"), 2);
		CHECK_EQ(schema.at("schema_version"), 1);
		CHECK_EQ(schema.at("commands").size(), Cli::commands().size());
		for (const auto &c : Cli::commands())
		{
			std::istringstream input(c.path);
			std::vector<std::string> args;
			std::string word;
			while (input >> word)
				args.push_back(word);
			args.push_back("--help");
			const auto request = Cli::parse(args);
			CHECK(request.help);
			CHECK(Cli::help(c.path).find(c.description) != std::string::npos);
			CHECK_EQ(Cli::describe(c.path).at("commands").size(), 1);
			CHECK_FALSE(c.outputs.empty());
			for (const auto &o : c.options)
				CHECK_FALSE(o.description.empty());
		}
	}
	TEST_CASE("default launch groups URLs and help paths")
	{
		CHECK_EQ(Cli::parse(std::vector<std::string>{}).command, "play");
		CHECK_EQ(Cli::parse({"map"}).positionals.at(0), "map");
		CHECK(Cli::parse({"map"}).help);
		CHECK_EQ(Cli::parse({"help", "map", "generate", "--format=json"}).get("--format"), "json");
		CHECK_EQ(Cli::parse({"glob2://j/ABCDEF"}).command, "online join");
		CHECK_EQ(Cli::parse({"play", "-h"}).command, "play");
	}
	TEST_CASE("typed options equals ordered repeats and literal filenames")
	{
		auto r = Cli::parse({"map", "generate", "river", "--output=result.map", "--set", "teams=2",
							 "--set=width=128"});
		CHECK_EQ(r.get("--output"), "result.map");
		CHECK_EQ(r.all("--set").size(), 2);
		CHECK_EQ(r.occurrences.at(1).second, "teams=2");
		r = Cli::parse({"replay", "--", "-named.replay"});
		CHECK_EQ(r.positionals.at(0), "-named.replay");
		CHECK_EQ(Cli::parse({"play", "--username=-name"}).get("--username"), "-name");
		CHECK_EQ(Cli::parse({"map", "generate", "river", "--output", "x", "--seed=4294967295"})
					 .get("--seed"),
				 "4294967295");
	}
	TEST_CASE("malformed input fails before handlers")
	{
		const std::vector<std::vector<std::string>> invalid = {
			{"--run-game"},
			{"--nox", "x", "10", "1"},
			{"play", "-f"},
			{"play", "--unknown"},
			{"play", "extra"},
			{"play", "--window-size", "1x2junk"},
			{"map", "preview", "x", "--output", "a.png", "--preview", "b.png"},
			{"play", "--username"},
			{"play", "--fullscreen", "--no-fullscreen"},
			{"play", "--fullscreen=true"},
			{"play", "--record-fps", "30"},
			{"map", "generate", "river", "--output", "x", "--seed", "-1"},
			{"map", "generate", "river", "--output", "x", "--seed", "4294967296"},
			{"map", "generate", "river", "--output", "x", "--seed", "1", "--seed", "2"},
			{"map", "generate", "river"},
			{"match", "verify", "x"},
			{"game", "run", "--output-dir", "x"},
			{"game", "run", "--output-dir", "x", "--load-game", "save", "--player", "castor"},
			{"online", "turn-client", "a", "--map-file", "m", "--output-dir", "o",
			 "--orders-per-second", "NaN"},
			{"completion", "unknown"},
			{"help", "missing"}};
		for (const auto &args : invalid)
			CHECK_THROWS_AS(Cli::parse(args), std::invalid_argument);
	}
	TEST_CASE("every leaf accepts a minimal request and rejects missing required inputs")
	{
		for (const auto &c : Cli::commands())
		{
			if (c.path == "help")
				continue;
			std::istringstream path(c.path);
			std::vector<std::string> base;
			std::string word;
			while (path >> word)
				base.push_back(word);
			auto args = base;
			for (unsigned i = 0; i < c.minimumPositionals; ++i)
				args.push_back(c.path == "completion" ? "bash" : "fixture");
			for (const auto &o : c.options)
				if (o.required)
				{
					args.push_back(o.name);
					args.push_back("fixture");
				}
			if (c.path == "map generate" || c.path == "map preview")
				args.insert(args.end(), {"--output", "fixture"});
			if (c.path == "game run")
				args.insert(args.end(), {"--load-game", "fixture"});
			CHECK_EQ(Cli::parse(args).command, c.path);
			if (c.minimumPositionals || std::any_of(c.options.begin(), c.options.end(),
													[](const auto &o) { return o.required; }))
				CHECK_THROWS_AS(Cli::parse(base), std::invalid_argument);
			for (const auto &o : c.options)
			{
				if (o.type == "flag")
					continue;
				auto missing = base;
				missing.push_back(o.name);
				CHECK_THROWS_AS(Cli::parse(missing), std::invalid_argument);
				if (!o.repeatable)
				{
					auto duplicate = args;
					// Use help to bypass command requirements while checking scalar parsing.
					duplicate = base;
					duplicate.push_back("--help");
					std::string v = o.choices.empty() ? o.type == "integer"
															? std::to_string(o.minimum)
														: o.type == "resolution" ? "640x480"
														: o.type == "assignment" ? "key=value"
														: o.type == "compute"    ? "auto"
														: o.type == "real"       ? "1"
																				 : "fixture"
													  : o.choices.front();
					duplicate.insert(duplicate.end(), {o.name, v, o.name, v});
					CHECK_THROWS_AS(Cli::parse(duplicate), std::invalid_argument);
				}
			}
			auto jsonHelp = base;
			jsonHelp.insert(jsonHelp.end(), {"--help", "--format=json"});
			CHECK_EQ(Cli::parse(jsonHelp).get("--format"), "json");
		}
	}
	TEST_CASE("completion scripts contain all command options without running jobs")
	{
		for (const auto &shell : {"bash", "zsh", "fish"})
		{
			const auto script = Cli::completion(shell);
			CHECK(script.find("Generated from Glob2 CLI 2") != std::string::npos);
			CHECK(script.find("compute-threads") != std::string::npos);
			CHECK(script.find("game run") != std::string::npos);
		}
	}
}
