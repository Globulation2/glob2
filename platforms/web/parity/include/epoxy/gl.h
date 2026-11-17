// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only: the browser build of torus-render-benchmark (cmake/web.cmake,
// web-render-parity) includes <epoxy/gl.h>; Emscripten provides GL itself.
#pragma once
#include <GL/gl.h>
#include <GL/glext.h>
