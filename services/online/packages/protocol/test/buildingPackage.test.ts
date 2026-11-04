import { describe, expect, it } from 'vitest';
import {
  buildingNamespacePrefix,
  checkBuildingPackage,
  forkBuildingPackage,
} from '../src/buildings.ts';
import {
  buildingAssetHash,
  canonicalBuildingJson,
  parseBuildingJson,
  readBuildingArchive,
  writeBuildingArchive,
  writeBuildingArtworkBundle,
} from '../src/node/buildingPackage.ts';
const namespace = '11111111-1111-4111-8111-111111111111',
  prefix = buildingNamespacePrefix(namespace);
function fixture() {
  return {
    schemaVersion: 1,
    namespace,
    experiments: [],
    sprites: [],
    variants: [
      {
        key: prefix + 'kitchen',
        properties: { gameSprite: 'data/gfx/inn0b' },
        semantics: {},
      },
    ],
  };
}
describe('portable building packages', () => {
  it('accepts families and empty transition references', () => {
    expect(checkBuildingPackage(fixture()).namespace).toBe(namespace);
    expect(
      checkBuildingPackage({
        ...fixture(),
        variants: [{ ...fixture().variants[0], next: '', previous: '' }],
      }),
    ).toBeTruthy();
  });
  it('rejects overrides, external references, runtime IDs and unsafe artwork', () => {
    for (const change of [
      { key: 'inn.0.finished' },
      { next: 'inn.0.finished' },
      { requiredExperiment: 'markets-v2' },
      {
        semantics: {
          market: { suppliesStockExperiment: 'b-22222222-2222-4222-8222-222222222222-enable' },
        },
      },
      { id: 0 },
      { properties: { gameSprite: '../outside' } },
      { properties: { gameSprite: 'package:missing' } },
    ])
      expect(() =>
        checkBuildingPackage({ ...fixture(), variants: [{ ...fixture().variants[0], ...change }] }),
      ).toThrow();
  });
  it('rewrites a fork without changing its source', () => {
    const source = {
      ...fixture(),
      experiments: [{ key: prefix + 'enable', label: 'Enable', help: 'Enable' }],
      variants: [
        {
          ...fixture().variants[0],
          next: prefix + 'kitchen',
          requiredExperiment: prefix + 'enable',
          semantics: {
            market: {
              suppliesStockExperiment: prefix + 'enable',
              fetchesStockExperiment: 'markets-v2',
            },
          },
          presentation: { connectionGroup: prefix + 'connection' },
        },
      ],
    };
    const copy = forkBuildingPackage(source, '22222222-2222-4222-8222-222222222222');
    expect(copy.variants[0]?.next).toBe(copy.variants[0]?.key);
    expect(copy.variants[0]?.requiredExperiment).toBe(copy.experiments[0]?.key);
    expect(copy.variants[0]?.presentation?.['connectionGroup']).toContain('22222222');
    expect(
      (copy.variants[0]?.semantics['market'] as Record<string, unknown>)['suppliesStockExperiment'],
    ).toBe(copy.experiments[0]?.key);
    expect(
      (copy.variants[0]?.semantics['market'] as Record<string, unknown>)['fetchesStockExperiment'],
    ).toBe('markets-v2');
    expect(source.variants[0]?.key).toBe(prefix + 'kitchen');
  });
  it('rejects ambiguous JSON, malformed syntax, deep nesting and oversized input', () => {
    for (const text of [
      '{"a":1,"a":2}',
      '{"a":1,"\\u0061":2}',
      '{"a":[1,]}',
      '01',
      'true false',
      '['.repeat(66) + '0' + ']'.repeat(66),
      '"' + 'a'.repeat(8 * 1024 * 1024) + '"',
    ])
      expect(() => parseBuildingJson(text)).toThrow();
    expect(parseBuildingJson('{"a":["a\\"b",true,null,1.2e-3]}')).toEqual({
      a: ['a"b', true, null, 0.0012],
    });
  });
  it('rejects deep or non-JSON authored fields before writing an unreadable package', () => {
    const cyclic: Record<string, unknown> = {};
    cyclic['loop'] = cyclic;
    for (const extra of [
      JSON.parse('['.repeat(66) + '0' + ']'.repeat(66)),
      cyclic,
      Infinity,
      new Date(),
      undefined,
    ]) {
      const source = {
        ...fixture(),
        variants: [{ ...fixture().variants[0], semantics: { extra } }],
      };
      expect(() => checkBuildingPackage(source)).toThrow();
      expect(() => writeBuildingArchive(source, new Map())).toThrow();
    }
  });
  it('exports reproducible archives and restores exact definitions', () => {
    const bytes = writeBuildingArchive(fixture(), new Map());
    expect(writeBuildingArchive(fixture(), new Map())).toEqual(bytes);
    expect(readBuildingArchive(bytes).package).toEqual(fixture());
    expect(canonicalBuildingJson({ z: 0, a: { d: 2, c: 1 } })).toBe('{"a":{"c":1,"d":2},"z":0}');
  });
  it('checks asset identities, CRCs and undeclared content', () => {
    const image = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10, 0]),
      hash = buildingAssetHash(image);
    const pkg = {
      ...fixture(),
      sprites: [{ key: 'kitchen', frames: [{ imageHash: hash, width: 1, height: 1 }] }],
    };
    const archive = writeBuildingArchive(pkg, new Map([[hash, image]]));
    expect(readBuildingArchive(archive).assets.get(hash)).toEqual(image);
    const corrupt = Buffer.from(archive);
    corrupt[40] = (corrupt[40] ?? 0) ^ 1;
    expect(() => readBuildingArchive(corrupt)).toThrow();
    expect(() => writeBuildingArchive(pkg, new Map([[hash, Buffer.from('bad')]]))).toThrow();
    expect(() => writeBuildingArchive(fixture(), new Map([[hash, image]]))).toThrow();
    expect(() => readBuildingArchive(archive.subarray(0, -1))).toThrow();
  });
  it('rejects symlinks, unsupported methods and traversal', () => {
    const archive = writeBuildingArchive(fixture(), new Map()),
      central = archive.readUInt32LE(archive.length - 6);
    const link = Buffer.from(archive);
    link.writeUInt32LE(0xa1ff0000, central + 38);
    expect(() => readBuildingArchive(link)).toThrow(/regular file/);
    const method = Buffer.from(archive);
    method.writeUInt16LE(99, central + 10);
    expect(() => readBuildingArchive(method)).toThrow(/encoding/);
    const traversal = Buffer.from(archive);
    traversal.write('../evil.json!', central + 46);
    expect(() => readBuildingArchive(traversal)).toThrow(/Unsafe/);
  });
  it('limits aggregate decoded image bytes', () => {
    const frame = { imageHash: 'a'.repeat(64), width: 512, height: 512 };
    expect(() =>
      checkBuildingPackage({
        ...fixture(),
        sprites: [{ key: 'huge', frames: Array.from({ length: 65 }, () => frame) }],
      }),
    ).toThrow(/64 MiB/);
  });
  it('counts repeated team-color references toward the decoded pixel budget', () => {
    const frame = {
      imageHash: 'a'.repeat(64),
      teamColorHash: 'a'.repeat(64),
      width: 512,
      height: 512,
    };
    const packageWithFrames = (count: number) => ({
      ...fixture(),
      sprites: [{ key: 'layered', frames: Array.from({ length: count }, () => frame) }],
    });
    expect(() => checkBuildingPackage(packageWithFrames(32))).not.toThrow();
    expect(() => checkBuildingPackage(packageWithFrames(33))).toThrow(/64 MiB/);
  });
  it('limits composed frame pixels even when every family reuses one encoded asset', () => {
    // This writer test needs only a container signature. Native validation
    // separately verifies actual pixels against the declared dimensions.
    const image = Buffer.from('RIFF0000WEBP'),
      hash = buildingAssetHash(image),
      assets = new Map([[hash, image]]),
      frame = { imageHash: hash, width: 512, height: 512 };
    const first = checkBuildingPackage({
      ...fixture(),
      sprites: [{ key: 'first', frames: Array.from({ length: 32 }, () => frame) }],
    });
    const second = forkBuildingPackage(first, '22222222-2222-4222-8222-222222222222');
    second.sprites[0]!.key = 'second';
    expect(() => writeBuildingArtworkBundle([first, second], assets)).not.toThrow();
    second.sprites[0]!.frames.push(frame);
    expect(() => writeBuildingArtworkBundle([first, second], assets)).toThrow(/artwork exceeds/);
    // The exact same descriptor is loaded once when shared between families.
    const shared = [
      first,
      forkBuildingPackage(first, '33333333-3333-4333-8333-333333333333'),
      forkBuildingPackage(first, '44444444-4444-4444-8444-444444444444'),
    ];
    expect(() => writeBuildingArtworkBundle(shared, assets)).not.toThrow();
  });
});
