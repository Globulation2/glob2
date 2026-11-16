// SPDX-License-Identifier: GPL-3.0-or-later
// Team colour: the CPU recolour of a sprite's team layer, per texel.
// CPU reference: DrawableSurface::shiftHSV (via Sprite::applyTeamHueShift),
// RGBtoHSV/HSVtoRGB in SupportFunctions.cpp and Color::setHSV's truncation,
// in IEEE single precision. Each texel is recoloured before filtering, as the
// CPU recolours its copy before uploading it. hue: degrees, flat from the
// vertex. premultiplied: 0 for straight texels and output; 1 for straight
// texels premultiplied after recolouring, exactly as an upload for linear
// sampling would; 2 for premultiplied texels (lossy below full alpha).
// linear: bilinear filtering, trilinear across `levels`.
//
// Sector and truncation boundaries make the result discontinuous, so it must
// follow the CPU's float operations exactly. Addition, subtraction and
// multiplication are correctly rounded (GLSL ES 3.00 highp, Vulkan, WebGPU);
// division is not, so teamDivide divides with integers and rounds to nearest
// even. A product feeding an addition is multiplied by `one`, a uniform the
// compiler cannot fold: contracting that into a fused multiply-add rounds
// exactly as the separate operations do.

// Correctly rounded a / b for normal finite floats, b != 0.
float teamDivide(float a, float b) {
    if (a == 0.0) return 0.0;
    bool negative = (a < 0.0) != (b < 0.0);
    uint ia = floatBitsToUint(abs(a)), ib = floatBitsToUint(abs(b));
    int exponent = int(ia >> 23) - int(ib >> 23);
    uint ma = (ia & 0x7FFFFFu) | 0x800000u, mb = (ib & 0x7FFFFFu) | 0x800000u;
    if (ma < mb) { ma <<= 1; exponent -= 1; }
    // One integer bit and 24 fraction bits, 8 at a time: the last is the guard bit.
    uint quotient = ma / mb, remainder = ma % mb;
    for (int step = 0; step < 3; ++step) {
        remainder <<= 8;
        quotient = (quotient << 8) | (remainder / mb);
        remainder %= mb;
    }
    uint guard = quotient & 1u;
    quotient >>= 1;
    if (guard != 0u && (remainder != 0u || (quotient & 1u) != 0u)) quotient += 1u;
    if (quotient == 0x1000000u) { quotient >>= 1; exponent += 1; }
    float result = uintBitsToFloat(uint(exponent + 127) << 23 | (quotient & 0x7FFFFFu));
    return negative ? -result : result;
}
vec3 teamHSV(vec3 c, float one) {
    float mx = max(max(c.r, c.g), c.b);
    float mn = min(min(c.r, c.g), c.b);
    float delta = mx - mn;
    // Black and grey keep s == 0, which HSVtoRGB returns unchanged.
    if (mx == 0.0 || delta == 0.0) return vec3(0.0, 0.0, mx);
    float h;
    if (c.r == mx) h = teamDivide(c.g - c.b, delta);
    else if (c.g == mx) h = 2.0 + teamDivide(c.b - c.r, delta);
    else h = 4.0 + teamDivide(c.r - c.g, delta);
    h = h * 60.0 * one;
    if (h < 0.0) h += 360.0;
    return vec3(h, teamDivide(delta, mx), mx);
}
vec3 teamRGB(vec3 hsv, float one) {
    float s = hsv.y, v = hsv.z;
    if (s == 0.0) return vec3(v);
    float h = teamDivide(hsv.x, 60.0);
    float i = floor(h);
    float f = h - i;
    float p = v * (1.0 - s);
    float q = v * (1.0 - s * f * one);
    float t = v * (1.0 - s * (1.0 - f) * one);
    if (i == 0.0) return vec3(v, t, p);
    if (i == 1.0) return vec3(q, v, p);
    if (i == 2.0) return vec3(p, v, t);
    if (i == 3.0) return vec3(p, q, v);
    if (i == 4.0) return vec3(t, p, v);
    return vec3(v, p, q);
}
vec4 teamTexel(vec4 texel, float one) {
    // Recover the 8-bit values the CPU recolours.
    uvec4 stored = uvec4(floor(texel * 255.0 + 0.5));
    // Premultiplied output of a transparent texel is zero whatever its colour.
    if (premultiplied > 0.5 && stored.a == 0u) return vec4(0.0);
    uvec3 straight = stored.rgb;
    // Lossy below full alpha: the upload rounded (c * a + 127) / 255.
    if (premultiplied > 1.5) straight = min((stored.rgb * 255u + stored.a / 2u) / stored.a, uvec3(255u));
    vec3 c = vec3(teamDivide(float(straight.r), 255.0), teamDivide(float(straight.g), 255.0),
                  teamDivide(float(straight.b), 255.0));
    vec3 hsv = teamHSV(c, one);
    hsv.x = hsv.x * one + hue;
    if (hsv.x >= 360.0) hsv.x -= 360.0;
    if (hsv.x < 0.0) hsv.x += 360.0;
    uvec3 shifted = uvec3(floor(255.0 * teamRGB(hsv, one)));
    // The upload's premultiplication, in integers.
    if (premultiplied > 0.5) shifted = (shifted * stored.a + 127u) / 255u;
    return vec4(vec3(shifted), float(stored.a)) / 255.0;
}
vec4 teamBilinear(int level, vec2 coordinate, float one) {
    ivec2 size = textureSize(image, level);
    ivec2 last = size - 1;
    vec2 position = coordinate * vec2(size) - 0.5;
    ivec2 corner = ivec2(floor(position));
    vec2 f = position - floor(position);
    vec4 a = teamTexel(texelFetch(image, clamp(corner, ivec2(0), last), level), one);
    vec4 b = teamTexel(texelFetch(image, clamp(corner + ivec2(1, 0), ivec2(0), last), level), one);
    vec4 c = teamTexel(texelFetch(image, clamp(corner + ivec2(0, 1), ivec2(0), last), level), one);
    vec4 d = teamTexel(texelFetch(image, clamp(corner + ivec2(1, 1), ivec2(0), last), level), one);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
void main() {
    // levels >= 1, which the compiler cannot know.
    float one = min(levels, 1.0);
    vec2 coordinate = clamp(uv, uvBounds.xy, uvBounds.zw);
    vec2 size = vec2(textureSize(image, 0));
    vec2 dx = dFdx(uv) * size, dy = dFdy(uv) * size;
    vec4 texel;
    if (linear < 0.5) {
        ivec2 texelSize = textureSize(image, 0);
        texel = teamTexel(texelFetch(image, clamp(ivec2(floor(coordinate * size)), ivec2(0), texelSize - 1), 0), one);
    } else {
        float lod = clamp(0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1e-8)), 0.0, levels - 1.0);
        float level = floor(lod);
        texel = teamBilinear(int(level), coordinate, one);
        if (lod > level) texel = mix(texel, teamBilinear(int(level) + 1, coordinate, one), lod - level);
    }
    color = tint * texel;
    color.a *= alpha;
}
