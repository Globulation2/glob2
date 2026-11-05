// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace GAGCore {
// Downsample tightly packed four-byte pixels with alpha in byte three.
// Source and destination may be the same buffer.
void downsampleMip(const unsigned char *source, int width, int height,
                   unsigned char *destination);
}
