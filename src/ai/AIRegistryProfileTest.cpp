// SPDX-License-Identifier: GPL-3.0-or-later
// What a compile_profile=core build does with the AIs and generators it leaves out
// (docs/development/build-options.md#core-compile-profile).
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "AI.h"
#include "AIRegistry.h"
#include "CompileProfile.h"
#include "GeneratorRegistry.h"
#include "Player.h"
#include "Version.h"
#include <BinaryStream.h>
#include <functional>
#include <StreamBackend.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
using namespace GAGCore;
const std::string Rebuild = "rebuild with compile_profile=full";

// An AI section as AI::save writes it, naming the implementation and nothing else.
std::string savedAI(AI::ImplementationID id)
{
	auto *storage = new MemoryStreamBackend;
	{
		BinaryOutputStream out(storage);
		out.writeEnterSection("AI");
		out.write("AI b", 4, "signatureStart");
		out.writeUint32(Uint32(id), "implementitionID");
		out.writeUint32(0, "state");
		out.writeLeaveSection();
		out.flush();
		return storage->takeContents();
	}
}

std::string failure(const std::function<void()> &action)
{
	try
	{
		action();
	}
	catch (const std::exception &error)
	{
		return error.what();
	}
	return "";
}
} // namespace

TEST_SUITE("AIRegistryProfile")
{
	TEST_CASE("an AI id is available exactly when the profile builds it")
	{
		const AI::ImplementationID optional[] = {AI::CASTOR, AI::CORTEX, AI::CABINO};
		for (int id : {AI::NONE, AI::NUMBI, AI::WARRUSH, AI::ECONO, AI::NICOWAR, AI::MAXIMA, AI::JAVASCRIPT})
			CHECK(AIRegistry::available(id));
		for (AI::ImplementationID id : optional)
		{
			CAPTURE(id);
			CHECK(AIRegistry::available(id) == CompileProfile::isFull());
		}
		CHECK_FALSE(AIRegistry::available(AI::SIZE));
		CHECK_FALSE(AIRegistry::available(-1));
	}

	TEST_CASE("a save naming an AI this build leaves out fails with a clear error")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world{glob2test::GameOptions{.header = true}};
		Player *player = world.game.players[0];
		int missing = 0;
		for (int id = 0; id < AI::SIZE; ++id)
		{
			if (AIRegistry::available(id))
				continue;
			CAPTURE(id);
			++missing;
			const std::string bytes = savedAI(AI::ImplementationID(id));
			const std::string loaded = failure([&] {
				MemoryStreamBackend storage(bytes.data(), bytes.size());
				storage.seekFromStart(0);
				BinaryInputStream in(new MemoryStreamBackend(storage));
				AI ai(&in, player, VERSION_MINOR);
			});
			CHECK(loaded.find("not compiled into this build") != std::string::npos);
			CHECK(loaded.find(Rebuild) != std::string::npos);
			const std::string created = failure([&] { player->makeItAI(AI::ImplementationID(id)); });
			CHECK(created == AIRegistry::unavailableMessage(id));
		}
		// The full profile builds every AI; the error path only exists in reduced profiles.
		CHECK((missing == 0) == CompileProfile::isFull());
	}

	TEST_CASE("generators the profile leaves out keep their ids and name the profile")
	{
		const auto &registry = GeneratorRegistry::builtins();
		for (const char *kept : {"lava-shield", "uniform", "hills", "forts", "even-ground"})
			CHECK_NOTHROW(registry.idOf(kept));
		CHECK(std::string(registry.at(registry.defaultMethod(false)).id) == "lava-shield");
		const std::string river = failure([&] { registry.idOf("river"); });
		if (CompileProfile::hasGenerators())
			CHECK(river.empty());
		else
			CHECK(river.find(Rebuild) != std::string::npos);
	}
}
