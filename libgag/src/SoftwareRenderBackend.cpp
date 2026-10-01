// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderBackend.h>
namespace GAGCore
{
std::unique_ptr<RenderBackend> makeSDLSoftwareGeometryBackend(SDL_Surface *surface);
// Structural extraction: keep SDL's triangle rasterizer until primitive optimization.
std::unique_ptr<RenderBackend> makeSoftwareRenderBackend(SDL_Surface *surface)
{
	return makeSDLSoftwareGeometryBackend(surface);
}
}
