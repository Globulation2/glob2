// colony-v2 skins: a 512px colour atlas and a 512px material map, each holding
// one 256px quadrant per model. Mesh UVs map into a quadrant as uv / 2 + offset.
export const ATLAS_SIZE = 512;
export const MODEL_SIZE = 256;

export const MODELS = [
  { id: 'worker', name: 'Worker', mesh: 'worker-walk', x: 0, y: 0 },
  { id: 'warrior', name: 'Warrior', mesh: 'warrior-walk', x: 256, y: 0 },
  { id: 'explorer', name: 'Explorer', mesh: 'explorer-fly', x: 0, y: 256 },
  { id: 'swarm', name: 'Swarm', mesh: 'swarm', x: 256, y: 256 },
] as const;
export type Model = (typeof MODELS)[number];

// Ids are stored in the material map; the game shades each id differently.
export const MATERIALS = [
  { id: 0, name: 'Classic glossy', swatch: 'rgba(255,255,255,0)' },
  { id: 1, name: 'Matte', swatch: 'rgba(120,120,120,0.75)' },
  { id: 2, name: 'Metallic', swatch: 'rgba(70,170,235,0.75)' },
  { id: 3, name: 'Hairy', swatch: 'rgba(170,115,60,0.75)' },
] as const;

/** Sets a hard-edged disc of material ids, clipped to one model's quadrant. */
export function paintMaterial(
  map: Uint8Array,
  model: Model,
  cx: number,
  cy: number,
  radius: number,
  id: number,
) {
  const r = Math.max(0.5, radius);
  const x0 = Math.max(model.x, Math.floor(cx - r)),
    x1 = Math.min(model.x + MODEL_SIZE - 1, Math.ceil(cx + r));
  const y0 = Math.max(model.y, Math.floor(cy - r)),
    y1 = Math.min(model.y + MODEL_SIZE - 1, Math.ceil(cy + r));
  for (let y = y0; y <= y1; y++)
    for (let x = x0; x <= x1; x++) {
      const dx = x + 0.5 - cx,
        dy = y + 0.5 - cy;
      if (dx * dx + dy * dy <= r * r) map[y * ATLAS_SIZE + x] = id;
    }
}

/** Opaque grey PNG (id, id, id): canvas premultiplication cannot alter it. */
export function encodeMaterials(map: Uint8Array): string {
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = ATLAS_SIZE;
  const context = canvas.getContext('2d');
  if (!context) throw new Error('Painting is unavailable in this browser.');
  const image = context.createImageData(ATLAS_SIZE, ATLAS_SIZE);
  for (let i = 0; i < map.length; i++) {
    const id = map[i] ?? 0;
    image.data.set([id, id, id, 255], i * 4);
  }
  context.putImageData(image, 0, 0);
  return canvas.toDataURL('image/png');
}

export async function decodeMaterials(src: string): Promise<Uint8Array> {
  const image = new Image();
  image.src = src;
  await image.decode();
  if (image.width !== ATLAS_SIZE || image.height !== ATLAS_SIZE)
    throw new Error('Invalid material map dimensions.');
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = ATLAS_SIZE;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (!context) throw new Error('Painting is unavailable in this browser.');
  context.drawImage(image, 0, 0);
  const pixels = context.getImageData(0, 0, ATLAS_SIZE, ATLAS_SIZE).data;
  const map = new Uint8Array(ATLAS_SIZE * ATLAS_SIZE);
  for (let i = 0; i < map.length; i++) {
    const id = pixels[i * 4] ?? 0;
    if (id >= MATERIALS.length) throw new Error('Invalid material map.');
    map[i] = id;
  }
  return map;
}
