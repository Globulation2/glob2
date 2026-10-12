// SPDX-License-Identifier: GPL-3.0-or-later
// Portable circular coverage for the torus renderer's particle quads.
void main() {
    float radius = length(uv * 2.0 - 1.0);
    float coverage = 1.0 - smoothstep(1.0 - clamp(fwidth(radius), 0.01, 0.5), 1.0, radius);
    color = vec4(tint.rgb, tint.a * alpha * coverage);
}
