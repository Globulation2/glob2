// SPDX-License-Identifier: GPL-3.0-or-later
// Torus geometry supplies canonical clip depth 0..w and camera-space normals.
void main() {
    uv = texcoord * uvTransform.xy + uvTransform.zw;
    light = vertexColor;
    normal = surfaceNormal;
    gl_Position = projection * position;
    RENDER_CANONICAL_DEPTH();
}
