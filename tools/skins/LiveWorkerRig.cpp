// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveWorkerRig.h"
#include <StreamBackend.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <list>
#include <nlohmann/json.hpp>
#include <numbers>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
using V = std::array<double, 3>;
using Q = std::array<double, 4>;
using M = std::array<V, 3>; // row major
constexpr double Pi = std::numbers::pi;
V operator+(V a, V b) {
    for (int i = 0; i < 3; ++i)
        a[i] += b[i];
    return a;
}
V operator-(V a, V b) {
    for (int i = 0; i < 3; ++i)
        a[i] -= b[i];
    return a;
}
V operator*(V a, double s) {
    for (auto &x : a)
        x *= s;
    return a;
}
double dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V cross(V a, V b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
V unit(V a) {
    double n = std::sqrt(dot(a, a));
    if (n < 1e-12)
        throw std::runtime_error("degenerate rig direction");
    return a * (1 / n);
}
V transform(const M &m, V a) { return {dot(m[0], a), dot(m[1], a), dot(m[2], a)}; }
M multiply(const M &a, const M &b) {
    M c{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                c[i][j] += a[i][k] * b[k][j];
    return c;
}
Q slerp(Q a, Q b, double t) {
    double d = 0;
    for (int i = 0; i < 4; ++i)
        d += a[i] * b[i];
    if (d < 0) {
        for (auto &x : b)
            x = -x;
        d = -d;
    }
    d = std::clamp(d, -1., 1.);
    double x = 1 - t, y = t;
    if (d < .9995) {
        double angle = std::acos(d), s = std::sin(angle);
        x = std::sin((1 - t) * angle) / s;
        y = std::sin(t * angle) / s;
    }
    Q q{};
    double n = 0;
    for (int i = 0; i < 4; ++i) {
        q[i] = a[i] * x + b[i] * y;
        n += q[i] * q[i];
    }
    for (auto &v : q)
        v /= std::sqrt(n);
    return q;
}
M rotation(Q q) {
    const auto [w, x, y, z] = q;
    return {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
             {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
             {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}}};
}
double scalar(const Json &j) {
    if (!j.is_number())
        throw std::runtime_error("expected rig number");
    double v = j.get<double>();
    if (!std::isfinite(v) || std::abs(v) > 1e6)
        throw std::runtime_error("invalid rig scalar");
    return v;
}
void count(const Json &j, std::size_t n) {
    if (!j.is_array() || j.size() != n)
        throw std::runtime_error("rig array length mismatch");
}
V vector(const Json &j) {
    count(j, 3);
    return {scalar(j[0]), scalar(j[1]), scalar(j[2])};
}
unsigned index(const Json &j, unsigned bound) {
    if (!j.is_number_integer() || j.get<long long>() < 0 || j.get<unsigned long long>() >= bound)
        throw std::runtime_error("rig index out of bounds");
    return j.get<unsigned>();
}
void word(std::string &bytes, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        bytes.push_back(static_cast<char>(v >> (i * 8)));
}
void number(std::string &bytes, double v) {
    if (!std::isfinite(v))
        throw std::runtime_error("nonfinite evaluated vertex");
    word(bytes, std::bit_cast<std::uint32_t>(static_cast<float>(v)));
}
double wrap(double x, double period) {
    if (!std::isfinite(x))
        throw std::runtime_error("nonfinite pose request");
    double v = std::fmod(x, period);
    if (v < 0)
        v += period;
    return v == 0 ? 0 : v;
}
} // namespace

struct LiveWorkerRig::State {
    struct Control {
        V center;
        Q orientation;
        double scale;
    };
    struct Vertex {
        std::string kind;
        V body{};
        unsigned branch = 0, k = 0;
        double s = 0;
        std::vector<unsigned> average;
    };
    struct Entry {
        double phase, heading;
        std::shared_ptr<const GAGCore::SkinMesh> mesh;
    };
    std::array<std::array<Control, 13>, 64> samples;
    std::array<V, 8> headingOffsets;
    std::array<double, 13> radii, stiffness;
    std::array<std::array<unsigned, 3>, 4> paths;
    std::vector<Vertex> vertices;
    std::vector<std::array<double, 2>> uv;
    std::vector<unsigned> indices;
    std::vector<int> regions;
    std::array<int, 3> axes;
    V pivot;
    std::array<double, 16> projection;
    M camera;
    double threshold, bodyScale, minimumScale;
    unsigned rings, divisions;
    Statistics stats;
    std::list<Entry> cache;

    std::shared_ptr<const GAGCore::SkinMesh> pose(double phase, double heading);
};
LiveWorkerRig::LiveWorkerRig() = default;
LiveWorkerRig::~LiveWorkerRig() = default;
bool LiveWorkerRig::load(const std::string &path, std::string &error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f || f.tellg() < 0 || f.tellg() > 1024 * 1024) {
        error = "cannot read rig or rig exceeds 1 MiB";
        return false;
    }
    std::string bytes(static_cast<std::size_t>(f.tellg()), '\0');
    f.seekg(0);
    if (!f.read(bytes.data(), bytes.size())) {
        error = "truncated rig";
        return false;
    }
    return loadJson(bytes, error);
}
bool LiveWorkerRig::loadJson(const std::string &bytes, std::string &error) {
    try {
        if (bytes.size() > 1024 * 1024)
            throw std::runtime_error("rig exceeds 1 MiB");
        const auto j = Json::parse(bytes);
        if (j.at("version") != 1 || j.at("model") != "worker" || j.at("clip") != "walk" ||
            j.at("logicalSize") != 38 || j.at("controls") != 13 || j.at("phases") != 64 ||
            j.at("cycleSourceFrames") != 16)
            throw std::runtime_error("unsupported live-worker rig");
        auto c = std::make_unique<State>();
        const auto &d = j.at("definition");
        count(d.at("bodyAxes"), 3);
        unsigned axesMask = 0;
        for (int i = 0; i < 3; ++i) {
            int a = d["bodyAxes"][i].get<int>();
            if (!a || std::abs(a) > 3 || (axesMask & (1 << std::abs(a))))
                throw std::runtime_error("invalid body axes");
            axesMask |= 1 << std::abs(a);
            c->axes[i] = a;
        }
        c->bodyScale = scalar(d.at("bodyRadiusScale"));
        c->minimumScale = scalar(d.at("minimumSectionRadiusScale"));
        c->rings = index(d.at("ringsPerSegment"), 9);
        c->divisions = index(d.at("socketDivisions"), 9);
        if (c->rings != 3 || c->divisions != 4 || c->bodyScale <= 0 || c->minimumScale <= 0)
            throw std::runtime_error("unsupported surface dimensions");
        count(d.at("paths"), 4);
        unsigned pathMask = 0;
        for (int b = 0; b < 4; ++b) {
            count(d["paths"][b], 3);
            for (int k = 0; k < 3; ++k) {
                auto v = index(d["paths"][b][k], 13);
                if (!v || (pathMask & (1 << v)))
                    throw std::runtime_error("invalid limb path");
                pathMask |= 1 << v;
                c->paths[b][k] = v;
            }
        }
        c->threshold = scalar(j.at("threshold"));
        if (c->threshold <= 0)
            throw std::runtime_error("invalid field threshold");
        count(j.at("radii"), 13);
        count(j.at("stiffness"), 13);
        for (int i = 0; i < 13; ++i) {
            c->radii[i] = scalar(j["radii"][i]);
            c->stiffness[i] = scalar(j["stiffness"][i]);
            if (c->radii[i] <= 0 || c->stiffness[i] <= c->threshold)
                throw std::runtime_error("invalid field support");
        }
        c->pivot = vector(j.at("pivot"));
        count(j.at("modelToClip"), 16);
        count(j.at("normalToCamera"), 9);
        for (int i = 0; i < 16; ++i)
            c->projection[i] = scalar(j["modelToClip"][i]);
        for (int i = 0; i < 9; ++i)
            c->camera[i / 3][i % 3] = scalar(j["normalToCamera"][i]);
        count(j.at("headingOffsets"), 8);
        for (int d = 0; d < 8; ++d)
            c->headingOffsets[d] = vector(j["headingOffsets"][d]);
        count(j.at("samples"), 64);
        for (int f = 0; f < 64; ++f) {
            count(j["samples"][f], 13);
            for (int i = 0; i < 13; ++i) {
                const auto &a = j["samples"][f][i];
                count(a, 8);
                auto &s = c->samples[f][i];
                s.center = {scalar(a[0]), scalar(a[1]), scalar(a[2])};
                double n = 0;
                for (int k = 0; k < 4; ++k) {
                    s.orientation[k] = scalar(a[k + 3]);
                    n += s.orientation[k] * s.orientation[k];
                }
                if (std::abs(n - 1) > .001)
                    throw std::runtime_error("invalid control quaternion");
                for (auto &q : s.orientation)
                    q /= std::sqrt(n);
                s.scale = scalar(a[7]);
                if (s.scale <= 1e-6 || s.scale > 100)
                    throw std::runtime_error("invalid control scale");
            }
        }
        const auto &descriptors = j.at("descriptors");
        if (!descriptors.is_array() || descriptors.size() < 3 || descriptors.size() > 8192)
            throw std::runtime_error("invalid rig vertex count");
        count(j.at("uv"), descriptors.size());
        count(j.at("regions"), descriptors.size());
        for (unsigned i = 0; i < descriptors.size(); ++i) {
            const auto &a = descriptors[i];
            if (!a.is_array() || a.empty() || !a[0].is_string())
                throw std::runtime_error("invalid vertex descriptor");
            State::Vertex v;
            v.kind = a[0].get<std::string>();
            if (v.kind == "body") {
                count(a, 2);
                v.body = vector(a[1]);
            } else if (v.kind == "average") {
                count(a, 2);
                if (!a[1].is_array() || a[1].size() < 3 || a[1].size() > 8)
                    throw std::runtime_error("invalid surface average");
                for (const auto &x : a[1])
                    v.average.push_back(index(x, i));
            } else if (v.kind == "tip") {
                count(a, 2);
                v.branch = index(a[1], 4);
            } else if (v.kind == "ring" || v.kind == "cap") {
                count(a, 4);
                v.branch = index(a[1], 4);
                v.s = scalar(a[2]);
                v.k = index(a[3], 16);
                if (v.s <= 0 || v.s > (v.kind == "ring" ? 1 : Pi / 2))
                    throw std::runtime_error("invalid ring parameter");
                if (v.kind == "ring" && std::abs(v.s * 12 - std::round(v.s * 12)) > 1e-8)
                    throw std::runtime_error("invalid ring section");
            } else
                throw std::runtime_error("unknown vertex descriptor");
            c->vertices.push_back(v);
            count(j["uv"][i], 2);
            std::array<double, 2> uv{scalar(j["uv"][i][0]), scalar(j["uv"][i][1])};
            if (uv[0] < 0 || uv[0] > 1 || uv[1] < 0 || uv[1] > 1)
                throw std::runtime_error("invalid paint coordinate");
            c->uv.push_back(uv);
            int region = j["regions"][i].get<int>();
            if (region < -1 || region > 3)
                throw std::runtime_error("invalid limb region");
            c->regions.push_back(region);
        }
        const auto &indices = j.at("indices");
        if (!indices.is_array() || indices.empty() || indices.size() > 49152 || indices.size() % 3)
            throw std::runtime_error("invalid topology");
        for (const auto &i : indices)
            c->indices.push_back(index(i, c->vertices.size()));
        // Evaluate once before publishing the candidate; failed reloads retain prior state.
        c->pose(0, 0);
        c->stats = {};
        c->cache.clear();
        state = std::move(c);
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

std::shared_ptr<const GAGCore::SkinMesh> LiveWorkerRig::State::pose(double phase, double heading) {
    const auto started = std::chrono::steady_clock::now();
    unsigned f = static_cast<unsigned>(phase * 64);
    double t = phase * 64 - f;
    M turn{{{std::cos(heading), -std::sin(heading), 0},
            {std::sin(heading), std::cos(heading), 0},
            {0, 0, 1}}};
    std::array<V, 13> centers;
    std::array<M, 13> rotations;
    std::array<double, 13> support, isolated;
    double direction = wrap(-heading, 2 * Pi) / (Pi / 4);
    unsigned d = static_cast<unsigned>(direction);
    double mix = direction - d;
    V offset = headingOffsets[d] * (1 - mix) + headingOffsets[(d + 1) % 8] * mix;
    for (int i = 0; i < 13; ++i) {
        const auto &a = samples[f][i];
        const auto &b = samples[(f + 1) % 64][i];
        centers[i] = transform(turn, a.center * (1 - t) + b.center * t - pivot) + pivot + offset;
        rotations[i] = multiply(turn, rotation(slerp(a.orientation, b.orientation, t)));
        support[i] = radii[i] * (a.scale * (1 - t) + b.scale * t);
        isolated[i] = support[i] * std::sqrt(1 - std::cbrt(threshold / stiffness[i]));
    }
    M basis{};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            basis[i][j] = rotations[0][i][std::abs(axes[j]) - 1] * (axes[j] > 0 ? 1 : -1);
    const double h = 1 / std::sqrt(2.);
    M cube = multiply(basis, M{{{1, 0, 0}, {0, h, -h}, {0, h, h}}});
    double bodyRadius = isolated[0] * bodyScale;
    struct Section {
        V center, tangent, front, lateral;
    };
    struct Branch {
        std::array<Section, 13> sections;
        std::array<V, 16> rim;
        std::array<double, 16> angles;
        std::array<std::array<V, 16>, 13> solved;
        std::array<bool, 13> ready{};
        V center;
    };
    std::array<Branch, 4> branches;
    const std::array<V, 4> normals{{{0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}}};
    for (int b = 0; b < 4; ++b) {
        auto &br = branches[b];
        V normal = normals[b], side = cross(normal, V{1, 0, 0});
        br.center = centers[0] +
                    transform(cube, normal) * (bodyRadius / std::sqrt(1 + std::pow(2. / 8, 2)));
        std::array<V, 4> controls{br.center, centers[paths[b][0]], centers[paths[b][1]],
                                  centers[paths[b][2]]};
        auto curve = [&](double s) {
            double a = s * 3;
            int i = std::min(static_cast<int>(a), 2);
            a -= i;
            V p0 = controls[i], p1 = controls[i + 1];
            V m0 =
                (controls[std::min(i + 1, 3)] - controls[std::max(i - 1, 0)]) * (i == 0 ? 1. : .5);
            V m1 = (controls[std::min(i + 2, 3)] - controls[i]) * (i + 1 == 3 ? 1. : .5);
            V c = p0 * (2 * a * a * a - 3 * a * a + 1) + m0 * (a * a * a - 2 * a * a + a) +
                  p1 * (-2 * a * a * a + 3 * a * a) + m1 * (a * a * a - a * a);
            V tangent = unit(p0 * (6 * a * a - 6 * a) + m0 * (3 * a * a - 4 * a + 1) +
                             p1 * (-6 * a * a + 6 * a) + m1 * (3 * a * a - 2 * a));
            return std::pair(c, tangent);
        };
        V previous = transform(cube, normal), front{basis[0][0], basis[1][0], basis[2][0]};
        for (int sample = 0; sample <= 48; ++sample) {
            auto [c, tangent] = curve(sample / 48.);
            V k = cross(previous, tangent);
            double cosine = dot(previous, tangent);
            if (cosine > -1 + 1e-8)
                front = front + cross(k, front) + cross(k, cross(k, front)) * (1 / (1 + cosine));
            front = unit(front - tangent * dot(front, tangent));
            previous = tangent;
            if (sample % 4 == 0)
                br.sections[sample / 4] = {c, tangent, front, cross(tangent, front)};
        }
        for (int k = 0; k < 16; ++k) {
            int x = 0, y = 0;
            int j = k % 4;
            switch (k / 4) {
            case 0:
                x = 4;
                y = -4 + 2 * j;
                break;
            case 1:
                x = 4 - 2 * j;
                y = 4;
                break;
            case 2:
                x = -4;
                y = 4 - 2 * j;
                break;
            default:
                x = -4 + 2 * j;
                y = -4;
            }
            br.angles[k] = std::atan2(y, x);
            br.rim[k] = centers[0] +
                        transform(cube, unit(normal * 8 +
                                             (V{static_cast<double>(x), 0, 0} + side * y) * .5)) *
                            bodyRadius;
        }
    }
    std::vector<V> positions;
    positions.reserve(vertices.size());
    for (const auto &v : vertices) {
        if (v.kind == "average") {
            V p{};
            for (auto i : v.average)
                p = p + positions[i];
            positions.push_back(p * (1. / v.average.size()));
            continue;
        }
        if (v.kind == "body") {
            positions.push_back(centers[0] + transform(cube, v.body) * (bodyRadius / 1.8));
            continue;
        }
        auto &br = branches[v.branch];
        const auto &path = paths[v.branch];
        double tipRadius = isolated[path[2]];
        if (v.kind == "ring") {
            unsigned section = static_cast<unsigned>(std::round(v.s * 12));
            if (!br.ready[section]) {
                const auto &sec = br.sections[section];
                double a = v.s * 3;
                int segment = std::min(static_cast<int>(a), 2);
                a -= segment;
                std::array<double, 4> necks{bodyRadius * .25, isolated[path[0]], isolated[path[1]],
                                            isolated[path[2]]};
                double minimum = ((1 - a) * necks[segment] + a * necks[segment + 1]) * minimumScale;
                for (int k = 0; k < 16; ++k) {
                    V ray =
                        sec.front * std::cos(br.angles[k]) + sec.lateral * std::sin(br.angles[k]);
                    double lo = 0, hi = 2 * std::max({support[0], support[path[0]],
                                                      support[path[1]], support[path[2]]});
                    for (int step = 0; step < 24; ++step) {
                        double r = (lo + hi) / 2, field = 0;
                        for (auto i : {0u, path[0], path[1], path[2]}) {
                            V delta = sec.center + ray * r - centers[i];
                            double falloff =
                                std::max(0., 1 - dot(delta, delta) / (support[i] * support[i]));
                            field += stiffness[i] * falloff * falloff * falloff;
                        }
                        if (field > threshold)
                            lo = r;
                        else
                            hi = r;
                    }
                    V p = sec.center + ray * std::max((lo + hi) / 2, minimum);
                    double blend = std::min(1., v.s * 3);
                    if (blend < 1)
                        p = (br.rim[k] + sec.center - br.center) * (1 - blend) + p * blend;
                    br.solved[section][k] = p;
                }
                br.ready[section] = true;
            }
            positions.push_back(br.solved[section][v.k]);
        } else {
            const auto &sec = br.sections[12];
            if (v.kind == "tip")
                positions.push_back(sec.center + sec.tangent * tipRadius);
            else
                positions.push_back(sec.center + sec.tangent * (tipRadius * std::sin(v.s)) +
                                    (sec.front * std::cos(br.angles[v.k]) +
                                     sec.lateral * std::sin(br.angles[v.k])) *
                                        (tipRadius * std::cos(v.s)));
        }
    }
    // Same restricted implicit fields as the exporter: body + proximal joints,
    // plus only the vertex's own limb. Unrelated feet cannot pull it sideways.
    auto field = [&](V p, int region) {
        double value = 0;
        V gradient{};
        for (unsigned i = 0; i < 13; ++i) {
            bool allowed = i == 0;
            for (int b = 0; b < 4; ++b)
                allowed = allowed || i == paths[b][0];
            if (region >= 0)
                for (auto k : paths[region])
                    allowed = allowed || i == k;
            if (!allowed)
                continue;
            V delta = p - centers[i];
            double support2 = support[i] * support[i],
                   falloff = std::max(0., 1 - dot(delta, delta) / support2);
            value += stiffness[i] * falloff * falloff * falloff;
            gradient = gradient + delta * (-6 * stiffness[i] * falloff * falloff / support2);
        }
        return std::pair(value, gradient);
    };
    for (int step = 0; step < 10; ++step)
        for (unsigned i = 0; i < positions.size(); ++i) {
            auto [value, gradient] = field(positions[i], regions[i]);
            V delta = gradient * ((value - threshold) / std::max(dot(gradient, gradient), 1e-12));
            double length = std::sqrt(dot(delta, delta));
            positions[i] =
                positions[i] - delta * std::min(1., bodyRadius * .12 / std::max(length, 1e-12));
        }
    std::string bytes = "GSK1";
    word(bytes, vertices.size());
    word(bytes, indices.size());
    word(bytes, 1);
    word(bytes, 38);
    for (auto v : uv) {
        number(bytes, v[0]);
        number(bytes, v[1]);
    }
    for (auto i : indices)
        word(bytes, i);
    for (unsigned i = 0; i < positions.size(); ++i) {
        V p = positions[i];
        for (int row = 0; row < 3; ++row)
            number(bytes, projection[row * 4] * p[0] + projection[row * 4 + 1] * p[1] +
                              projection[row * 4 + 2] * p[2] + projection[row * 4 + 3]);
        auto [value, gradient] = field(p, regions[i]);
        V normal = transform(camera, unit(gradient) * -1);
        for (auto n : normal)
            number(bytes, n);
    }
    auto mesh = std::make_shared<GAGCore::SkinMesh>();
    std::string error;
    GAGCore::MemoryStreamBackend input(bytes.data(), bytes.size());
    if (!mesh->load(input, error))
        throw std::runtime_error("evaluated rig: " + error);
    stats.deformationSeconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return mesh;
}
std::shared_ptr<const GAGCore::SkinMesh> LiveWorkerRig::evaluate(double phase, double heading) {
    if (!state)
        throw std::runtime_error("live-worker rig not loaded");
    phase = wrap(phase, 1);
    heading = wrap(heading, 2 * Pi);
    for (auto i = state->cache.begin(); i != state->cache.end(); ++i)
        if (i->phase == phase && i->heading == heading) {
            auto mesh = i->mesh;
            state->cache.splice(state->cache.begin(), state->cache, i);
            ++state->stats.hits;
            return mesh;
        }
    auto mesh = state->pose(phase, heading);
    ++state->stats.misses;
    state->cache.push_front({phase, heading, mesh});
    if (state->cache.size() > 128)
        state->cache.pop_back();
    return mesh;
}
LiveWorkerRig::Statistics LiveWorkerRig::statistics() const {
    return state ? state->stats : Statistics{};
}
std::size_t LiveWorkerRig::geometryBytes() const {
    std::size_t n = 0;
    if (state)
        for (const auto &e : state->cache)
            n += (e.mesh->uv.capacity() + e.mesh->poses.capacity()) * sizeof(float) +
                 e.mesh->indices.capacity() * sizeof(std::uint32_t);
    return n;
}
void LiveWorkerRig::clearCache() {
    if (state) {
        state->cache.clear();
        state->stats = {};
    }
}
