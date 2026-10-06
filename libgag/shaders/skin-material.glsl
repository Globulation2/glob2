// SPDX-License-Identifier: GPL-3.0-or-later
// Colony-skin materials, shared verbatim by the game's tile renderer, the
// server sprite baker and the web studio. Ids, names and the shell count live
// in skin-materials.json; scons/skin_materials.py checks both agree.
//
// Valid as GLSL 1.20 and GLSL ES 3.00: no version line, no texture lookups,
// no loops. Normals are camera space; the camera is orthographic along +Z.
// Mesh UVs are 0..1 per model and fold front/back, so patterns are frequencies
// per UV unit: macro structure at 6-16 cells reads at the 48 px game size,
// micro grain at 48-96 shows only in the studio.
#define SKIN_SHELLS 8

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
// Ridged noise: creases, cracks and fracture lines.
float skinRidge(vec2 p) { return 1.0 - abs(2.0 * skinNoise(p) - 1.0); }

// Tilts n by a UV-space height gradient g = (dh/du, dh/dv). Meshes carry no
// tangents, so the gradient is expressed in screen directions with derivatives,
// independent of resolution; stretched UV regions keep a bounded tilt.
vec3 skinTilt(vec3 n, vec2 uv, vec2 g, float amount) {
  vec2 du = vec2(dFdx(uv.x), dFdy(uv.x));
  vec2 dv = vec2(dFdx(uv.y), dFdy(uv.y));
  float density = max(0.5 * (length(du) + length(dv)), 1e-6);
  vec2 s = amount * (g.x * du + g.y * dv) / density;
  s *= min(1.0, 0.7 / max(length(s), 1e-6));
  return normalize(n - vec3(s, 0.0));
}
// Central-difference gradient of a height function over UV. One line: GLSL
// 1.20 has no line continuation.
#define SKIN_GRADIENT(height, p, e) (vec2(height((p) + vec2(e, 0.0)) - height((p) - vec2(e, 0.0)), height((p) + vec2(0.0, e)) - height((p) - vec2(0.0, e))) / (2.0 * (e)))
float skinStucciHeight(vec2 p) { return skinStucci(p); }
vec3 skinBump(vec3 n, vec2 uv, float amount, float frequency) {
  return skinTilt(n, uv, SKIN_GRADIENT(skinStucciHeight, uv * frequency, 0.05) * frequency, amount);
}

// Dome of a disc: height 0..1 inside, -1 outside.
float skinDisc(vec2 p, vec2 centre, float radius) {
  float d = length(p - centre) / radius;
  return d < 1.0 ? sqrt(1.0 - d * d) : -1.0;
}
// Imbricated discs on staggered unit rows: (height, cell id) of the topmost
// disc covering p. Rows further up overlap the row below, like fish scales.
vec2 skinScales(vec2 p, float radius) {
  float row = floor(p.y);
  float shift = 0.5 * mod(row, 2.0);
  vec2 c0 = vec2(floor(p.x - shift) + 0.5 + shift, row + 0.5);
  float shiftUp = 0.5 * mod(row + 1.0, 2.0);
  vec2 c1 = vec2(floor(p.x - shiftUp) + 0.5 + shiftUp, row + 1.5);
  vec2 c2 = c1 + vec2(c1.x > p.x ? -1.0 : 1.0, 0.0);
  float h1 = skinDisc(p, c1, radius);
  if (h1 >= 0.0) return vec2(h1, skinHash(c1));
  float h2 = skinDisc(p, c2, radius);
  if (h2 >= 0.0) return vec2(h2, skinHash(c2));
  float h0 = skinDisc(p, c0, radius);
  return vec2(max(h0, 0.0), skinHash(c0));
}
// Hexagonal cells of unit width: (distance to the nearest edge 0..0.5, cell id).
vec3 skinHex(vec2 p) {
  vec2 r = vec2(1.0, 1.7320508);
  vec2 h = r * 0.5;
  vec2 a = mod(p, r) - h;
  vec2 b = mod(p - h, r) - h;
  vec2 g = dot(a, a) < dot(b, b) ? a : b;
  vec2 q = abs(g);
  float edge = 0.5 - max(dot(q, vec2(0.5, 0.8660254)), q.x);
  vec2 id = p - g;
  return vec3(edge, id);
}

// What a material leaves for the shared lighting: a perturbed normal and the
// response of the surface. Materials only fill this in; skinLight does the rest.
struct SkinSurface {
  vec3 albedo;
  vec3 n;
  float rough;    // 0 mirror .. 1 chalk
  float spec;     // dielectric highlight and reflection strength
  float metal;    // reflections take the albedo's colour, diffuse fades out
  float wrap;     // diffuse wraps past the terminator: soft, translucent
  float rim;      // albedo-coloured fringe along the silhouette
  float cel;      // >0: that many diffuse bands, hard highlight, inked edge
  vec3 emissive;
  float alpha;    // below 0.5 on a shell pass: no strand here
};
SkinSurface skinDefault(vec3 albedo, vec3 n) {
  SkinSurface s;
  s.albedo = albedo;
  s.n = n;
  s.rough = 1.0;
  s.spec = 0.0;
  s.metal = 0.0;
  s.wrap = 0.0;
  s.rim = 0.0;
  s.cel = 0.0;
  s.emissive = vec3(0.0);
  s.alpha = 1.0;
  return s;
}
// One light from the upper left as the classic sprites, a hemisphere ambient,
// a reflected sky/ground gradient and a normalised Blinn-Phong highlight.
vec3 skinLight(SkinSurface s) {
  vec3 n = normalize(s.n);
  vec3 l = normalize(vec3(-0.4, 0.7, 1.0));
  vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
  float facing = max(0.0, n.z);
  float nl = dot(n, l);
  float diffuse = max(0.0, (nl + s.wrap) / (1.0 + s.wrap));
  float gloss = 1.0 - s.rough;
  float power = exp2(2.0 + 9.0 * gloss);
  float highlight = pow(max(0.0, dot(n, h)), power) * (0.5 + 0.9 * gloss) * smoothstep(-0.2, 0.2, nl);
  float fresnel = pow(1.0 - facing, 4.0);
  // The sky reflected by the view direction: bright from above, dark ground.
  float ry = 2.0 * n.z * n.y;
  vec3 sky = mix(vec3(0.26, 0.25, 0.28), vec3(1.0), smoothstep(-0.5, 0.9, ry));
  float reflectivity = mix(s.spec * gloss * (0.06 + 0.6 * fresnel), 0.6 + 0.3 * fresnel, s.metal);
  vec3 tint = mix(vec3(1.0), mix(s.albedo, vec3(1.0), 0.3 * fresnel), s.metal);
  if (s.cel > 0.0) {
    diffuse = min(1.0, floor(diffuse * s.cel) / max(1.0, s.cel - 1.0));
    highlight = step(0.5, highlight);
    reflectivity = 0.0;
  }
  float ambient = 0.26 + 0.08 * n.y;
  vec3 color = s.albedo * (1.0 - 0.6 * s.metal) * (ambient + (1.0 - ambient) * diffuse);
  color += tint * sky * reflectivity;
  color += tint * max(s.spec, s.metal) * highlight;
  color += s.albedo * s.rim * pow(1.0 - facing, 2.0);
  color += s.emissive;
  if (s.cel > 0.0 && facing < 0.3) color *= 0.25;
  return color;
}

// --- Materials. Each fills the surface from the albedo, mesh UV and the fur
// shell fraction (0 on the body, k/SKIN_SHELLS on shell k). ---------------

// 0: the original glob, a soft bumped plastic with broad white streaks.
void skinMaterial_glossy(inout SkinSurface s, vec2 uv, float shell) {
  s.n = skinBump(s.n, uv, 0.012, 12.0);
  s.n = skinBump(s.n, uv, 0.003, 48.0);
  s.rough = 0.7;
  s.spec = 0.6;
  s.wrap = 0.15;
}
// 1: soft clay with a fine grain.
void skinMaterial_matte(inout SkinSurface s, vec2 uv, float shell) {
  s.albedo *= 0.94 + 0.12 * skinNoise(uv * 80.0);
  s.n = skinBump(s.n, uv, 0.004, 60.0);
  s.wrap = 0.35;
}
// 2: riveted metal panels with recessed seams.
float skinPanelHeight(vec2 g) {
  vec2 f = fract(g);
  vec2 edge = min(f, 1.0 - f);
  float seam = min(edge.x, edge.y);
  vec2 d = edge - 0.14;
  float r = length(d) / 0.085;
  float rivet = r < 1.0 ? sqrt(1.0 - r * r) : 0.0;
  return 0.5 * smoothstep(0.0, 0.05, seam) + 0.5 * rivet;
}
void skinMaterial_metallic(inout SkinSurface s, vec2 uv, float shell) {
  vec2 g = uv * vec2(5.0, 6.0);
  vec2 f = fract(g);
  float seam = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
  s.albedo *= (0.9 + 0.25 * skinHash(floor(g))) * mix(0.55, 1.0, smoothstep(0.0, 0.035, seam));
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinPanelHeight, g, 0.01) * vec2(5.0, 6.0), 0.045);
  s.n = skinBump(s.n, uv, 0.004, 40.0);
  s.metal = 1.0;
  s.rough = 0.3;
}
// 3: fur. The body is dark underfur; shells keep strands that thin outward.
void skinMaterial_hairy(inout SkinSurface s, vec2 uv, float shell) {
  float strand = skinHash(floor(uv * 96.0 + shell * vec2(0.0, 1.5)));
  s.alpha = strand > shell * shell * 0.5 + shell * 0.5 ? 1.0 : 0.0;
  s.albedo *= mix(0.55, 1.05, shell) * (0.9 + 0.2 * strand);
  s.n = skinBump(s.n, uv, 0.02, 24.0);
  s.wrap = 0.6;
  s.rough = 0.9;
  s.rim = 0.4;
}
// 4: cel shading with an inked edge.
void skinMaterial_cartoon(inout SkinSurface s, vec2 uv, float shell) {
  s.albedo = mix(s.albedo, s.albedo * s.albedo * 1.5, 0.2);
  s.cel = 3.0;
  s.rough = 0.4;
  s.spec = 0.6;
}
// 5: woven cloth, threads over and under.
float skinWeaveHeight(vec2 p) {
  float over = step(0.0, sin(p.x * 3.14159) * sin(p.y * 3.14159));
  float warp = 0.5 + 0.5 * sin(p.x * 6.28318);
  float weft = 0.5 + 0.5 * sin(p.y * 6.28318);
  return mix(warp, weft, over);
}
void skinMaterial_woven(inout SkinSurface s, vec2 uv, float shell) {
  vec2 p = uv * 36.0;
  float h = skinWeaveHeight(p);
  s.albedo *= 0.78 + 0.32 * h;
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinWeaveHeight, p, 0.05) * 36.0, 0.02);
  s.rough = 0.95;
  s.spec = 0.15;
  s.rim = 0.25;
}
// 6: thick dripping goo.
float skinGooHeight(vec2 p) {
  return 0.75 * skinNoise(p * vec2(7.0, 2.5)) + 0.25 * skinNoise(p * 13.0 + 5.0);
}
void skinMaterial_goo(inout SkinSurface s, vec2 uv, float shell) {
  float h = skinGooHeight(uv);
  s.albedo *= mix(0.7, 1.1, smoothstep(0.2, 0.8, h));
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinGooHeight, uv, 0.01), 0.09);
  s.rough = 0.08;
  s.spec = 1.0;
  s.wrap = 0.5;
}
// 7: planks with wobbling growth rings.
float skinWoodHeight(vec2 p) {
  float wobble = 0.35 * skinNoise(p * vec2(2.0, 5.0)) + 0.08 * skinNoise(p * vec2(9.0, 30.0));
  float ring = 0.5 + 0.5 * sin((p.x + wobble) * 9.0 * 6.28318);
  return ring * ring * ring * ring;
}
void skinMaterial_wood(inout SkinSurface s, vec2 uv, float shell) {
  float h = skinWoodHeight(uv);
  float grain = skinNoise(uv * vec2(6.0, 180.0));
  s.albedo *= (0.62 + 0.45 * (1.0 - h)) * (0.92 + 0.12 * grain);
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinWoodHeight, uv, 0.003), 0.003);
  s.rough = 0.7;
  s.spec = 0.25;
}
// 8: dry stone blocks with cracked joints.
vec2 skinBlocks(vec2 p) {
  vec2 g = p * vec2(5.0, 8.0);
  g.x += 0.5 * mod(floor(g.y), 2.0);
  return g;
}
float skinStoneHeight(vec2 p) {
  vec2 f = fract(skinBlocks(p));
  float joint = min(min(f.x, 1.0 - f.x) * 0.6, min(f.y, 1.0 - f.y));
  return 0.7 * smoothstep(0.0, 0.1, joint) + 0.3 * skinStucci(p * 30.0);
}
void skinMaterial_stone(inout SkinSurface s, vec2 uv, float shell) {
  vec2 g = skinBlocks(uv);
  s.albedo *= (0.75 + 0.35 * skinHash(floor(g))) * (0.9 + 0.2 * skinNoise(uv * 90.0));
  s.albedo = mix(s.albedo, vec3(dot(s.albedo, vec3(0.33))), 0.3);
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinStoneHeight, uv, 0.003), 0.03);
  s.spec = 0.05;
}
// 9: overlapping scales.
float skinScaleHeight(vec2 p) { return skinScales(p, 0.78).x; }
void skinMaterial_scales(inout SkinSurface s, vec2 uv, float shell) {
  vec2 p = uv * vec2(14.0, 10.0);
  vec2 scale = skinScales(p, 0.78);
  s.albedo *= 0.85 + 0.3 * scale.y;
  s.albedo *= mix(0.5, 1.0, smoothstep(0.0, 0.5, scale.x));
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinScaleHeight, p, 0.02) * vec2(14.0, 10.0), 0.03);
  s.rough = 0.4;
  s.spec = 0.5;
}
// 10: pebbled leather with creases.
float skinLeatherHeight(vec2 p) { return 0.6 * skinNoise(p * 40.0) + 0.4 * skinRidge(p * 6.0); }
void skinMaterial_leather(inout SkinSurface s, vec2 uv, float shell) {
  float h = skinLeatherHeight(uv);
  s.albedo *= 0.8 + 0.3 * h;
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinLeatherHeight, uv, 0.003), 0.025);
  s.rough = 0.75;
  s.spec = 0.25;
  s.wrap = 0.2;
}
// 11: faceted crystal with an inner glow.
void skinMaterial_crystal(inout SkinSurface s, vec2 uv, float shell) {
  float jitter = 0.7 * skinNoise(uv * 5.0);
  vec3 facet = normalize(floor(s.n * 2.5 + 0.5 + jitter) / 2.5 + vec3(0.001));
  s.n = normalize(mix(s.n, facet, 0.85));
  // Inner planes catch light where the outer facet does not.
  float inner = smoothstep(0.55, 0.9, skinNoise(uv * 7.0 + 11.0));
  s.albedo = mix(s.albedo, vec3(1.0), 0.15);
  s.metal = 0.6;
  s.spec = 1.0;
  s.rough = 0.0;
  s.rim = 0.4;
  s.emissive = 0.35 * s.albedo * pow(1.0 - max(0.0, s.n.z), 2.0) + 0.45 * s.albedo * inner;
}
// 12: cooling crust over glowing cracks, in the paint's colour.
float skinLavaHeight(vec2 p) { return skinStucci(p * 9.0); }
void skinMaterial_lava(inout SkinSurface s, vec2 uv, float shell) {
  float c = skinLavaHeight(uv) / 1.5;
  float crack = smoothstep(0.58, 0.68, c);
  vec3 glow = mix(s.albedo * 1.3 + vec3(0.3, 0.05, 0.0), s.albedo * 1.4 + vec3(0.5, 0.45, 0.2), smoothstep(0.66, 0.8, c));
  s.albedo = mix(mix(vec3(0.14), s.albedo * 0.35, 0.5), vec3(0.05), crack);
  s.emissive = glow * crack * (0.75 + 0.25 * skinNoise(uv * 30.0)) + s.albedo * 0.3 * smoothstep(0.45, 0.58, c) * vec3(1.0, 0.4, 0.1);
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinLavaHeight, uv, 0.004), 0.03);
  s.spec = 0.1;
}
// 13: chrome, a mirror of the sky with a horizon line.
void skinMaterial_chrome(inout SkinSurface s, vec2 uv, float shell) {
  s.albedo = mix(s.albedo, vec3(1.0), 0.5);
  s.metal = 1.0;
  s.rough = 0.02;
  float ry = 2.0 * s.n.z * s.n.y;
  s.emissive = s.albedo * 0.3 * (1.0 - smoothstep(0.0, 0.08, abs(ry - 0.05)));
}
// 14: carbon fibre, a checker of thread directions.
void skinMaterial_carbon(inout SkinSurface s, vec2 uv, float shell) {
  vec2 g = uv * 60.0;
  float check = abs(step(0.5, fract(g.x)) - step(0.5, fract(g.y)));
  float thread = mix(sin(uv.x * 480.0), sin(uv.y * 480.0), check);
  s.albedo *= 0.35 + 0.1 * check + 0.08 * thread;
  s.rough = 0.3;
  s.spec = 0.6 + 0.4 * check;
}
// 15: velvet, bright along the silhouette.
void skinMaterial_velvet(inout SkinSurface s, vec2 uv, float shell) {
  float edge = pow(1.0 - max(0.0, s.n.z), 1.5);
  s.albedo *= (0.55 + 1.1 * edge) * (0.95 + 0.1 * skinNoise(uv * 96.0));
  s.wrap = 0.3;
}
// 16: honeycomb cells with raised walls.
float skinHoneyHeight(vec2 p) {
  float edge = skinHex(p).x;
  return 1.0 - smoothstep(0.0, 0.1, edge) + 0.3 * smoothstep(0.1, 0.5, edge);
}
void skinMaterial_honeycomb(inout SkinSurface s, vec2 uv, float shell) {
  vec2 p = uv * 9.0;
  vec3 hex = skinHex(p);
  s.albedo *= vec3(1.05, 0.95, 0.8) * mix(0.6, 1.1, smoothstep(0.0, 0.1, hex.x)) * (0.9 + 0.2 * skinHash(hex.yz));
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinHoneyHeight, p, 0.02) * 9.0, 0.03);
  s.rough = 0.5;
  s.spec = 0.4;
}
// 17: ice, cool and glassy, with internal fractures.
float skinIceHeight(vec2 p) { return skinRidge(p * 16.0) * 0.7 + 0.3 * skinRidge(p * 7.0 + 3.0); }
void skinMaterial_ice(inout SkinSurface s, vec2 uv, float shell) {
  float fracture = smoothstep(0.9, 0.97, skinIceHeight(uv));
  s.albedo = mix(s.albedo, vec3(0.8, 0.92, 1.0), 0.35) * (0.9 + 0.5 * fracture);
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinIceHeight, uv, 0.003), 0.004);
  s.rough = 0.08;
  s.spec = 1.0;
  s.rim = 0.25;
}
// 18: a fluffy cloud; shells carry lumps that shrink outward.
float skinCloudHeight(vec2 p) { return skinStucci(p * 5.0); }
void skinMaterial_cloud(inout SkinSurface s, vec2 uv, float shell) {
  float lump = skinStucci(uv * 10.0 + shell * 0.7) / 1.5;
  s.alpha = lump > shell * 1.3 ? 1.0 : 0.0;
  s.albedo = mix(s.albedo, vec3(1.0), 0.3) * mix(0.8, 1.0, shell);
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinCloudHeight, uv, 0.005), 0.05);
  s.wrap = 0.8;
}
// 19: hammered metal, a field of dimples.
float skinHammerHeight(vec2 p) { return 1.0 - skinScales(p, 0.56).x; }
void skinMaterial_hammered(inout SkinSurface s, vec2 uv, float shell) {
  vec2 p = uv * 12.0;
  s.albedo *= 0.9 + 0.2 * skinScales(p, 0.56).y;
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinHammerHeight, p, 0.02) * 12.0, 0.018);
  s.metal = 1.0;
  s.rough = 0.3;
}
// 20: hard candy, saturated and glassy.
void skinMaterial_candy(inout SkinSurface s, vec2 uv, float shell) {
  s.albedo = mix(s.albedo, s.albedo * s.albedo * 1.5, 0.3);
  s.rough = 0.1;
  s.spec = 1.0;
  s.wrap = 0.1;
}
// 21: translucent slime with bubbles.
float skinSlimeHeight(vec2 p) {
  vec2 bubble = skinScales(p * 14.0, 0.34);
  return skinGooHeight(p) - 0.5 * bubble.x * step(0.6, bubble.y);
}
void skinMaterial_slime(inout SkinSurface s, vec2 uv, float shell) {
  float h = skinGooHeight(uv);
  s.albedo *= mix(0.7, 1.15, smoothstep(0.25, 0.8, h));
  s.n = skinTilt(s.n, uv, SKIN_GRADIENT(skinSlimeHeight, uv, 0.004), 0.06);
  s.rough = 0.12;
  s.spec = 0.9;
  s.wrap = 0.7;
  s.emissive = 0.08 * s.albedo;
}

// Shades one texel: the painted albedo, its material id, the camera-space
// surface normal, the mesh UV and the shell fraction. Unknown ids are matte.
// Alpha is 0 where a shell pass has no strand.
vec4 skinShade(vec3 albedo, float material, vec3 surfaceNormal, vec2 uv, float shell) {
  SkinSurface s = skinDefault(albedo, normalize(surfaceNormal));
  int m = int(material + 0.5);
  if (m == 0) skinMaterial_glossy(s, uv, shell);
  else if (m == 2) skinMaterial_metallic(s, uv, shell);
  else if (m == 3) skinMaterial_hairy(s, uv, shell);
  else if (m == 4) skinMaterial_cartoon(s, uv, shell);
  else if (m == 5) skinMaterial_woven(s, uv, shell);
  else if (m == 6) skinMaterial_goo(s, uv, shell);
  else if (m == 7) skinMaterial_wood(s, uv, shell);
  else if (m == 8) skinMaterial_stone(s, uv, shell);
  else if (m == 9) skinMaterial_scales(s, uv, shell);
  else if (m == 10) skinMaterial_leather(s, uv, shell);
  else if (m == 11) skinMaterial_crystal(s, uv, shell);
  else if (m == 12) skinMaterial_lava(s, uv, shell);
  else if (m == 13) skinMaterial_chrome(s, uv, shell);
  else if (m == 14) skinMaterial_carbon(s, uv, shell);
  else if (m == 15) skinMaterial_velvet(s, uv, shell);
  else if (m == 16) skinMaterial_honeycomb(s, uv, shell);
  else if (m == 17) skinMaterial_ice(s, uv, shell);
  else if (m == 18) skinMaterial_cloud(s, uv, shell);
  else if (m == 19) skinMaterial_hammered(s, uv, shell);
  else if (m == 20) skinMaterial_candy(s, uv, shell);
  else if (m == 21) skinMaterial_slime(s, uv, shell);
  else skinMaterial_matte(s, uv, shell);
  if (shell > 0.0 && s.alpha < 0.5) return vec4(0.0);
  return vec4(skinLight(s), 1.0);
}
