import { COLONY_SKIN_MATERIALS, type ColonySkinMaterial } from '@glob2/protocol';

// colony-v2 skins: a 512px colour atlas and a 512px material map, each holding
// one 256px quadrant per model. Mesh UVs map into a quadrant as uv / 2 + offset.
export const ATLAS_SIZE = 512;

export const MODELS = [
  { id: 'worker', name: 'Worker', mesh: 'worker-walk', x: 0, y: 0 },
  { id: 'warrior', name: 'Warrior', mesh: 'warrior-walk', x: 256, y: 0 },
  { id: 'explorer', name: 'Explorer', mesh: 'explorer-fly', x: 0, y: 256 },
  { id: 'swarm', name: 'Swarm', mesh: 'swarm', x: 256, y: 256 },
] as const;
export type Model = (typeof MODELS)[number];

// Ids are stored in the material map; the game shades each id differently.
export const MATERIALS = COLONY_SKIN_MATERIALS;
/** Materials in picker order, grouped under their registry group names. */
export const MATERIAL_GROUPS = MATERIALS.reduce<
  { name: string; materials: ColonySkinMaterial[] }[]
>((groups, material) => {
  const group = groups.find((g) => g.name === material.group);
  if (group) group.materials.push(material);
  else groups.push({ name: material.group, materials: [material] });
  return groups;
}, []);
/** Whether the material map's quadrant for a model paints any fur material. */
export function regionHasShells(map: Uint8Array, model: Model): boolean {
  for (let y = model.y; y < model.y + ATLAS_SIZE / 2; y++)
    for (let x = model.x; x < model.x + ATLAS_SIZE / 2; x++)
      if (MATERIALS[map[y * ATLAS_SIZE + x] ?? 0]?.shells) return true;
  return false;
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
