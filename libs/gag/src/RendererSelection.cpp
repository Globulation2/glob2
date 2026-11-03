// SPDX-License-Identifier: GPL-3.0-or-later
#include <RendererSelection.h>

namespace GAGCore
{
std::string_view rendererBackendName(RendererBackend backend)
{
    switch (backend) {
    case RendererBackend::Auto: return "auto";
    case RendererBackend::SDLGPU: return "sdl-gpu";
    case RendererBackend::ProgrammableGL: return "programmable-gl";
    case RendererBackend::WebGPU: return "webgpu";
    case RendererBackend::PortableSDL: return "portable-sdl";
    case RendererBackend::Software: return "software";
    case RendererBackend::LegacyGL: return "legacy-gl";
    }
    return "auto";
}
std::string_view rendererBackendLabel(RendererBackend backend)
{
    switch (backend) {
    case RendererBackend::Auto: return "Automatic";
    case RendererBackend::SDLGPU: return "SDL GPU";
    case RendererBackend::ProgrammableGL: return "Programmable GL";
    case RendererBackend::WebGPU: return "WebGPU";
    case RendererBackend::PortableSDL: return "Portable SDL";
    case RendererBackend::Software: return "Software";
    case RendererBackend::LegacyGL: return "Legacy OpenGL";
    }
    return "Automatic";
}
std::optional<RendererBackend> parseRendererBackend(std::string_view name)
{
    for (auto backend : RendererBackends)
        if (rendererBackendName(backend) == name) return backend;
    // Preserve command-line and browser aliases during migration.
    if (name == "gpu" || name == "webgl2" || name == "opengl") return RendererBackend::LegacyGL;
    if (name == "sdl") return RendererBackend::PortableSDL;
    return std::nullopt;
}
RendererBackend resolveRendererPreference(RendererBackend saved,
    std::optional<RendererBackend> launch, std::optional<RendererBackend> environment)
{
    return launch.value_or(environment.value_or(saved));
}
}
