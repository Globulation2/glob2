// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveWorkerRig.h"
#include "Glob2Test.h"
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>

namespace {
using Json = nlohmann::json;
Json fixture() {
    Json samples = Json::array();
    const std::array<std::array<unsigned, 3>, 4> paths{
        {{4, 5, 6}, {1, 2, 3}, {7, 9, 11}, {8, 10, 12}}};
    const double h = 1 / std::sqrt(2.);
    const std::array<std::array<double, 3>, 4> directions{
        {{h, h, 0}, {-h, h, 0}, {h, -h, 0}, {-h, -h, 0}}};
    for (unsigned f = 0; f < 64; ++f) {
        Json controls = Json::array();
        for (int i = 0; i < 13; ++i)
            controls.push_back(Json::array({0., 0., 0., 1., 0., 0., 0., 1.}));
        for (unsigned b = 0; b < 4; ++b)
            for (int k = 0; k < 3; ++k)
                for (int axis = 0; axis < 3; ++axis)
                    controls[paths[b][k]][axis] = directions[b][axis] * (3 + k * 2);
        samples.push_back(controls);
    }
    return {{"version", 1},
            {"model", "worker"},
            {"clip", "walk"},
            {"logicalSize", 38},
            {"controls", 13},
            {"phases", 64},
            {"cycleSourceFrames", 16},
            {"definition",
             {{"bodyAxes", {3, 1, 2}},
              {"bodyRadiusScale", 1.15},
              {"minimumSectionRadiusScale", .65},
              {"ringsPerSegment", 3},
              {"socketDivisions", 4},
              {"paths", paths}}},
            {"descriptors", Json::array({Json::array({"body", Json::array({1., 0., 0.})}),
                                         Json::array({"body", Json::array({0., 1., 0.})}),
                                         Json::array({"body", Json::array({0., 0., 1.})})})},
            {"uv", {{0., 0.}, {1., 0.}, {0., 1.}}},
            {"regions", {-1, -1, -1}},
            {"indices", {0, 1, 2}},
            {"radii", std::array<double, 13>{3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3}},
            {"stiffness", std::array<double, 13>{2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}},
            {"threshold", .6},
            {"pivot", {0., 0., 0.}},
            {"headingOffsets", std::array<std::array<double, 3>, 8>{}},
            {"modelToClip", {.1, 0., 0., 0., 0., .1, 0., 0., 0., 0., .01, 0., 0., 0., 0., 1.}},
            {"normalToCamera", {1., 0., 0., 0., 1., 0., 0., 0., 1.}},
            {"samples", samples}};
}
} // namespace
TEST_SUITE("LiveWorkerRig") {
    TEST_CASE("loader rejects malformed candidates and preserves a loaded rig") {
        auto valid = fixture();
        LiveWorkerRig rig;
        std::string error;
        REQUIRE(rig.loadJson(valid.dump(), error));
        auto pose = rig.evaluate(0, 0);
        for (unsigned kind = 0; kind < 9; ++kind) {
            auto bad = valid;
            switch (kind) {
            case 0:
                bad["version"] = 9;
                break;
            case 1:
                bad["samples"].erase(bad["samples"].begin());
                break;
            case 2:
                bad["indices"][0] = -1;
                break;
            case 3:
                bad["samples"][0][0][7] = 0;
                break;
            case 4:
                bad["uv"][0][0] = 2;
                break;
            case 5:
                bad["samples"][0][0][3] = 0;
                break;
            case 6:
                bad["definition"]["paths"][0][0] = 0;
                break;
            case 7:
                bad["threshold"] = 2;
                break;
            case 8:
                bad["regions"][0] = 4;
                break;
            }
            CHECK_FALSE(rig.loadJson(bad.dump(), error));
            CHECK_FALSE(error.empty());
            CHECK(rig.evaluate(0, 0) == pose);
        }
        CHECK_FALSE(rig.loadJson("{", error));
        CHECK_FALSE(rig.loadJson(std::string(1024 * 1024 + 1, ' '), error));
    }
    TEST_CASE("periodic keys preserve identity and eviction remains bounded") {
        LiveWorkerRig rig;
        std::string error;
        REQUIRE(rig.loadJson(fixture().dump(), error));
        auto first = rig.evaluate(0, 0);
        CHECK(first == rig.evaluate(1, 2 * std::numbers::pi));
        CHECK(first == rig.evaluate(-1, -2 * std::numbers::pi));
        for (unsigned i = 0; i < 200; ++i) {
            auto p = rig.evaluate(i / 200., .125);
            for (float v : p->poses)
                CHECK(std::isfinite(v));
        }
        CHECK(rig.geometryBytes() <= 128 * 108);
        CHECK(first->identity != rig.evaluate(0, 0)->identity);
        CHECK_THROWS(rig.evaluate(std::numeric_limits<double>::infinity(), 0));
        CHECK_THROWS(rig.evaluate(0, std::numeric_limits<double>::quiet_NaN()));
    }
    TEST_CASE("fractional translations interpolate without changing topology or normals") {
        auto data = fixture();
        for (int i = 0; i < 13; ++i)
            data["samples"][1][i][0] = data["samples"][1][i][0].get<double>() + .1;
        LiveWorkerRig rig;
        std::string error;
        REQUIRE(rig.loadJson(data.dump(), error));
        auto base = rig.evaluate(0, 0), mid = rig.evaluate(.5 / 64, 0),
             end = rig.evaluate(1. / 64, 0);
        CHECK(base->uv == mid->uv);
        CHECK(base->indices == mid->indices);
        for (unsigned v = 0; v < 3; ++v) {
            CHECK(mid->poses[v * 6] ==
                  doctest::Approx((base->poses[v * 6] + end->poses[v * 6]) * .5).epsilon(1e-5));
            for (int k = 3; k < 6; ++k)
                CHECK(mid->poses[v * 6 + k] ==
                      doctest::Approx(base->poses[v * 6 + k]).epsilon(1e-5));
        }
    }
    TEST_CASE("heading rotates model-space geometry continuously") {
        LiveWorkerRig rig;
        std::string error;
        REQUIRE(rig.loadJson(fixture().dump(), error));
        auto a = rig.evaluate(0, 0), b = rig.evaluate(0, std::numbers::pi / 2);
        for (unsigned v = 0; v < 3; ++v) {
            CHECK(b->poses[v * 6] == doctest::Approx(-a->poses[v * 6 + 1]).epsilon(1e-5));
            CHECK(b->poses[v * 6 + 1] == doctest::Approx(a->poses[v * 6]).epsilon(1e-5));
        }
        auto c = rig.evaluate(0, .123), d = rig.evaluate(0, .123001);
        for (unsigned i = 0; i < c->poses.size(); ++i)
            CHECK(std::abs(c->poses[i] - d->poses[i]) < .00001);
    }
}
