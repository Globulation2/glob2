// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "field/OpenCLProbeBatchStorage.h"
#include "field/OpenCLChargedStorage.h"
#include "field/AdaptiveGradientPolicy.h"
#include "field/GradientPropagation.h"
#include <SDL3/SDL_platform_defines.h>
#include <thread>
#include <cstring>

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
    static std::shared_ptr<OpenCLProbeBatchStorage> adopt(std::size_t& bytes,OpenCLProbeBatchStorage::Estimate estimate) {
        return OpenCLProbeBatchStorage::adopt(bytes,estimate);
    }
    static std::shared_ptr<PrechargedProbeStorage> capture(std::size_t& bytes){return PrechargedProbeStorage::adopt(bytes);}
    static std::shared_ptr<const PrechargedProbeCostOwner> costs(std::shared_ptr<const void> owner,
        std::shared_ptr<PrechargedProbeStorage> capture,std::shared_ptr<PrechargedProbeStorage> replay,
        std::shared_ptr<OpenCLProbeBatchStorage> future) {
        return PrechargedProbeCostOwner::create(std::move(owner),std::move(capture),std::move(replay),std::move(future));
    }
};
}

TEST_CASE("isolated native batch storage transfers whole promises before real 512 clones" * doctest::test_suite("OpenCLGradient"))
{
    using namespace gradient_kernel;using Test=OpenCLProbeBatchStorageTests;
    const auto* enabled=std::getenv("GLOB2_TEST_OPENCL_BATCH_STORAGE");
    if(!enabled || std::strcmp(enabled,"1"))return;
    if constexpr(!GAGCore::ThreadSupport::available)return;
    std::thread coordinator([] {
        REQUIRE(initializeOpenCL());const auto configured=openCLStatus();
        REQUIRE_FALSE(configured.parityBound);REQUIRE_FALSE(configured.directSeedUpload);REQUIRE(configured.apiCpuMode==0);
        REQUIRE((readyPlans.load()&(1u<<unsigned(Plan::Frozen8)))!=0);
        const auto baseline=openCLProbeBytes(),deviceBaseline=openCLStatus().deviceBytes;
        const auto initializationCpu=openCLStatus().initializationThreadCPUNs;
        constexpr unsigned count=2;const field::Grid grid(512,512);
        struct Owner {
            BackendSession session;
            std::array<std::vector<std::uint16_t>,count> originals,golden;
            std::uint64_t clock=1000;
            static std::uint64_t read(const void* context) noexcept {return ++const_cast<Owner*>(static_cast<const Owner*>(context))->clock;}
        };
        const auto estimate=OpenCLProbeBatchStorage::estimate(grid,count,Plan::Frozen8);REQUIRE(bool(estimate));
        const auto captureBytes=sizeof(Owner)+1024+PrechargedProbeStorage::metadataBytes()+count*grid.cells()*4;
        const auto promised=captureBytes+estimate.hostBytes;REQUIRE(promised<OpenCLProbeBudget);
        REQUIRE(reserveOpenCLProbeBytes(promised));
        struct Return {std::size_t capture=0,future=0;~Return(){releaseOpenCLProbeBytes(capture+future);}} raw{captureBytes,estimate.hostBytes};
        // Tracked-device pressure is synthetic reservation, not driver OOM. It
        // proves rejection preserves the real paired admission before clones.
        REQUIRE(estimate.deviceBytes<OpenCLDeviceBudget-deviceBaseline);
        auto pressure=reserveOpenCLDeviceBytesForDiagnostics(OpenCLDeviceBudget-deviceBaseline-estimate.deviceBytes+1);REQUIRE(bool(pressure));
        CHECK_FALSE(bool(Test::adopt(raw.future,estimate)));CHECK(raw.future==estimate.hostBytes);
        CHECK(openCLProbeBytes()==baseline+promised);pressure.reset();
        auto future=Test::adopt(raw.future,estimate);REQUIRE(bool(future));CHECK(raw.future==0);
        CHECK(openCLStatus().deviceBytes==deviceBaseline+estimate.deviceBytes);
        auto capture=Test::capture(raw.capture);REQUIRE(bool(capture));CHECK(raw.capture==0);
        // All original/golden allocations follow BOTH real admissions.
        auto owner=std::make_shared<Owner>();owner->session.setExternalInitialization(true);
        std::uint64_t acceptedCpu=0;
        for(unsigned field=0;field<count;++field) {
            owner->originals[field].assign(grid.cells(),1);
            for(std::size_t i=0;i<grid.cells();++i)if((i+field)%19==7)owner->originals[field][i]=0;
            owner->originals[field][field]=65535;owner->originals[field].back()=65001;
            owner->golden[field]=owner->originals[field];GradientWorkspace workspace;
            const auto start=glob2::threadCpuNs();
            propagateFieldCPU(owner->golden[field].data(),0,COST_LIMIT,grid,workspace,[](std::size_t){return false;});
            const auto end=glob2::threadCpuNs();REQUIRE(start!=0);REQUIRE(end>=start);acceptedCpu+=end-start;
        }
        REQUIRE(acceptedCpu>0); // Actual eager reference; no timing qualification.
        auto replay=Test::claim(future,4096);REQUIRE(bool(replay));
        auto costs=Test::costs(owner,capture,replay,future);REQUIRE(bool(costs));
        CostIdentity identity{costs,91,21,true,captureBytes+4096,LAND_STEPS.cardinal|(LAND_STEPS.diagonal<<16)};
        identity.prechargedOwner=costs;
        std::vector<OpenCLProbeInput> inputs;
        for(unsigned field=0;field<count;++field) {
            inputs.push_back({owner->originals[field].data(),COST_LIMIT,grid,owner->session,owner.get(),
                [](void*,std::size_t){FAIL("proven scalar batch must not scan costs");return LAND_STEPS;},identity});
            inputs.back().capturedPrivateCosts=true;
        }
        const auto claimed=future->claimedHostBytes();
        auto probe=beginOpenCLProbeBatch(inputs,Plan::Frozen8,owner,{owner,Owner::read},future);REQUIRE(bool(probe));
        auto progress=OpenCLProbeProgress::Pending;bool oneKernel=true;
        for(unsigned attempt=0;attempt<1000000 && progress==OpenCLProbeProgress::Pending;++attempt) {
            const auto before=probe->metrics().dispatches;progress=probe->advance();oneKernel=oneKernel && probe->metrics().dispatches<=before+1;
            if(progress==OpenCLProbeProgress::Pending)std::this_thread::yield();
        }
        REQUIRE(progress==OpenCLProbeProgress::Complete);CHECK(oneKernel);
        CHECK(probe->metrics().batchFields==count);CHECK(probe->metrics().retiredFields==count);
        CHECK(probe->metrics().dispatches>0);CHECK(probe->metrics().invalidCpuClocks==0);
        for(unsigned field=0;field<count;++field) {
            const auto actual=probe->result(field);
            CHECK(std::vector<std::uint16_t>(actual.begin(),actual.end())==owner->golden[field]);
            CHECK(owner->originals[field][field]==65535);CHECK(owner->originals[field].back()==65001);
        }
        CHECK(openCLProbeBytes()==baseline+promised);CHECK(future->claimedDeviceBytes()==estimate.deviceBytes);
        probe.reset();CHECK(future->claimedDeviceBytes()==0);CHECK(future->claimedHostBytes()==claimed);
        inputs.clear();identity={};costs.reset();owner.reset();replay.reset();capture.reset();
        CHECK(openCLProbeBytes()==baseline+estimate.hostBytes);
        std::weak_ptr<OpenCLProbeBatchStorage> escaped=future;future.reset();
        CHECK(escaped.expired());CHECK(openCLProbeBytes()==baseline+estimate.hostBytes);
        CHECK(openCLStatus().deviceBytes==deviceBaseline+estimate.deviceBytes);
        escaped.reset();CHECK(openCLProbeBytes()==baseline);CHECK(openCLStatus().deviceBytes==deviceBaseline);
        CHECK(openCLStatus().initializationThreadCPUNs==initializationCpu);
    });coordinator.join();
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
