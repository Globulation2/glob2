/** Version 1 sprite bundle contract shared by output validation and worker startup. */
export const SPRITE_BUNDLE_FORMAT = 'colony-sprites-v1';
const clips = [
  'worker-walk',
  'worker-swim',
  'worker-harvest',
  'warrior-walk',
  'warrior-swim',
  'warrior-fight',
  'explorer-fly',
  'swarm',
];
const sizes = [38, 38, 38, 40, 40, 40, 32, 96];
function validFrameMapping(value: unknown) {
  if (!value || typeof value !== 'object') return false;
  const mapping = value as Record<string, unknown>;
  return (
    mapping['directions'] === 8 &&
    mapping['phases'] === 32 &&
    mapping['phaseShift'] === 3 &&
    mapping['direction8Shift'] === 5
  );
}
export interface Page {
  clip: string;
  first: number;
  frames: number;
  width: number;
  height: number;
  sha256: string;
  bytes: number;
}
export interface Bundle {
  pages: Page[];
}
/** Validate native output before reading filenames or exposing any bytes. */
export function validateBundle(
  bytes: Buffer,
  source: {
    manifest_sha256: string;
    texture_sha256: string;
    material_sha256: string;
    swarm_mesh: string;
    swarm_view_angle: number;
  },
  revision: string,
): Bundle {
  if (bytes.length > 65536) throw new Error('Oversized skin manifest');
  const doc = JSON.parse(bytes.toString()) as Record<string, unknown>;
  if (
    doc['format'] !== SPRITE_BUNDLE_FORMAT ||
    doc['renderRevision'] !== revision ||
    doc['sourceManifestSha256'] !== source.manifest_sha256 ||
    doc['textureSha256'] !== source.texture_sha256 ||
    doc['materialSha256'] !== source.material_sha256 ||
    doc['swarmMesh'] !== source.swarm_mesh ||
    doc['swarmViewAngle'] !== source.swarm_view_angle ||
    !validFrameMapping(doc['frameMapping']) ||
    doc['tileSize'] !== 128 ||
    doc['padding'] !== 1.25 ||
    doc['encoding'] !== 'bundled-images-v3-webp-only' ||
    JSON.stringify(doc['logicalSizes']) !== JSON.stringify(sizes)
  )
    throw new Error('Skin output identity mismatch');
  const pages = doc['pages'];
  if (!Array.isArray(pages) || pages.length !== 29) throw new Error('Incomplete sprite bundle');
  pages.forEach((raw: unknown, index: number) => {
    if (!raw || typeof raw !== 'object') throw new Error('Invalid sprite page');
    const p = raw as Record<string, unknown>,
      clip = index < 28 ? Math.floor(index / 4) : 7,
      size = index < 28 ? 1024 : 128;
    if (
      p['clip'] !== clips[clip] ||
      p['first'] !== (index < 28 ? (index % 4) * 64 : 0) ||
      p['frames'] !== (index < 28 ? 64 : 1) ||
      p['width'] !== size ||
      p['height'] !== size ||
      typeof p['sha256'] !== 'string' ||
      !/^[0-9a-f]{64}$/.test(p['sha256']) ||
      typeof p['bytes'] !== 'number' ||
      !Number.isInteger(p['bytes']) ||
      p['bytes'] < 1 ||
      p['bytes'] > 2 * 1024 * 1024
    )
      throw new Error('Invalid sprite page');
  });
  return { pages: pages as Page[] };
}
