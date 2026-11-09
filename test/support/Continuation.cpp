// SPDX-License-Identifier: GPL-3.0-or-later
#include "Continuation.h"

#include <BinaryStream.h>
#include <StreamBackend.h>
#include <TextStream.h>

#include <sstream>

namespace glob2test
{
	std::vector<Uint32> simulationComponents(Game& game, bool legacy152Representation)
	{
		std::vector<Uint32> result, buildings, units;
		game.checkSum(&result, &buildings, &units, true, legacy152Representation ? ChecksumScheme::Legacy152 : ChecksumScheme::Current);
		// Only the frozen layout's map-header word mixes in the format version
		// saving rewrites; drop it there so historical traces stay comparable.
		if (legacy152Representation && !result.empty())
			result.erase(result.begin());
		result.insert(result.end(), buildings.begin(), buildings.end());
		result.insert(result.end(), units.begin(), units.end());
		return result;
	}

	std::string saveGameBytes(Game& game, const std::string& name, bool text)
	{
		auto* backend = new GAGCore::MemoryStreamBackend;
		std::unique_ptr<GAGCore::OutputStream> output(text
			? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend))
			: static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(backend)));
		game.save(output.get(), false, name);
		output->flush();
		return backend->takeContents();
	}

	std::string saveGUIBytes(GameGUI& gui, const std::string& name)
	{
		auto* backend = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(backend);
		gui.save(&output, name);
		output.flush();
		return backend->takeContents();
	}

	bool loadGameBytes(Game& game, const std::string& bytes, bool text)
	{
		auto backend = std::make_unique<GAGCore::MemoryStreamBackend>();
		backend->write(bytes.data(), bytes.size());
		backend->seekFromStart(0);
		// TextInputStream parses synchronously and does not own its backend;
		// BinaryInputStream owns and deletes the backend handed to it.
		if (text)
		{
			GAGCore::TextInputStream input(backend.get());
			return game.load(&input);
		}
		GAGCore::BinaryInputStream input(backend.release());
		return game.load(&input);
	}

	std::unique_ptr<GameGUI> loadGameGUI(const std::string& bytes, bool throughGUI)
	{
		auto gui = std::make_unique<GameGUI>(false);
		auto* backend = new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size());
		backend->seekFromStart(0);
		GAGCore::BinaryInputStream input(backend);
		const bool loaded = throughGUI ? gui->load(&input) : gui->game.load(&input);
		REQUIRE_MESSAGE(loaded, "the saved continuation state loads");
		gui->game.setWaitingOnMask(0);
		return gui;
	}

	void stepEngine(Game& game)
	{
		game.step(0);
		if (game.gui)
			game.gui->consumeClientEvents();
	}

	namespace
	{
		std::string describeComponents(const std::vector<Uint32>& reference, const std::vector<Uint32>& loaded)
		{
			std::ostringstream text;
			std::size_t index = 0;
			while (index < reference.size() && index < loaded.size() && reference[index] == loaded[index])
				++index;
			text << "simulation components differ: " << reference.size() << " reference and " << loaded.size()
				 << " loaded words, first difference at index " << index;
			if (index < reference.size() && index < loaded.size())
				text << " (" << reference[index] << " vs " << loaded[index] << ")";
			return text.str();
		}

		// The first difference between the two games, or an empty string.
		std::string difference(Game& reference, Game& loaded, const ContinuationOptions& options,
			const std::vector<std::string>* referenceOrders, const std::vector<std::string>* loadedOrders)
		{
			const auto expected = simulationComponents(reference);
			const auto actual = simulationComponents(loaded);
			if (expected != actual)
				return describeComponents(expected, actual);
			if (!(reference.syncRandom == loaded.syncRandom))
				return "the synchronized random streams differ";
			if (referenceOrders && *referenceOrders != *loadedOrders)
			{
				std::ostringstream text;
				text << "orders differ: " << referenceOrders->size() << " reference and " << loadedOrders->size()
					 << " loaded";
				for (std::size_t i = 0; i < referenceOrders->size() && i < loadedOrders->size(); ++i)
					if ((*referenceOrders)[i] != (*loadedOrders)[i])
					{
						text << ", first at order " << i;
						break;
					}
				return text.str();
			}
			return {};
		}

		std::vector<std::string> advance(Game& game, const ContinuationOptions& options)
		{
			auto random = game.bindRandom();
			if (options.step)
				return options.step(game);
			game.step(0);
			return {};
		}
	}

	ContinuationResult expectContinuation(Game& original, const ContinuationOptions& options)
	{
		ContinuationResult result;
		const auto bytes = options.gui ? saveGUIBytes(*options.gui, options.name) : saveGameBytes(original, options.name);
		const auto load = [&]
		{
			auto gui = loadGameGUI(bytes, options.gui != nullptr);
			if (options.afterLoad)
				options.afterLoad(original, gui->game);
			return gui;
		};
		if (options.twoLoads)
			result.reference = load();
		result.loaded = load();
		Game& reference = options.twoLoads ? result.reference->game : original;
		Game& loaded = result.loaded->game;

		const auto compare = [&](int tick, const std::vector<std::string>* referenceOrders,
			const std::vector<std::string>* loadedOrders)
		{
			const auto problem = difference(reference, loaded, options, referenceOrders, loadedOrders);
			if (!problem.empty())
			{
				result.matched = false;
				result.divergedAt = tick;
				const std::string where = tick < 0 ? std::string("right after loading")
					: "after continuation tick " + std::to_string(tick);
				FAIL_CHECK("continuation of \"" << options.name << "\" diverged " << where << ": " << problem);
				return false;
			}
			if (options.each)
				options.each(reference, loaded, tick);
			return true;
		};

		if (options.compareLoaded && !compare(-1, nullptr, nullptr))
			return result;
		for (int tick = 0; tick < options.ticks; ++tick)
		{
			const auto referenceOrders = advance(reference, options);
			const auto loadedOrders = advance(loaded, options);
			if (!compare(tick, &referenceOrders, &loadedOrders))
				return result;
		}
		return result;
	}
}
