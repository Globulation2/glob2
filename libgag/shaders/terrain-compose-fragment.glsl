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
// CPU reference: TerrainVisual::shoreCalm and applyShoreCalm in
// src/render/terrain/TerrainMaterials.cpp. surfPlace.w packs the cell's corner
// calm; surfColor[slot].w packs a liquid's strength and mean colour.
uint terrainCalmAt(ivec2 pixel) {
    uint corners = uint(surfPlace.w);
    if (corners == 0u) return 0u;
    uint fx = (2u * uint(pixel.x) + 1u) * 128u / params.x;
    uint fy = (2u * uint(pixel.y) + 1u) * 128u / params.x;
    return ((corners & 255u) * (256u - fx) * (256u - fy) +
        ((corners >> 8u) & 255u) * fx * (256u - fy) +
        ((corners >> 16u) & 255u) * (256u - fx) * fy +
        (corners >> 24u) * fx * fy + 32768u) >> 16u;
}
uvec3 terrainCalm(uint slot, ivec2 pixel, uvec3 rgb) {
    uint packedMean = uint(surfColor[slot].w), strength = packedMean >> 24u;
    if (strength == 0u) return rgb;
    uint calm = terrainCalmAt(pixel);
    if (calm == 0u) return rgb;
    uint keep = 256u - calm * strength / 255u;
    uvec3 mean = uvec3((packedMean >> 16u) & 255u, (packedMean >> 8u) & 255u, packedMean & 255u);
    return (rgb * keep + mean * (256u - keep) + 128u) / 256u;
}
int terrainSurfBump(int u) {
    int x = u < 512 ? u : 1024 - u;
    return x * x * (1536 - 2 * x) / 524288;
}
uvec3 terrainSurfMix(uvec3 value, int packedColor, int amount) {
    uvec3 surfColor = uvec3((uint(packedColor) >> 16u) & 255u,
        (uint(packedColor) >> 8u) & 255u, uint(packedColor) & 255u);
    return (value * uint(256 - amount) + surfColor * uint(amount)) / 256u;
}
// CPU reference: TerrainVisual::applySurf in src/render/terrain/TerrainMaterials.cpp.
uvec3 terrainSurf(uint slot, bool surfSide, int margin, ivec2 world, uvec3 value) {
    ivec4 shape = surfShape[slot], band = surfBand[slot], colors = surfColor[slot];
    if (margin >= (surfSide ? max(band.y, shape.w) : max(band.w, shape.y + shape.w))) return value;
    uint time = uint(surfPlace.z) - uint(surfPlace.z) % uint(colors.z);
    int phase = int(time % uint(shape.x) * 1024u / uint(shape.x));
    int offset = terrainSurfBump((world.x >> 7) & 1023) + terrainSurfBump((world.y >> 7) & 1023) +
        terrainSurfBump(((world.x + world.y) >> 6) & 1023);
    int u = (phase + offset) & 1023;
    int rise = u < 320 ? u * 512 / 320 : 512 + (u - 320) * 512 / 704;
    int run = shape.y * terrainSurfBump(rise) / 256;
    value = min(value, uvec3(255u));
    int front;
    if (surfSide) {
        if (margin < band.y)
            value = terrainSurfMix(value, colors.x, band.x * (band.y - margin) / band.y);
        front = margin + run;
    } else {
        if (margin < band.w)
            value = value * uint(256 - band.z * (band.w - margin) / band.w) / 256u;
        if (margin < run)
            value = terrainSurfMix(value, colors.x, band.x * (run - margin) / (2 * run));
        front = margin < run ? run - margin : margin - run;
    }
    if (front < shape.w) {
        int breakup = 96 + (terrainSurfBump(((world.x * 5) >> 7) & 1023) +
            terrainSurfBump((((world.y * 7) >> 7) + phase) & 1023)) * 160 / 512;
        value = terrainSurfMix(value, colors.y, shape.z * (shape.w - front) / shape.w * breakup / 256);
    }
    return value;
}
void main() {
    ivec2 pixel = clamp(ivec2(uv * float(params.x)), ivec2(0), ivec2(int(params.x)-1));
    if ((params.w & 1u) == 0u) {
        uvec4 bytes = terrainMaterial(0u, pixel);
        bytes.rgb = terrainCalm(0u, pixel, bytes.rgb);
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
        source.rgb = terrainCalm(slot, pixel, source.rgb);
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
    bool surfSelf = surfShape[dominant].x > 0, surfOther = surfShape[neighbor].x > 0;
    if (surfSelf != surfOther) {
        // Q8 world pixels of the sample centre, as the coverage samples it.
        int size = int(params.x);
        ivec2 world = surfPlace.xy + (2 * pixel + 1) * 4096 / size;
        value = terrainSurf(surfSelf ? dominant : neighbor, surfSelf, margin, world, value);
    }
    uint packedColor = (((alpha + 32768u) / 65536u) << 24u) |
        (value.r << 16u) | (value.g << 8u) | value.b;
    color = terrainUnpack(packedColor);
}
