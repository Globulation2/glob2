// Shared with the native game; checked by test_skin_shader_parity.py.
export const SKIN_MATERIAL_GLSL = `
// BEGIN skin-material
float skinHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float skinNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 s = f * f * (3.0 - 2.0 * f);
  return mix(mix(skinHash(i), skinHash(i + vec2(1.0, 0.0)), s.x),
             mix(skinHash(i + vec2(0.0, 1.0)), skinHash(i + vec2(1.0, 1.0)), s.x), s.y);
}
// The original glob material bumps its normals with Stucci noise (norfac 5).
float skinStucci(vec2 p) { return skinNoise(p) + 0.5 * skinNoise(p * 2.03 + 17.0); }
vec3 skinBump(vec3 n, vec2 uv, float amount, float frequency) {
  float e = 0.25 / frequency;
  float h = skinStucci(uv * frequency);
  float gu = (skinStucci((uv + vec2(e, 0.0)) * frequency) - h) / e;
  float gv = (skinStucci((uv + vec2(0.0, e)) * frequency) - h) / e;
  // Express the UV height gradient in screen directions, independent of resolution.
  vec2 du = vec2(dFdx(uv.x), dFdy(uv.x));
  vec2 dv = vec2(dFdx(uv.y), dFdy(uv.y));
  float density = max(0.5 * (length(du) + length(dv)), 1e-6);
  vec2 g = amount * (gu * du + gv * dv) / density;
  // Stretched UV regions exaggerate the gradient; keep the tilt bounded.
  g *= min(1.0, 0.7 / max(length(g), 1e-6));
  return normalize(n - vec3(g, 0.0));
}
// Material ids come from the skin's material map: 0 classic glossy,
// 1 matte, 2 metallic, 3 hairy (shell fur adds strands in extra passes).
vec3 skinShade(vec3 albedo, float material, vec3 surfaceNormal, vec2 uv) {
  vec3 n = normalize(surfaceNormal);
  vec3 l = normalize(vec3(-0.4, 0.7, 1.0));
  vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
  if (material < 0.5) {
    // The original glob material: Stucci-bumped body and broad white streaks
    // (specular 0.5, hardness 2), lit like the classic sprites.
    vec3 b = skinBump(n, uv, 0.08, 12.0);
    float nh = max(0.0, dot(b, h));
    return albedo * (0.24 + 0.66 * max(0.0, dot(b, l))) + vec3(0.42 * pow(nh, 4.0));
  }
  if (material < 1.5) {
    return albedo * (0.45 + 0.55 * max(0.0, dot(n, l)));
  }
  if (material < 2.5) {
    // Metal: albedo-tinted reflection of a sky/ground gradient, a tight
    // highlight and a brightening rim.
    vec3 b = skinBump(n, uv, 0.015, 20.0);
    float facing = max(0.0, b.z);
    float nh = max(0.0, dot(b, h));
    vec3 sky = mix(vec3(0.18), vec3(1.0), smoothstep(-0.6, 0.8, b.y));
    float rim = pow(1.0 - facing, 3.0);
    vec3 tint = mix(albedo, vec3(1.0), 0.3 * rim);
    return tint * (0.12 + 0.2 * max(0.0, dot(b, l)) + 0.75 * sky) + vec3(pow(nh, 40.0));
  }
  // Hairy: soft, wrapped diffuse with a light fringe.
  float wrap = max(0.0, (dot(n, l) + 0.5) / 1.5);
  float fringe = pow(1.0 - max(0.0, n.z), 2.0);
  return albedo * (0.35 + 0.65 * wrap) + albedo * 0.35 * fringe;
}
// END skin-material
`;
