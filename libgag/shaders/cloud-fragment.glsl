// SPDX-License-Identifier: GPL-3.0-or-later
void main() {
    vec4 cloud = texture(world, uv);
    color = vec4(cloud.rgb * light, cloud.a);
}
