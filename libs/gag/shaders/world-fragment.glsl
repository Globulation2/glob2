// SPDX-License-Identifier: GPL-3.0-or-later
// Shared world lighting preserves the torus renderer's warm specular response.
void main() {
    vec3 highlight = vec3(1.0, 0.96, 0.85) *
        pow(max(dot(normalize(normal), sunSpecular.xyz), 0.0), 36.0) * sunSpecular.w;
    color = vec4(texture(world, uv).rgb * light + highlight, 1.0);
}
