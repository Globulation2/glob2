// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace GAGCore
{
// Identical deformation body for desktop GL and WebGL2. Matrix uniforms are
// column-major uploads of the row-major CPU palette. Do not use the camera's
// independently scaled depth axis to transform normals.
inline constexpr const char *SkinDeformationGLSL = R"GLSL(
uniform mat4 bones[32];
uniform mat4 view;
uniform mat3 normalView;
uniform float shell;
uniform float furLength;
uniform float shellDepth;
vec3 rigNormal(vec3 n) {
    float l = length(n);
    return l < 1e-8 ? vec3(0.0, 0.0, 1.0) : n / l;
}
void main() {
    vec3 p = vec3(0.0);
    vec3 n = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        mat4 b = bones[int(joints[i])];
        p += weights[i] * (b * vec4(position, 1.0)).xyz;
        n += weights[i] * (mat3(b) * surfaceNormal) / dot(b[0].xyz, b[0].xyz);
    }
    // The CPU camera is affine. Do not let roundoff in sum(weights) scale
    // its translation through the blended homogeneous coordinate.
    vec4 projected = view * vec4(p, 1.0);
    uv = texcoord;
    normal = rigNormal(normalView * rigNormal(n));
    gl_Position = vec4(projected.xy / 1.25 + normal.xy * shell * furLength,
                       projected.z - shell * shellDepth, 1.0);
}
)GLSL";
} // namespace GAGCore
