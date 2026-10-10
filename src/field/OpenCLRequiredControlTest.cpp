// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "field/OpenCLGradient.h"
#include "field/OpenCLRequiredControl.h"

TEST_CASE("required cancellation owns bounded metadata and honors monotonic deadlines" * doctest::test_suite("OpenCLRequiredControl"))
{
    using namespace gradient_kernel;
    const auto before=openCLStatus().hostBytes;
    auto control=makeOpenCLRequiredControl();REQUIRE(bool(control));
    CHECK(openCLStatus().hostBytes>before);
    CHECK_FALSE(control->shouldCancel(0));
    control->setDeadlineNs(100);
    CHECK_FALSE(control->shouldCancel(99));CHECK(control->shouldCancel(100));
    control->setDeadlineNs(0);CHECK_FALSE(control->shouldCancel(100));
    control->requestCancel();CHECK(control->shouldCancel(0));
    control->setDeadlineNs(1000);CHECK(control->shouldCancel(1));
    control.reset();CHECK(openCLStatus().hostBytes==before);
}
