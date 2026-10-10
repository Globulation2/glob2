// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "field/OpenCLProbeBatchStorage.h"
#include "field/OpenCLChargedStorage.h"
#include "field/AdaptiveGradientPolicy.h"
#include <SDL3/SDL_platform_defines.h>

namespace gradient_kernel
{
// Hardware-free subdivision proof. Host/probe credits use the real shared
// budget. The device credit is explicitly synthetic: no driver allocation or
// initialized-device admission is claimed by these tests.
struct OpenCLProbeBatchStorageTests {
    struct Credit {
        std::shared_ptr<std::size_t> syntheticDevice;
        std::size_t paired=0,device=0;
        ~Credit(){if(paired)releaseOpenCLProbeBytes(paired);if(device)*syntheticDevice-=device;}
    };
    static std::shared_ptr<OpenCLProbeBatchStorage> create(std::size_t bytes,std::size_t device,
        const std::shared_ptr<std::size_t>& tracked) {
        if(bytes<OpenCLProbeBatchStorage::metadataBytes() || !reserveOpenCLProbeBytes(bytes))return {};
        struct Return {std::size_t bytes;~Return(){if(bytes)releaseOpenCLProbeBytes(bytes);}} reserved{bytes};
        auto credit=std::make_shared<Credit>();credit->syntheticDevice=tracked;
        auto allocation=std::unique_ptr<OpenCLProbeBatchStorage>(new OpenCLProbeBatchStorage({bytes,device}));
        auto result=std::shared_ptr<OpenCLProbeBatchStorage>(allocation.release(),[credit](auto* value){delete value;});
        result->realCredit=credit;credit->paired=bytes;credit->device=device;*tracked+=device;reserved.bytes=0;
        return result;
    }
    static bool reserve(const std::shared_ptr<OpenCLProbeBatchStorage>& value,unsigned kind,std::size_t bytes) {
        return value->reserve(kind,bytes);
    }
    static bool pair(const std::shared_ptr<OpenCLProbeBatchStorage>& value,std::size_t bytes){return value->reservePaired(bytes);}
    static void release(const std::shared_ptr<OpenCLProbeBatchStorage>& value,unsigned kind,std::size_t bytes) {
        value->release(kind,bytes);
    }
    static std::shared_ptr<PrechargedProbeStorage> claim(const std::shared_ptr<OpenCLProbeBatchStorage>& value,std::size_t bytes) {
        return PrechargedProbeStorage::claim(value,bytes);
    }
};
}

TEST_CASE("future batch admission retains its real promise without release reserve gaps" * doctest::test_suite("OpenCLGradient"))
{
    using namespace gradient_kernel;using Test=OpenCLProbeBatchStorageTests;
    const auto baseline=openCLProbeBytes();auto device=std::make_shared<std::size_t>(0);
    auto future=Test::create(65536,8192,device);REQUIRE(bool(future));
    CHECK(openCLProbeBytes()==baseline+65536);CHECK(*device==8192);
    REQUIRE(Test::pair(future,4096));REQUIRE(Test::reserve(future,2,2048));
    CHECK(openCLProbeBytes()==baseline+65536);CHECK(future->claimedHostBytes()==future->metadataBytes()+4096);
    // Holding an actual payload alias keeps both its allowance and global
    // promise alive even after the enrolling group gives up its alias.
    struct Payload {
        std::shared_ptr<OpenCLProbeBatchStorage> owner;
        ~Payload(){Test::release(owner,2,2048);Test::release(owner,1,4096);Test::release(owner,0,4096);}
    };
    auto payload=std::make_unique<Payload>();payload->owner=future;
    std::weak_ptr<OpenCLProbeBatchStorage> escaped=future;future.reset();
    CHECK_FALSE(escaped.expired());CHECK(openCLProbeBytes()==baseline+65536);
    payload.reset();CHECK(escaped.expired());CHECK(openCLProbeBytes()==baseline+65536);CHECK(*device==8192);
    escaped.reset();CHECK(openCLProbeBytes()==baseline);CHECK(*device==0);
}

TEST_CASE("future batch paired claims roll back when one local ledger is full" * doctest::test_suite("OpenCLGradient"))
{
    using namespace gradient_kernel;using Test=OpenCLProbeBatchStorageTests;
    const auto baseline=openCLProbeBytes();auto device=std::make_shared<std::size_t>(0);
    auto future=Test::create(4096,128,device);REQUIRE(bool(future));
    const auto fixed=future->metadataBytes();const auto free=4096-fixed;
    REQUIRE(Test::reserve(future,1,free));CHECK_FALSE(Test::pair(future,1));
    CHECK(future->claimedHostBytes()==fixed);CHECK(future->claimedProbeBytes()==4096);
    CHECK_FALSE(Test::reserve(future,2,129));CHECK(future->claimedDeviceBytes()==0);
    Test::release(future,1,free);REQUIRE(Test::pair(future,free));
    CHECK_FALSE(Test::pair(future,1));CHECK(openCLProbeBytes()==baseline+4096);
    Test::release(future,1,free);Test::release(future,0,free);future.reset();
    CHECK(openCLProbeBytes()==baseline);CHECK(*device==0);
}

TEST_CASE("future local storage credits survive escaped weak native metadata" * doctest::test_suite("OpenCLGradient"))
{
    using namespace gradient_kernel;using Test=OpenCLProbeBatchStorageTests;
    const auto baseline=openCLProbeBytes();auto device=std::make_shared<std::size_t>(0);
    auto future=Test::create(65536,512,device);REQUIRE(bool(future));
    auto credit=Test::claim(future,8192);REQUIRE(bool(credit));
    std::weak_ptr<PrechargedProbeStorage> escaped=credit;
    CHECK(future->claimedHostBytes()==future->metadataBytes()+8192);
    credit.reset();CHECK(escaped.expired());
    CHECK(future->claimedHostBytes()>future->metadataBytes());
    CHECK(future->claimedHostBytes()<future->metadataBytes()+8192);
    CHECK(future->claimedHostBytes()==future->claimedProbeBytes());
    future.reset();CHECK(openCLProbeBytes()==baseline+65536);CHECK(*device==512);
    escaped.reset();CHECK(openCLProbeBytes()==baseline);CHECK(*device==0);
}

TEST_CASE("complete batch storage estimates permit real capacity downshift before cloning" * doctest::test_suite("OpenCLGradient"))
{
    using namespace gradient_kernel;
    const auto eight=OpenCLProbeBatchStorage::estimate({512,512},8,Plan::Frozen8);
    // Unsupported targets decline rather than fabricate initialized GPU storage.
#if defined(SDL_PLATFORM_EMSCRIPTEN) || defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    CHECK_FALSE(bool(eight));
#else
    REQUIRE(bool(eight));CHECK(eight.hostBytes>=8ull*512*512*2);
    CHECK(eight.deviceBytes==8ull*512*512*4+8ull*32*32*8+292);
    CHECK(eight.hostBytes+8ull*512*512*4<OpenCLProbeBudget);
    const auto four=OpenCLProbeBatchStorage::estimate({1024,1024},4,Plan::Frozen8);
    REQUIRE(bool(four));CHECK(four.hostBytes+4ull*1024*1024*4>OpenCLProbeBudget);
    const auto two=OpenCLProbeBatchStorage::estimate({1024,1024},2,Plan::Frozen8);
    REQUIRE(bool(two));CHECK(two.hostBytes+2ull*1024*1024*4<OpenCLProbeBudget);
    const auto huge=OpenCLProbeBatchStorage::estimate({2048,2048},1,Plan::Frozen8);
    REQUIRE(bool(huge));CHECK(huge.hostBytes+2048ull*2048*4>OpenCLProbeBudget);
    CHECK_FALSE(bool(OpenCLProbeBatchStorage::estimate({512,512},9,Plan::Frozen8)));
    CHECK_FALSE(bool(OpenCLProbeBatchStorage::estimate({512,512},1,Plan::CPU)));
    CHECK_FALSE(bool(OpenCLProbeBatchStorage::estimate({512,512},1,Plan::Frozen8,OpenCLProbeBudget)));
#endif
}
