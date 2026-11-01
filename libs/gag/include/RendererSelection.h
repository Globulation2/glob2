// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace GAGCore
{
enum class RendererBackend { Auto, SDLGPU, ProgrammableGL, WebGPU, PortableSDL, Software, LegacyGL };
inline constexpr std::array RendererBackends{RendererBackend::Auto, RendererBackend::SDLGPU,
    RendererBackend::ProgrammableGL, RendererBackend::WebGPU, RendererBackend::PortableSDL,
    RendererBackend::Software, RendererBackend::LegacyGL};
std::string_view rendererBackendName(RendererBackend backend);
std::string_view rendererBackendLabel(RendererBackend backend);
std::optional<RendererBackend> parseRendererBackend(std::string_view name);

struct RendererCapabilities
{
    bool accelerated = false;
    bool torus = false;
    bool liveSkins = false;
    bool textureFiltering = false;
    bool asynchronousCapture = false;
    bool alphaMapCellCentres = false;
    bool cloudLayers = false;
    bool textureMipmaps = false;
};
struct RendererAvailability
{
    RendererBackend backend;
    bool available;
    std::string reason;
};
enum class RendererSwitchPhase { Idle, Queued, Preparing, Presenting, Complete, Failed };
struct RendererSwitchStatus
{
    RendererBackend requested = RendererBackend::Auto;
    RendererBackend active = RendererBackend::Software;
    RendererSwitchPhase phase = RendererSwitchPhase::Idle;
    std::string reason;
    unsigned long long serial = 0;
    unsigned long long durationNanoseconds = 0;
};
// Startup overrides are session state, never written to preferences.
RendererBackend resolveRendererPreference(RendererBackend saved,
    std::optional<RendererBackend> launch, std::optional<RendererBackend> environment);
}
