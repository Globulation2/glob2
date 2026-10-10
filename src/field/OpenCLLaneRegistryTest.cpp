// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "field/OpenCLLaneRegistry.h"
#include "field/OpenCLGradient.h"

TEST_CASE("bounded lane registry keeps weak control credit after payload retirement" * doctest::test_suite("OpenCLGradient"))
{
    using namespace gradient_kernel;
    const auto baseline=openCLProbeBytes();
    struct Credit {std::size_t bytes=256;~Credit(){releaseOpenCLProbeBytes(bytes);}};
    struct Payload {bool* destroyed;~Payload(){*destroyed=true;}};
    bool firstDestroyed=false,secondDestroyed=false;
    OpenCLLaneRegistry<Payload,1> registry;
    REQUIRE(reserveOpenCLProbeBytes(256));auto firstCredit=std::make_shared<Credit>();
    auto first=std::shared_ptr<Payload>(new Payload{&firstDestroyed},[firstCredit](Payload* value){delete value;});
    std::weak_ptr<Payload> escaped=first;
    REQUIRE(registry.insert(first,firstCredit));firstCredit.reset();
    auto alias=registry.snapshot()[0];first.reset();CHECK_FALSE(firstDestroyed);
    REQUIRE(reserveOpenCLProbeBytes(256));auto secondCredit=std::make_shared<Credit>();
    auto second=std::shared_ptr<Payload>(new Payload{&secondDestroyed},[secondCredit](Payload* value){delete value;});
    CHECK_FALSE(registry.insert(second,secondCredit));CHECK(openCLProbeBytes()==baseline+512);
    alias.reset();CHECK(firstDestroyed);CHECK(openCLProbeBytes()==baseline+512);
    CHECK_FALSE(bool(registry.snapshot()[0]));
    REQUIRE(registry.insert(second,secondCredit));CHECK(openCLProbeBytes()==baseline+512);
    CHECK(escaped.expired());escaped.reset();CHECK(openCLProbeBytes()==baseline+256);
    second.reset();secondCredit.reset();CHECK(secondDestroyed);CHECK(openCLProbeBytes()==baseline+256);
    registry.clear();CHECK(openCLProbeBytes()==baseline);
}
