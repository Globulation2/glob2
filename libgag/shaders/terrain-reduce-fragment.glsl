// SPDX-License-Identifier: GPL-3.0-or-later
// CPU reference: src/render/SoftwareTerrainCache.cpp, reduceTile.
// params: divisor (bits 0..15) and final PMA flag (bit 16), source origin X/Y,
// source height for GL framebuffer Y flip
// (zero for native/WebGPU top-left textures). uv spans the output tile.
void main() {
    uint divisor = params.x & 65535u;
    ivec2 outputSize = textureSize(rawImage, 0) / int(divisor);
    ivec2 local = clamp(ivec2(uv * vec2(outputSize)), ivec2(0), outputSize - 1);
    ivec2 origin = ivec2(params.yz) + local * int(divisor);
    uint alpha = 0u;
    uvec3 rgb = uvec3(0u);
    for (uint y = 0u; y < 32u; ++y) {
        if (y >= divisor) break;
        for (uint x = 0u; x < 32u; ++x) {
            if (x >= divisor) break;
            ivec2 coordinate = origin + ivec2(x, y);
            if (params.w != 0u) coordinate.y = int(params.w) - 1 - coordinate.y;
            uvec4 source = uvec4(floor(texelFetch(rawImage, coordinate, 0) * 255.0 + 0.5));
            alpha += source.a;
            rgb += source.rgb * source.a;
        }
    }
    uint count = divisor * divisor;
    uvec3 reduced = alpha == 0u ? uvec3(0u) : (rgb + alpha / 2u) / alpha;
    uint reducedAlpha = (alpha + count / 2u) / count;
    if ((params.x & 65536u) != 0u) reduced = (reduced * reducedAlpha + 127u) / 255u;
    color = vec4(reduced, reducedAlpha) / 255.0;
}
