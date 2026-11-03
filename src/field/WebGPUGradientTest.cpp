// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit browser hardware coverage; selected separately from portable CPU tests.
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
#include "Glob2Test.h"
#include "GradientDeviceService.h"
#include "GradientAccelerators.h"
#include "GradientPropagation.h"
#include "GradientOracle.h"
#include "fixtures/gradients/RetainedVulkanSeeds.h"
#include <chrono>
#include <thread>

TEST_CASE("owned worker bridge commits exact retained fields and recovers canceled admission" *
          doctest::test_suite("WebGPUGradient"))
{
    using namespace gradient_kernel;
    auto service = std::make_shared<GradientDeviceService>();
    service->configure(2, Backend::WebGPU);
    struct Drain { ~Drain() { shutdownGradientDeviceCoordinator(); } } drain;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!service->metrics().ready && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE_MESSAGE(service->metrics().ready, webGPUGradientStatus().error);
    MESSAGE("WebGPU C++ device: ", webGPUGradientStatus().device);
    ComputeExecutor executor;
    executor.configure(2);
    REQUIRE(executor.threadCount() == 2);
    const field::Grid grid{gradient_fixture::retainedVulkanWidth, gradient_fixture::retainedVulkanHeight};
    const auto costs = std::make_shared<const std::vector<EntrySteps>>(grid.cells(), LAND_STEPS);
    auto session = std::make_shared<BackendSession>();
    const std::array<std::span<const std::uint16_t>, 5> fixtures{
        gradient_fixture::retainedVulkanRuns0, gradient_fixture::retainedVulkanRuns1,
        gradient_fixture::retainedVulkanRuns2, gradient_fixture::retainedVulkanRuns3,
        gradient_fixture::retainedVulkanRuns4};
    for (bool destinations : {false,true}) for (auto runs : fixtures) {
        std::vector<std::uint16_t> seeds;
        for (auto run : runs) seeds.insert(seeds.end(), run >> 2, (run & 3) == 2 ? 65535 : run & 3);
        REQUIRE(seeds.size() == grid.cells());
        auto expected = seeds;
        GradientWorkspace scratch;
        propagateFieldCPUVariant<false>(expected.data(), 0, gradient_fixture::retainedVulkanCap,
                                        grid, scratch, [](std::size_t) { return false; });
        OwnedDestinations expectedDestinations(grid.cells());
        if (destinations) {
            auto labeled=seeds;
            propagateFieldCPU(labeled.data(),expectedDestinations.planes(),0,gradient_fixture::retainedVulkanCap,
                grid,scratch,[](std::size_t){return false;});
            REQUIRE(labeled==expected);
        }
        for (auto algorithm : {Algorithm::Frozen8, Algorithm::Jacobi})
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            auto field = std::make_shared<OwnedGradientField>();
            field->session = session;
            field->decision = {selectedPlan(Backend::WebGPU, algorithm), session->currentGeneration(),
                               {Backend::WebGPU, algorithm}};
            field->grid = grid;
            field->limit = gradient_fixture::retainedVulkanCap;
            field->inputs = costs;
            field->data = std::make_unique<std::uint16_t[]>(grid.cells());
            if (destinations) field->destinations=std::make_shared<OwnedDestinations>(grid.cells());
            std::copy(seeds.begin(), seeds.end(), field->data.get());
            field->costAt = [](const auto&, std::size_t) { return LAND_STEPS; };
            field->cpu = [](auto& value) {
                GradientWorkspace scratch;
                if (value.destinations)
                    propagateFieldCPU(value.data.get(),value.destinations->planes(),0,value.limit,value.grid,
                        scratch,[](std::size_t){return false;});
                else propagateFieldCPUVariant<false>(value.data.get(), 0, value.limit, value.grid,
                                                scratch, [](std::size_t) { return false; });
            };
            // The canceled iteration must finish on the submitting worker, with
            // original seeds. Every other iteration requires an actual GPU commit.
            const bool canceled = repeat == 1 && algorithm == Algorithm::Jacobi;
            if (canceled) field->requiredControl->requestCancel();
            struct Context {
                ComputeExecutor& executor;
                GradientDeviceService& service;
                std::shared_ptr<OwnedGradientField> field;
                bool admitted = false, recoveredInline = false;
            } context{executor, *service, field};
            const ComputeExecutor::Group group{1, {[](void* value, std::size_t) {
                auto& c = *static_cast<Context*>(value);
                c.field->completion = c.executor.defer();
                c.admitted = c.service.submit(c.field);
                if (!c.admitted) {
                    c.service.recoverOnWorker(c.field);
                    c.recoveredInline = c.field->phase.load() == OwnedGradientField::Phase::Complete;
                }
            }, &context}};
            auto ticket = executor.submit(std::span(&group, 1));
            executor.join(ticket);
            REQUIRE_FALSE(field->error);
            CHECK(context.admitted == !canceled);
            CHECK(context.recoveredInline == canceled);
            CHECK(field->executedGPU == !canceled);
            CHECK(field->deviceExecutionObserved == !canceled);
            const std::span<const std::uint16_t> actual(field->data.get(), grid.cells());
            CHECK(std::equal(actual.begin(), actual.end(), expected.begin()));
            if (destinations) {
                CHECK(std::equal(field->destinations->coordinates.get(),field->destinations->coordinates.get()+grid.cells(),expectedDestinations.coordinates.get()));
            }
            CHECK(exactGradientFixedPoint(std::span<const std::uint16_t>(seeds), actual,
                  grid, field->limit, [](std::size_t) { return LAND_STEPS; }));
        }
    }
    CHECK(service->metrics().executed == 30);
    CHECK(service->metrics().fallbacks == 10);
    // Cancel the completed first member from the second member's cost callback.
    // This deterministically exercises the aggregate commit gate on a real worker.
    for (auto plan : {Plan::WebGPUFrozen8, Plan::WebGPUJacobi}) {
        const field::Grid small{17,19};
        std::vector<std::uint16_t> seeds(small.cells(),1); seeds[0]=65535;
        auto first=seeds, second=seeds;
        OwnedDestinations firstXY(small.cells()), secondXY(small.cells());
        for (auto* xy : {&firstXY,&secondXY}) {
            std::fill_n(xy->coordinates.get(),small.cells(),gradient_kernel::DestinationPlanes::pack(123,456));
        }
        auto control=std::make_shared<OpenCLRequiredControl>();
        std::array<BackendRequest,2> requests{
            BackendRequest{first.data(),COST_LIMIT,small,*session,nullptr,
                [](void*,auto){return LAND_STEPS;},nullptr},
            BackendRequest{second.data(),COST_LIMIT,small,*session,control.get(),
                [](void* p,auto){static_cast<OpenCLRequiredControl*>(p)->requestCancel();return LAND_STEPS;},nullptr}};
        requests[0].requiredControl=control;
        requests[0].destinations=firstXY.planes(); requests[1].destinations=secondXY.planes();
        std::array<bool,2> committed{}, observed{};
        for (unsigned i=0;i<2;++i) {
            requests[i].executedOnDevice=&committed[i];
            requests[i].deviceExecutionObserved=&observed[i];
        }
        struct Batch { std::span<const BackendRequest> requests; Plan plan; bool result=true; } batch{requests,plan};
        const ComputeExecutor::Group group{1,{[](void* p,std::size_t){
            auto& batch=*static_cast<Batch*>(p);
            batch.result=executeWebGPUGradient(batch.requests,batch.plan);
        },&batch}};
        const auto fields=webGPUGradientStatus().fields;
        auto batchTicket=executor.submit(std::span(&group,1));
        executor.join(batchTicket);
        CHECK_FALSE(batch.result);
        CHECK(observed[0]); CHECK(observed[1]);
        CHECK_FALSE(committed[0]); CHECK_FALSE(committed[1]);
        CHECK(first==seeds); CHECK(second==seeds);
        for (auto* xy : {&firstXY,&secondXY}) {
            CHECK(std::all_of(xy->coordinates.get(),xy->coordinates.get()+small.cells(),[](auto v){return v==gradient_kernel::DestinationPlanes::pack(123,456);}));
        }
        CHECK(webGPUGradientStatus().fields==fields);
    }
    service->stop();
}
#endif
