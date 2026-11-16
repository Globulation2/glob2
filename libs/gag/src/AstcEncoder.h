// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vector>

// ASTC 4x4 LDR blocks through the vendored astcenc (third_party/README.md).
// Asset workers call these; they never run on the render thread.
namespace GAGCore::AstcEncoder
{
//! 16 bytes for each 4x4 texels of tightly packed RGBA8 (red first), in row
//! order; width and height must be multiples of 4. Empty if the encoder fails.
std::vector<unsigned char> encode(const unsigned char *rgba, int width, int height);
//! The RGBA8 texels an ASTC LDR decoder produces from encode()'s blocks.
std::vector<unsigned char> decode(const unsigned char *blocks, int width, int height);
} // namespace GAGCore::AstcEncoder
