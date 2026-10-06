// SPDX-License-Identifier: GPL-3.0-or-later
// Colony-skin materials, shared verbatim by the game's tile renderer, the
// server sprite baker and the web studio. Ids, names, the shell count and the
// fur geometry live in skin-materials.json; scons/skin_materials.py checks the
// defines below agree with it and compiles both files into SkinMaterials.h.
//
// Valid as GLSL 1.20 and GLSL ES 3.00: no version line, no loops, texture
// lookups only through the SKIN_TEXTURE macro a wrapper may define. Normals are
// camera space; the camera is orthographic along +Z. Mesh UVs are 0..1 per
// model and fold front/back, so patterns are frequencies per UV unit: macro
// structure at 4-12 cells reads at the 32 px game size, micro grain at 24-96
// is faded out as texels shrink (SkinSurface.detail) so it never aliases.
#define SKIN_SHELLS 8

// Sin-free hash: the classic sin(dot()) form bands on mobile GPUs.
float skinHash(vec2 p) {
  vec3 q = fract(vec3(p.xyx) * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return fract((q.x + q.y) * q.z);
}
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
// Central-difference gradient of a height function over UV. One line: GLSL
// 1.20 has no line continuation.
#define SKIN_GRADIENT(height, p, e) (vec2(height((p) + vec2(e, 0.0)) - height((p) - vec2(e, 0.0)), height((p) + vec2(0.0, e)) - height((p) - vec2(0.0, e))) / (2.0 * (e)))

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
  vec2 du, dv;    // screen-space derivatives of the mesh UV, taken once before
                  // the material branch so they stay defined on every GPU
  float detail;   // 1 when a UV unit spans many pixels, 0 when texels are
                  // smaller than pixels: fades micro grain instead of aliasing
  float rough;    // 0 mirror .. 1 chalk
  float spec;     // dielectric highlight and reflection strength
  float metal;    // reflections take the albedo's colour, diffuse fades out
  float wrap;     // diffuse wraps past the terminator: soft, translucent
  float rim;      // fringe along the silhouette, lifted toward white
  float cel;      // >0: that many diffuse bands, hard highlight, inked edge
  vec3 emissive;
  float alpha;    // below 0.5 on a shell pass: no strand here
};
SkinSurface skinDefault(vec3 albedo, vec3 n, vec2 uv) {
  SkinSurface s;
  s.albedo = albedo;
  s.n = n;
  s.du = vec2(dFdx(uv.x), dFdy(uv.x));
  s.dv = vec2(dFdx(uv.y), dFdy(uv.y));
  s.detail = clamp(1.0 - 24.0 * (length(s.du) + length(s.dv)), 0.0, 1.0);
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
// Tilts the normal by a UV-space height gradient g = (dh/du, dh/dv). Meshes
// carry no tangents, so the gradient is expressed in screen directions with
// the derivatives, independent of resolution; stretched UV regions keep a
// bounded tilt.
void skinTilt(inout SkinSurface s, vec2 g, float amount) {
  float density = max(0.5 * (length(s.du) + length(s.dv)), 1e-6);
  vec2 t = amount * (g.x * s.du + g.y * s.dv) / density;
  t *= min(1.0, 0.7 / max(length(t), 1e-6));
  s.n = normalize(s.n - vec3(t, 0.0));
}
float skinStucciHeight(vec2 p) { return skinStucci(p); }
void skinBump(inout SkinSurface s, vec2 uv, float amount, float frequency) {
  skinTilt(s, SKIN_GRADIENT(skinStucciHeight, uv * frequency, 0.05) * frequency, amount);
}
// One light from the upper left as the classic sprites, a hemisphere ambient,
// a reflected sky/ground gradient, a Blinn-Phong highlight sized to survive
// the 32 px game sprite, and a small white sheen so dark paint keeps its form.
vec3 skinLight(SkinSurface s) {
  vec3 n = normalize(s.n);
  vec3 l = normalize(vec3(-0.4, 0.7, 1.0));
  vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
  float facing = max(0.0, n.z);
  float nl = dot(n, l);
  float diffuse = max(0.0, (nl + s.wrap) / (1.0 + s.wrap));
  float gloss = 1.0 - s.rough;
  float power = exp2(2.0 + 6.5 * gloss);
  float highlight = pow(max(0.0, dot(n, h)), power) * (0.5 + 1.2 * gloss) * smoothstep(-0.2, 0.2, nl);
  float fresnel = pow(1.0 - facing, 4.0);
  // The sky reflected by the view direction: bright from above, dark ground.
  // Metals keep a crisp horizon; dielectrics get a softer gradient.
  float ry = 2.0 * n.z * n.y;
  float horizon = mix(smoothstep(-0.8, 1.0, ry), smoothstep(-0.5, 0.9, ry), s.metal);
  vec3 sky = mix(vec3(0.26, 0.25, 0.28), vec3(1.0), horizon);
  float reflectivity = mix(s.spec * gloss * (0.06 + 0.6 * fresnel), 0.6 + 0.3 * fresnel, s.metal);
  vec3 tint = mix(vec3(1.0), mix(s.albedo, vec3(1.0), 0.3 * fresnel), s.metal);
  float ambient = 0.2 + 0.12 * n.y;
  float ink = 1.0;
  if (s.cel > 0.0) {
    diffuse = min(1.0, floor(diffuse * s.cel) / max(1.0, s.cel - 1.0));
    highlight = step(0.5, highlight);
    reflectivity = 0.0;
    ambient = max(ambient, 0.35);
    ink = mix(0.3, 1.0, smoothstep(0.22, 0.34, facing));
  }
  vec3 color = s.albedo * (1.0 - 0.6 * s.metal) * (ambient + (1.0 - ambient) * diffuse);
  color += tint * sky * reflectivity;
  color += tint * max(s.spec, s.metal) * highlight;
  color += mix(s.albedo, vec3(1.0), 0.5) * s.rim * pow(1.0 - facing, 2.0);
  color += vec3(0.04) * (0.5 + fresnel) * (1.0 - s.metal);
  color += s.emissive;
  return color * ink;
}

// --- Materials. Each fills the surface from the albedo, mesh UV and the fur
// shell fraction (0 on the body, k/SKIN_SHELLS on shell k). Micro octaves are
// scaled by s.detail so they fade cleanly in the baker's small tiles. -------

// 0: the original glob, a soft bumped plastic with broad white streaks.
void skinMaterial_glossy(inout SkinSurface s, vec2 uv, float shell) {
  skinBump(s, uv, 0.007, 8.0);
  skinBump(s, uv, 0.002 * s.detail, 48.0);
  s.rough = 0.7;
  s.spec = 0.6;
  s.wrap = 0.15;
}
// 1: soft clay with a fine grain and the faintest sheen.
void skinMaterial_matte(inout SkinSurface s, vec2 uv, float shell) {
  s.albedo *= 1.0 - 0.06 * s.detail + 0.12 * s.detail * skinNoise(uv * 80.0);
  skinBump(s, uv, 0.006, 30.0);
  s.wrap = 0.35;
  s.rough = 0.9;
  s.spec = 0.08;
}
// 2: riveted metal panels with recessed seams.
float skinPanelHeight(vec2 g) {
  vec2 f = fract(g);
  vec2 edge = min(f, 1.0 - f);
  float seam = min(edge.x, edge.y);
  vec2 d = edge - 0.16;
  float r = length(d) / 0.11;
  float rivet = r < 1.0 ? sqrt(1.0 - r * r) : 0.0;
  return 0.5 * smoothstep(0.0, 0.05, seam) + 0.5 * rivet;
}
void skinMaterial_metallic(inout SkinSurface s, vec2 uv, float shell) {
  vec2 g = uv * vec2(5.0, 6.0);
  vec2 f = fract(g);
  float seam = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
  s.albedo *= (0.9 + 0.25 * skinHash(floor(g))) * mix(0.68, 1.0, smoothstep(0.0, 0.035, seam));
  skinTilt(s, SKIN_GRADIENT(skinPanelHeight, g, 0.01) * vec2(5.0, 6.0), 0.06);
  skinBump(s, uv, 0.004 * s.detail, 40.0);
  s.metal = 1.0;
  s.rough = 0.38;
}
// 3: fur. The body is darker underfur; shells keep clumped strands that lean
// and thin outward, with a jittered threshold so shells do not band.
void skinMaterial_hairy(inout SkinSurface s, vec2 uv, float shell) {
  float cell = skinHash(floor(uv * 64.0 + shell * vec2(0.4, 2.5)));
  float strand = 0.6 * cell + 0.4 * skinNoise(uv * 12.0);
  float threshold = 0.45 * shell + 0.45 * shell * shell + 0.15 * (cell - 0.5);
  s.alpha = strand > threshold ? 1.0 : 0.0;
  s.albedo *= mix(0.65, 1.05, shell) * (0.9 + 0.2 * cell);
  skinBump(s, uv, 0.02 * s.detail, 24.0);
  s.wrap = 0.6;
  s.rough = 0.9;
  s.rim = 0.4;
}
// 4: cel shading with an inked edge.
void skinMaterial_cartoon(inout SkinSurface s, vec2 uv, float shell) {
  s.albedo = mix(s.albedo, s.albedo * s.albedo * 1.5, 0.2);
  s.cel = 3.0;
  s.rough = 0.55;
  s.spec = 0.6;
}
// 5: woven cloth, a fine weave under a coarse thread stripe that survives the
// game scale as a faint plaid.
float skinWeaveHeight(vec2 p) {
  float over = step(0.0, sin(p.x * 3.14159) * sin(p.y * 3.14159));
  float warp = 0.5 + 0.5 * sin(p.x * 6.28318);
  float weft = 0.5 + 0.5 * sin(p.y * 6.28318);
  return mix(warp, weft, over);
}
void skinMaterial_woven(inout SkinSurface s, vec2 uv, float shell) {
  vec2 p = uv * 48.0;
  float h = skinWeaveHeight(p);
  vec2 stripe = step(0.875, fract(uv * 6.0));
  s.albedo *= (0.88 + 0.16 * h * s.detail) * (1.0 - 0.08 * max(stripe.x, stripe.y));
  skinTilt(s, SKIN_GRADIENT(skinWeaveHeight, p, 0.05) * 48.0, 0.008 * s.detail);
  s.rough = 0.95;
  s.spec = 0.15;
  s.rim = 0.25;
}
// 6: thick dripping goo: drips run along v, the mesh's vertical.
float skinGooHeight(vec2 p) {
  return 0.8 * skinNoise(p * vec2(8.0, 2.0)) + 0.2 * skinNoise(p * 9.0 + 5.0);
}
void skinMaterial_goo(inout SkinSurface s, vec2 uv, float shell) {
  float h = skinGooHeight(uv);
  s.albedo *= mix(0.7, 1.1, smoothstep(0.2, 0.8, h));
  skinTilt(s, SKIN_GRADIENT(skinGooHeight, uv, 0.01), 0.06);
  s.rough = 0.16;
  s.spec = 1.0;
  s.wrap = 0.5;
}
// 7: varnished planks with wobbling growth rings.
float skinWoodHeight(vec2 p) {
  float plank = floor(p.y * 3.0);
  float wobble = 0.25 * skinNoise(p * vec2(1.5, 3.0)) + 0.05 * skinNoise(p * vec2(6.0, 20.0));
  float ring = 0.5 + 0.5 * sin((p.x + wobble + 0.37 * plank) * 5.0 * 6.28318);
  return ring * ring * ring * ring;
}
void skinMaterial_wood(inout SkinSurface s, vec2 uv, float shell) {
  float h = skinWoodHeight(uv);
  float grain = skinNoise(uv * vec2(6.0, 180.0));
  float edge = min(fract(uv.y * 3.0), 1.0 - fract(uv.y * 3.0));
  s.albedo *= (0.74 + 0.32 * (1.0 - h)) * (1.0 - 0.1 * s.detail + 0.12 * s.detail * grain) * mix(0.85, 1.0, smoothstep(0.0, 0.02, edge));
  skinTilt(s, SKIN_GRADIENT(skinWoodHeight, uv, 0.003), 0.003);
  s.rough = 0.6;
  s.spec = 0.25;
}
// 8: dry stone blocks in a running bond.
vec2 skinBlocks(vec2 p) {
  vec2 g = p * vec2(4.0, 6.0);
  g.x += 0.5 * mod(floor(g.y), 2.0);
  return g;
}
float skinStoneHeight(vec2 p) {
  vec2 f = fract(skinBlocks(p));
  float joint = min(min(f.x, 1.0 - f.x) * 0.6, min(f.y, 1.0 - f.y));
  return 0.6 * smoothstep(0.0, 0.1, joint) + 0.4 * skinStucci(p * 18.0);
}
void skinMaterial_stone(inout SkinSurface s, vec2 uv, float shell) {
  vec2 g = skinBlocks(uv);
  s.albedo *= (0.75 + 0.35 * skinHash(floor(g))) * (1.0 - 0.1 * s.detail + 0.2 * s.detail * skinNoise(uv * 90.0));
  s.albedo = mix(s.albedo, vec3(dot(s.albedo, vec3(0.33))), 0.35);
  skinTilt(s, SKIN_GRADIENT(skinStoneHeight, uv, 0.003), 0.03);
  s.spec = 0.12;
  s.rough = 0.85;
}
// 9: overlapping scales.
float skinScaleHeight(vec2 p) { return skinScales(p, 0.78).x; }
void skinMaterial_scales(inout SkinSurface s, vec2 uv, float shell) {
  vec2 p = uv * vec2(10.0, 7.0);
  vec2 scale = skinScales(p, 0.78);
  s.albedo *= 0.92 + 0.15 * scale.y;
  s.albedo *= mix(0.4, 1.0, smoothstep(0.0, 0.5, scale.x));
  skinTilt(s, SKIN_GRADIENT(skinScaleHeight, p, 0.02) * vec2(10.0, 7.0), 0.04);
  s.rough = 0.4;
  s.spec = 0.5;
}
// 10: pebbled leather with a few long creases and a satin sheen.
float skinLeatherHeight(vec2 p) { return 0.75 * skinNoise(p * 26.0) + 0.25 * skinRidge(p * 4.0); }
void skinMaterial_leather(inout SkinSurface s, vec2 uv, float shell) {
  float h = skinLeatherHeight(uv);
  s.albedo *= 0.8 + 0.3 * h;
  skinTilt(s, SKIN_GRADIENT(skinLeatherHeight, uv, 0.003), 0.02 * mix(0.4, 1.0, s.detail));
  s.rough = 0.6;
  s.spec = 0.35;
  s.wrap = 0.2;
}
// 11: faceted crystal with sharp inner planes.
float skinCrystalPlanes(vec2 p) { return skinRidge(p * 9.0); }
void skinMaterial_crystal(inout SkinSurface s, vec2 uv, float shell) {
  // The glow follows the smooth silhouette, not the facets, so a steep facet
  // does not light up as a block.
  float edge = pow(1.0 - max(0.0, s.n.z), 2.0);
  float jitter = 0.35 * skinNoise(uv * 5.0);
  vec3 facet = normalize(floor(s.n * 5.0 + 0.5 + jitter) / 5.0 + vec3(0.001));
  // Facets only half replace the smooth normal, so the sky reflection and the
  // highlight fade across each one instead of lighting it as a flat block.
  s.n = normalize(mix(s.n, facet, 0.5));
  float inner = smoothstep(0.8, 0.95, skinCrystalPlanes(uv));
  s.albedo = mix(s.albedo, vec3(1.0), 0.08);
  s.metal = 0.4;
  s.spec = 0.6;
  s.rough = 0.2;
  s.rim = 0.4;
  s.emissive = min(vec3(0.5), 0.35 * s.albedo * edge + 0.45 * s.albedo * inner);
}
// 12: cooling crust over glowing cracks and a few pools, in the paint's colour.
float skinLavaHeight(vec2 p) { return skinStucci(p * 9.0); }
void skinMaterial_lava(inout SkinSurface s, vec2 uv, float shell) {
  float c = skinLavaHeight(uv) / 1.5;
  float crack = max(smoothstep(0.86, 0.95, skinRidge(uv * 7.0)), 0.3 * smoothstep(0.7, 0.8, c));
  vec3 glow = mix(s.albedo * 1.3 + vec3(0.3, 0.05, 0.0), s.albedo * 1.4 + vec3(0.5, 0.45, 0.2), smoothstep(0.66, 0.8, c));
  s.albedo = mix(mix(vec3(0.14), s.albedo * 0.35, 0.7), vec3(0.05), crack);
  s.emissive = glow * crack * (0.75 + 0.25 * skinNoise(uv * 30.0)) + s.albedo * 0.3 * smoothstep(0.45, 0.58, c) * vec3(1.0, 0.4, 0.1);
  skinTilt(s, SKIN_GRADIENT(skinLavaHeight, uv, 0.004), 0.03);
  s.spec = 0.1;
}
// 13: chrome, a mirror of the sky with a soft horizon fading upward.
void skinMaterial_chrome(inout SkinSurface s, vec2 uv, float shell) {
  s.albedo = mix(s.albedo, vec3(1.0), 0.35);
  s.metal = 1.0;
  s.rough = 0.02;
  float ry = 2.0 * s.n.z * s.n.y;
  s.emissive = s.albedo * 0.18 * smoothstep(0.0, 0.14, ry - 0.05) * (1.0 - smoothstep(0.14, 0.5, ry - 0.05));
}
// 14: carbon fibre, a twill of thread directions.
void skinMaterial_carbon(inout SkinSurface s, vec2 uv, float shell) {
  vec2 g = uv * vec2(24.0, 12.0);
  float check = abs(step(0.5, fract(g.x)) - step(0.5, fract(g.y)));
  float thread = mix(sin(uv.x * 480.0), sin(uv.y * 480.0), check) * s.detail;
  s.albedo *= 0.45 + 0.1 * check + 0.08 * thread;
  s.rough = 0.3;
  s.metal = 0.3;
  s.spec = 0.6 + 0.4 * check;
}
// 15: velvet, dark where it faces the camera with a pale sheen along the edge.
void skinMaterial_velvet(inout SkinSurface s, vec2 uv, float shell) {
  float edge = 1.0 - max(0.0, s.n.z);
  s.albedo *= (0.5 + 0.3 * edge) * (1.0 - 0.05 * s.detail + 0.1 * s.detail * skinNoise(uv * 96.0));
  s.emissive = 0.35 * edge * mix(vec3(1.0), s.albedo, 0.5);
  s.wrap = 0.5;
}
// 16: honeycomb cells with raised walls.
float skinHoneyHeight(vec2 p) {
  float edge = skinHex(p).x;
  return 1.0 - smoothstep(0.0, 0.1, edge) + 0.3 * smoothstep(0.1, 0.5, edge);
}
void skinMaterial_honeycomb(inout SkinSurface s, vec2 uv, float shell) {
  vec2 p = uv * 9.0;
  vec3 hex = skinHex(p);
  s.albedo *= vec3(1.02, 0.99, 0.94) * mix(0.6, 1.1, smoothstep(0.0, 0.1, hex.x)) * (0.95 + 0.1 * skinHash(hex.yz));
  skinTilt(s, SKIN_GRADIENT(skinHoneyHeight, p, 0.02) * 9.0, 0.03);
  s.rough = 0.5;
  s.spec = 0.4;
}
// 17: ice, cool and glassy, with internal fractures that catch the light.
float skinIceHeight(vec2 p) { return skinRidge(p * 10.0) * 0.7 + 0.3 * skinRidge(p * 5.0 + 3.0); }
void skinMaterial_ice(inout SkinSurface s, vec2 uv, float shell) {
  float fracture = smoothstep(0.82, 0.92, skinIceHeight(uv));
  s.albedo = mix(s.albedo, vec3(0.8, 0.92, 1.0), 0.22) * (0.95 + 0.3 * fracture);
  skinTilt(s, SKIN_GRADIENT(skinIceHeight, uv, 0.003), 0.012);
  s.rough = 0.15;
  s.spec = 1.0;
  s.wrap = 0.4;
  s.rim = 0.25;
}
// 18: a fluffy cloud, shaded underneath; shells carry lumps that shrink outward.
float skinCloudHeight(vec2 p) { return skinStucci(p * 5.0); }
void skinMaterial_cloud(inout SkinSurface s, vec2 uv, float shell) {
  float lump = skinStucci(uv * 8.0 + shell * 0.7) / 1.5;
  s.alpha = lump > 0.2 + 0.75 * shell ? 1.0 : 0.0;
  s.albedo = mix(s.albedo, vec3(1.0), 0.3) * mix(0.8, 1.0, shell) * (0.85 + 0.15 * s.n.y);
  skinTilt(s, SKIN_GRADIENT(skinCloudHeight, uv, 0.005), 0.05);
  s.wrap = 0.8;
}
// 19: hammered metal, a field of dimples.
float skinHammerHeight(vec2 p) { return 1.0 - skinScales(p, 0.56).x; }
void skinMaterial_hammered(inout SkinSurface s, vec2 uv, float shell) {
  vec2 p = uv * 9.0;
  s.albedo *= 0.95 + 0.1 * skinScales(p, 0.56).y;
  skinTilt(s, SKIN_GRADIENT(skinHammerHeight, p, 0.02) * 9.0, 0.025);
  s.metal = 1.0;
  s.rough = 0.4;
}
// 20: hard candy, saturated and glassy with a sugary core.
void skinMaterial_candy(inout SkinSurface s, vec2 uv, float shell) {
  s.albedo = mix(s.albedo, s.albedo * s.albedo * 1.5, 0.3);
  s.rough = 0.2;
  s.spec = 1.0;
  s.wrap = 0.3;
}
// 21: translucent slime with a few domed bubbles.
float skinSlimeHeight(vec2 p) {
  vec2 bubble = skinScales(p * 10.0, 0.4);
  return skinGooHeight(p) + 0.5 * bubble.x * step(0.5, bubble.y);
}
void skinMaterial_slime(inout SkinSurface s, vec2 uv, float shell) {
  float h = skinGooHeight(uv);
  s.albedo *= mix(0.7, 1.15, smoothstep(0.25, 0.8, h));
  skinTilt(s, SKIN_GRADIENT(skinSlimeHeight, uv, 0.004), 0.06);
  s.rough = 0.18;
  s.spec = 0.9;
  s.wrap = 0.7;
  s.emissive = 0.08 * s.albedo;
}

// Shades one texel: the painted albedo, its material id, the camera-space
// surface normal, the mesh UV and the shell fraction. Unknown ids are matte.
// Alpha is 0 where a shell pass has no strand, including on back faces, whose
// pushed-out copies would otherwise fringe the silhouette.
vec4 skinShade(vec3 albedo, float material, vec3 surfaceNormal, vec2 uv, float shell) {
  SkinSurface s = skinDefault(albedo, normalize(surfaceNormal), uv);
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
  if (shell > 0.0 && (s.alpha < 0.5 || surfaceNormal.z < 0.0)) return vec4(0.0);
  return vec4(skinLight(s), 1.0);
}
// The mesh renderers' texel: paint and material ids come from the model's
// quadrant of the colony atlas; material noise keeps the mesh UV. Wrappers
// define SKIN_TEXTURE as texture (ES 3.00) or texture2D (GLSL 1.20).
#ifdef SKIN_TEXTURE
vec4 skinShadeAtlas(sampler2D paint, sampler2D material, vec2 region, vec3 normal, vec2 uv, float shell) {
  vec2 atlasUv = uv * 0.5 + region;
  float id = floor(SKIN_TEXTURE(material, atlasUv).r * 255.0 + 0.5);
  return skinShade(SKIN_TEXTURE(paint, atlasUv).rgb, id, normal, uv, shell);
}
#endif
// The swatch sphere: q is the fragment's position within the unit disc of a
// sphere seen along +Z; wrappers grow the disc per shell so fur shows around it.
vec4 skinShadeSphere(vec3 albedo, float material, vec2 q, float shell) {
  float r = dot(q, q);
  if (r > 1.0) return vec4(0.0);
  return skinShade(albedo, material, vec3(q, sqrt(1.0 - r)), (q + 1.0) * 0.5, shell);
}
