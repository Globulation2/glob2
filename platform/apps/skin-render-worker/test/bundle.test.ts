import { expect, it } from 'vitest';
import { validateBundle } from '../src/bundle.ts';
const source = {
  manifest_sha256: 'a'.repeat(64),
  texture_sha256: 'b'.repeat(64),
  material_sha256: 'c'.repeat(64),
  swarm_mesh: 'classic',
  swarm_view_angle: 0,
};
const revision = 'd'.repeat(64);
function fixture() {
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
  const page = (clip: number, first: number, frames: number, size = 1024) => ({
    clip: clips[clip],
    first,
    frames,
    width: size,
    height: size,
    sha256: 'e'.repeat(64),
    bytes: 100,
  });
  return {
    format: 'colony-sprites-v1',
    renderRevision: revision,
    sourceManifestSha256: source.manifest_sha256,
    textureSha256: source.texture_sha256,
    materialSha256: source.material_sha256,
    swarmMesh: 'classic',
    swarmViewAngle: 0,
    frameMapping: { directions: 8, phases: 32, phaseShift: 3, direction8Shift: 5 },
    turnMapping: { first: 256, frames: 32, phaseShift: 3 },
    tileSize: 128,
    padding: 1.25,
    logicalSizes: [38, 38, 38, 40, 40, 40, 32, 96],
    encoding: 'bundled-images-v3-webp-only',
    pages: Array.from({ length: 29 }, (_, i) =>
      i < 28 ? page(Math.floor(i / 4), (i % 4) * 64, 64) : page(7, 0, 1, 128),
    ),
    turnPages: Array.from({ length: 7 }, (_, i) => page(i, 256, 32)),
  };
}
it('returns every page for storage while retaining the original bundle contract', () => {
  const doc = fixture();
  const bundle = validateBundle(Buffer.from(JSON.stringify(doc)), source, revision);
  expect(doc.pages).toHaveLength(29);
  expect(bundle.pages).toHaveLength(36);
  expect(bundle.pages.slice(29)).toEqual(doc.turnPages);
});
it.each([
  'missing mapping',
  'missing pages',
  'partial pages',
  'wrong offset',
  'wrong phase',
  'wrong clip',
  'wrong count',
  'wrong size',
  'unsafe hash',
  'oversized page',
  'extra mapping',
])('rejects invalid turn extension: %s', (fault) => {
  const doc = fixture() as unknown as Record<string, unknown>;
  const turns = doc['turnPages'] as Record<string, unknown>[];
  const mapping = doc['turnMapping'] as Record<string, unknown>;
  if (fault === 'missing mapping') delete doc['turnMapping'];
  if (fault === 'missing pages') delete doc['turnPages'];
  if (fault === 'partial pages') turns.pop();
  if (fault === 'wrong offset') mapping['first'] = 0;
  if (fault === 'wrong phase') mapping['phaseShift'] = 5;
  if (fault === 'wrong clip') turns[0]!['clip'] = 'swarm';
  if (fault === 'wrong count') turns[0]!['frames'] = 64;
  if (fault === 'wrong size') turns[0]!['width'] = 512;
  if (fault === 'unsafe hash') turns[0]!['sha256'] = '../bad';
  if (fault === 'oversized page') turns[0]!['bytes'] = 2 * 1024 * 1024 + 1;
  if (fault === 'extra mapping') mapping['extra'] = true;
  expect(() => validateBundle(Buffer.from(JSON.stringify(doc)), source, revision)).toThrow();
});
