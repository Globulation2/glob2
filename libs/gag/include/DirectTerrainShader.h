// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace GAGCore
{
inline constexpr const char *DirectTerrainFragment = R"(
#ifdef DIRECT_TERRAIN_PACKED
in vec2 world;
out vec4 terrainColor;
#else
varying vec2 world;
#define terrainColor gl_FragColor
#endif
uniform sampler2D cells, coverageIndex, art, coverage, edges, variants, discovery, shoreCalm;
uniform vec2 mapSize, artSize, coverageSize, variantSize;
uniform float animationTime;
uniform bool wholeMap;
#ifdef DIRECT_TERRAIN_ANIMATION
uniform float artTilePixels;
#endif

// The offline generator supplies its own interface above this shared shader.
#ifndef DIRECT_TERRAIN_SAMPLE
#ifdef DIRECT_TERRAIN_PACKED
#define DIRECT_TERRAIN_SAMPLE(image, uv) texture(image, uv)
#define DIRECT_TERRAIN_FETCH(image, xy) texelFetch(image, xy, 0)
#else
#define DIRECT_TERRAIN_SAMPLE(image, uv) texture2D(image, uv)
#endif
#endif

#ifndef DIRECT_TERRAIN_ANIMATION
const float artTilePixels = 32.0;
#endif

vec2 atlasUV(float index, vec2 pixel, vec2 size) {
    float columns = size.x / artTilePixels;
    vec2 base = vec2(mod(index, columns), floor(index / columns)) * artTilePixels;
    return (base + pixel * (artTilePixels / 32.0)) / size;
}

vec2 maskAtlasUV(float index, vec2 pixel) {
    float columns = coverageSize.x / 16.0;
    vec2 base = vec2(mod(index, columns), floor(index / columns)) * 16.0;
    vec2 samplePixel = clamp(pixel * 0.5 - 0.25, vec2(0.5), vec2(15.5));
    return (base + samplePixel) / coverageSize;
}

#ifdef DIRECT_TERRAIN_PACKED
vec4 decodeRecord(vec4 low, vec4 high) {
    return floor(low * 255.0 + 0.5) + floor(high * 255.0 + 0.5) * 256.0;
}
#define RECORD_COORD(slot) (ivec2(slot) * ivec2(2, 1))
#define readRecord(image, slot, size) decodeRecord(DIRECT_TERRAIN_FETCH(image, RECORD_COORD(slot)), DIRECT_TERRAIN_FETCH(image, RECORD_COORD(slot) + ivec2(1, 0)))

float decodeScalar(vec4 packedBytes, int column) {
    vec4 bytes = floor(packedBytes * 255.0 + 0.5);
    vec2 value = (column % 2 == 0) ? bytes.rg : bytes.ba;
    return value.x + value.y * 256.0;
}
#define SCALAR_COORD(uv, size) ivec2(floor((uv) * (size)))
#define readScalar(image, uv, size) decodeScalar(DIRECT_TERRAIN_FETCH(image, SCALAR_COORD(uv, size) / ivec2(2, 1)), SCALAR_COORD(uv, size).x)
#else
#define readRecord(image, slot, size) floor(DIRECT_TERRAIN_SAMPLE(image, ((slot) + 0.5) / (size)) * 65535.0 + 0.5)
#define readScalar(image, uv, size) floor(DIRECT_TERRAIN_SAMPLE(image, uv).r * 65535.0 + 0.5)
#endif

// Edge records pack a Q8 margin with dominant/neighbor selectors, so they
// cannot be filtered by the sampler.
vec3 edgeTexel(float index, vec2 texel) {
    float columns = coverageSize.x / 16.0;
    vec2 base = vec2(mod(index, columns), floor(index / columns)) * 16.0;
    float packedEdge = readScalar(edges, (base + texel + 0.5) / coverageSize, coverageSize);
    float selectors = floor(packedEdge / 4096.0);
    return vec3(mod(packedEdge, 4096.0), mod(selectors, 4.0), floor(selectors / 4.0));
}

// Margin relative to the reference pair: positive on its dominant side,
// negative where the same two materials meet the other way round.
float signedMargin(vec3 sampleEdge, vec3 reference) {
    if (sampleEdge.y == reference.y && sampleEdge.z == reference.z)
        return sampleEdge.x;
    if (sampleEdge.y == reference.z && sampleEdge.z == reference.y)
        return -sampleEdge.x;
    return reference.x;
}

// The mask stores every second compositor sample. Nearest edge records
// would step the surf bands in two-texel blocks, so interpolate the margin
// bilinearly as a signed distance across the boundary, keeping the nearest
// record's material pair (swapped where the interpolated side flips). The
// outer half texel of a tile extrapolates linearly, so margins meet the
// neighboring cell's instead of flattening into a step at the border.
vec3 edgeAt(float index, vec2 pixel) {
    vec2 p = pixel * 0.5 - 0.75;
    vec2 low = clamp(floor(p), vec2(0.0), vec2(14.0));
    vec2 f = p - low;
    vec2 high = low + 1.0;
    vec3 e00 = edgeTexel(index, low);
    vec3 e10 = edgeTexel(index, vec2(high.x, low.y));
    vec3 e01 = edgeTexel(index, vec2(low.x, high.y));
    vec3 e11 = edgeTexel(index, high);
    vec3 nearest = f.y < 0.5 ? (f.x < 0.5 ? e00 : e10) : (f.x < 0.5 ? e01 : e11);
    if (nearest.y == nearest.z)
        return nearest;
    float s = mix(mix(signedMargin(e00, nearest), signedMargin(e10, nearest), f.x),
                  mix(signedMargin(e01, nearest), signedMargin(e11, nearest), f.x), f.y);
    return s >= 0.0 ? vec3(s, nearest.yz) : vec3(-s, nearest.zy);
}

vec4 variantRecord(float index, float field) {
    float address = index * 8.0 + field;
    vec2 slot = vec2(mod(address, variantSize.x), floor(address / variantSize.x));
    return readRecord(variants, slot, variantSize);
}

vec4 material(float index, vec2 uv, float calm) {
    vec4 data = variantRecord(index, 0.0);
    float stepTime = mod(animationTime, data.z);
    float phase = mod(floor(animationTime / data.z), data.y);
    vec4 color = DIRECT_TERRAIN_SAMPLE(art, atlasUV(data.x + phase, uv, artSize));
    // Blend premultiplied endpoints so transparent artwork cannot tint a fade.
    color.rgb *= color.a;
    if (data.w > 0.0) {
        float blend = floor(stepTime * data.w / data.z) / data.w;
        vec4 nextColor = DIRECT_TERRAIN_SAMPLE(art, atlasUV(data.x + mod(phase + 1.0, data.y), uv, artSize));
        nextColor.rgb *= nextColor.a;
        color = mix(color, nextColor, blend);
    }
    if (calm > 0.0) {
        vec4 quiet = variantRecord(index, 7.0);
        color.rgb = mix(color.rgb, quiet.rgb * (color.a / 255.0), calm * quiet.w / 256.0);
    }
    return color;
}

vec3 contact(vec4 indices, vec3 edge, vec3 color) {
    int dominant = int(edge.y);
    int neighbor = int(edge.z);
    if (dominant == neighbor)
        return color;
    vec4 adjacent = variantRecord(indices[neighbor], 5.0);
    float selfHeight = variantRecord(indices[dominant], 5.0).x;
    float margin = edge.x;
    if (adjacent.x > selfHeight && margin < adjacent.z)
        color *= 1.0 - adjacent.y * (adjacent.z - margin) / (256.0 * adjacent.z);
    vec4 fringe = variantRecord(indices[neighbor], 6.0);
    if (margin < fringe.w)
        color = mix(color, fringe.rgb / 255.0, adjacent.w * (fringe.w - margin) / (256.0 * fringe.w));
    return color;
}

// Authored shoreline bands. Coverage supplies the same dominant/neighbor
// selectors and Q8 margin used by the reference compositor.
float bump(float u) {
    float x = min(u, 1024.0 - u);
    return floor(x * x * (1536.0 - 2.0 * x) / 524288.0);
}

vec3 surf(vec4 indices, vec2 uv, vec3 edge, vec3 color) {
    int dominant = int(edge.y);
    int neighbor = int(edge.z);
    if (dominant == neighbor)
        return color;
    vec4 shape = variantRecord(indices[dominant], 1.0);
    vec4 other = variantRecord(indices[neighbor], 1.0);
    bool liquidSide = shape.x > 0.0;
    if (liquidSide == (other.x > 0.0))
        return color;
    float index = liquidSide ? indices[dominant] : indices[neighbor];
    if (!liquidSide)
        shape = other;
    vec4 band = variantRecord(index, 2.0);
    float margin = edge.x;
    if (margin >= (liquidSide ? max(band.y, shape.w) : max(band.w, shape.y + shape.w)))
        return color;
    vec4 shallow = variantRecord(index, 3.0);
    vec3 foam = variantRecord(index, 4.0).rgb / 255.0;
    float time = animationTime - mod(animationTime, shallow.w);
    float phase = floor(mod(time, shape.x) * 1024.0 / shape.x);
    vec2 position = mod(floor(world), mapSize) * 8192.0 + floor(uv) * 256.0 + 128.0;
    float offset = bump(mod(floor(position.x / 128.0), 1024.0)) +
                   bump(mod(floor(position.y / 128.0), 1024.0)) +
                   bump(mod(floor((position.x + position.y) / 64.0), 1024.0));
    float u = mod(phase + offset, 1024.0);
    float rise = floor(u < 224.0 ? u * 512.0 / 224.0 : 512.0 + (u - 224.0) * 512.0 / 800.0);
    float run = floor(shape.y * bump(rise) / 256.0);
    float front;
    if (liquidSide) {
        if (margin < band.y)
            color = mix(color, shallow.rgb / 255.0, band.x * (band.y - margin) / (256.0 * band.y));
        front = margin + run;
    } else {
        if (margin < band.w)
            color *= 1.0 - band.z * (band.w - margin) / (256.0 * band.w);
        if (margin < run)
            color = mix(color, shallow.rgb / 255.0, band.x * (run - margin) / (512.0 * run));
        front = abs(margin - run);
    }
    if (front < shape.w) {
        float breakup = 96.0 + (bump(mod(floor(position.x * 5.0 / 128.0), 1024.0)) +
            bump(mod(floor(position.y * 7.0 / 128.0) + phase, 1024.0))) * 160.0 / 512.0;
        color = mix(color, foam, shape.z * (shape.w - front) / shape.w * breakup / 65536.0);
    }
    return color;
}

void main() {
    vec2 slot = mod(floor(world), mapSize);
    vec2 cell = (slot + 0.5) / mapSize;
    if (!wholeMap) {
#ifdef DIRECT_TERRAIN_PACKED
        ivec2 pixel = ivec2(slot);
        if (DIRECT_TERRAIN_FETCH(discovery, pixel / ivec2(4, 1))[pixel.x % 4] < 0.5)
            discard;
#else
        if (DIRECT_TERRAIN_SAMPLE(discovery, cell).r < 0.5)
            discard;
#endif
    }
    vec4 indices = readRecord(cells, slot, mapSize);
    float halfTexel = 16.0 / artTilePixels;
    vec2 uv = clamp(fract(world) * 32.0, vec2(halfTexel), vec2(32.0 - halfTexel));
    float mask = readScalar(coverageIndex, cell, mapSize);
    if (mask >= 65535.0) discard;
    vec4 quiet = DIRECT_TERRAIN_SAMPLE(shoreCalm, cell);
    vec2 position = uv / 32.0;
    float calm = mix(mix(quiet.x, quiet.y, position.x), mix(quiet.z, quiet.w, position.x), position.y);
    vec4 color = material(indices.x, uv, calm);
    if (mask > 0.0) {
        vec4 weights = DIRECT_TERRAIN_SAMPLE(coverage, maskAtlasUV(mask - 1.0, uv));
        weights /= max(dot(weights, vec4(1.0)), 0.00001);
        color *= weights.x;
        if (weights.y > 0.0) color += weights.y * material(indices.y, uv, calm);
        if (weights.z > 0.0) color += weights.z * material(indices.z, uv, calm);
        if (weights.w > 0.0) color += weights.w * material(indices.w, uv, calm);
        vec3 edge = edgeAt(mask - 1.0, uv);
        if (color.a > 0.0)
            color.rgb = surf(indices, uv, edge, contact(indices, edge, color.rgb / color.a)) * color.a;
    }
    terrainColor = color;
}
)";
}
