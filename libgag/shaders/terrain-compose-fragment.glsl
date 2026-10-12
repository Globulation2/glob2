// SPDX-License-Identifier: GPL-3.0-or-later
// CPU reference: src/render/terrain/TerrainCompositor.cpp, Compositor::compose.
// Declarations are generated from the renderer manifest. params contains
// composition size, palette count, present-material bits, and flags:
// bit 0 coverage present, bit 1 final premultiplied output requested.
uvec4 terrainBytes(vec4 value) {
    return uvec4(floor(value * 255.0 + 0.5));
}
uvec4 terrainMaterial(uint slot, ivec2 pixel) {
    if ((params.z & (1u << slot)) == 0u) return uvec4(0u);
    // Explicit branches keep combined texture bindings portable across dialects.
    if (slot == 0u) return terrainBytes(texelFetch(material0,
        pixel * textureSize(material0, 0) / int(params.x), 0));
    if (slot == 1u) return terrainBytes(texelFetch(material1,
        pixel * textureSize(material1, 0) / int(params.x), 0));
    if (slot == 2u) return terrainBytes(texelFetch(material2,
        pixel * textureSize(material2, 0) / int(params.x), 0));
    return terrainBytes(texelFetch(material3,
        pixel * textureSize(material3, 0) / int(params.x), 0));
}
vec4 terrainUnpack(uint packedColor) {
    uvec4 bytes = uvec4((packedColor >> 16u) & 255u, (packedColor >> 8u) & 255u,
        packedColor & 255u, packedColor >> 24u);
    if ((params.w & 2u) != 0u) bytes.rgb = (bytes.rgb * bytes.a + 127u) / 255u;
    return vec4(bytes) / 255.0;
}
void main() {
    ivec2 pixel = clamp(ivec2(uv * float(params.x)), ivec2(0), ivec2(int(params.x)-1));
    if ((params.w & 1u) == 0u) {
        uvec4 bytes = terrainMaterial(0u, pixel);
        if ((params.w & 2u) != 0u) bytes.rgb = (bytes.rgb * bytes.a + 127u) / 255u;
        color = vec4(bytes) / 255.0;
        return;
    }
    uvec4 coverage = texelFetch(coverageWeights, pixel, 0);
    uint selectors = texelFetch(coverageSelectors, pixel, 0).r;
    uint largest = selectors & 3u;
    uint weights[4];
    uint remaining = 65536u, otherIndex = 0u;
    for (uint slot = 0u; slot < 4u; ++slot) {
        if (slot != largest) {
            weights[slot] = coverage[otherIndex++];
            remaining -= weights[slot];
        }
    }
    weights[largest] = remaining;
    uvec3 rgb = uvec3(0u);
    uint alpha = 0u;
    for (uint slot = 0u; slot < 4u; ++slot) {
        if (slot >= params.y) break;
        uvec4 source = terrainMaterial(slot, pixel);
        uint contribution = weights[slot] * source.a;
        alpha += contribution;
        rgb += source.rgb * contribution;
    }
    uint dominant = (selectors >> 2u) & 3u;
    uint neighbor = (selectors >> 4u) & 3u;
    ivec4 self = seamGeometry[dominant], adjacent = seamGeometry[neighbor];
    ivec4 fringe = seamFringe[neighbor];
    int margin = int(coverage.w), shade = 256, tintAmount = 0;
    if (neighbor != dominant) {
        if (adjacent.y != 0 && adjacent.x > self.x && margin < adjacent.z)
            shade = 256 - adjacent.y * (adjacent.z - margin) / adjacent.z;
        if (adjacent.w != 0 && margin < fringe.x)
            tintAmount = adjacent.w * (fringe.x - margin) / fringe.x;
    }
    uvec3 blended = alpha == 0u ? uvec3(0u) : rgb / alpha;
    uvec3 value = (blended * uint(shade)) >> 8u;
    // Preserve unsigned subtraction/multiplication wrapping and packed-ARGB OR:
    // legacy fringe overflow may carry into neighboring channels or alpha.
    value += (uvec3(fringe.yzw) - value) * uint(tintAmount) / 256u;
    uint packedColor = (((alpha + 32768u) / 65536u) << 24u) |
        (value.r << 16u) | (value.g << 8u) | value.b;
    color = terrainUnpack(packedColor);
}
