import { it, expect } from 'vitest';
import sharp from 'sharp';
import { processArtwork } from '../src/artwork.ts';
import { buildingAssetHash, writeBuildingArchive, readBuildingArchive } from '@glob2/protocol/node';
import { assemble, newBuildingPackage, type BuildingPlan } from '@glob2/building-studio';
import { randomUUID } from 'node:crypto';
it('creates aligned game and miniature frames with stable normalized bytes and round trips', async () => {
  const source = await sharp({
    create: { width: 200, height: 200, channels: 4, background: { r: 0, g: 0, b: 0, alpha: 0 } },
  })
    .composite([
      {
        input: await sharp({
          create: { width: 70, height: 110, channels: 4, background: '#876543' },
        })
          .png()
          .toBuffer(),
        left: 60,
        top: 30,
      },
    ])
    .png()
    .toBuffer();
  const a = await processArtwork(source, 'building', 2, 2),
    b = await processArtwork(source, 'building', 2, 2);
  expect(a.game).toEqual(b.game);
  expect(a.game.frames[0]).toMatchObject({ width: 64, height: 96 });
  expect(a.mini.frames[0]).toMatchObject({ width: 32, height: 48 });
  for (const [hash, bytes] of a.assets) {
    expect(buildingAssetHash(bytes)).toBe(hash);
    expect((await sharp(bytes).metadata()).hasAlpha).toBe(true);
  }
  const base = newBuildingPackage(randomUUID()),
    plan: BuildingPlan = {
      action: 'build',
      scope: 'appearance',
      text: 'Art',
      brief: '',
      title: 'Art',
      experimentsJson: null,
      entries: [
        {
          key: 'building',
          operation: 'upsert',
          next: null,
          previous: null,
          requiredExperiment: null,
          propertiesJson: '{}',
          semanticsJson: '{}',
          presentationJson: '{}',
          regenerateArt: true,
          teamColor: false,
          artPrompt: 'Hospital',
        },
      ],
    };
  const pack = assemble(base, plan, { [base.variants[0]!.key]: a });
  const archive = writeBuildingArchive(pack, a.assets);
  expect(readBuildingArchive(archive).package).toEqual(pack);
  expect(writeBuildingArchive(pack, a.assets)).toEqual(archive);
});
it('rejects opaque, empty and oversized requested artwork', async () => {
  const source = await sharp({
    create: { width: 16, height: 16, channels: 4, background: '#abcdef' },
  })
    .png()
    .toBuffer();
  await expect(processArtwork(source, 'x', 2, 2)).rejects.toThrow('transparent background');
  await expect(processArtwork(source, 'x', 99, 2)).rejects.toThrow('footprint');
});
it('extracts team-color accents into matching mint-base layers without losing alpha', async () => {
  const body = await sharp({
    create: { width: 70, height: 110, channels: 4, background: '#876543' },
  })
    .png()
    .toBuffer();
  const accent = await sharp({
    create: { width: 20, height: 40, channels: 4, background: '#ff00ff' },
  })
    .png()
    .toBuffer();
  const source = await sharp({
    create: { width: 200, height: 200, channels: 4, background: { r: 0, g: 0, b: 0, alpha: 0 } },
  })
    .composite([
      { input: body, left: 60, top: 30 },
      { input: accent, left: 80, top: 50 },
    ])
    .png()
    .toBuffer();
  const art = await processArtwork(source, 'building', 2, 2, true);
  for (const sprite of [art.game, art.mini]) {
    const frame = sprite.frames[0]!;
    expect(frame.teamColorHash).toMatch(/^[a-f0-9]{64}$/);
    const layer = await sharp(art.assets.get(frame.teamColorHash!)!)
      .raw()
      .toBuffer({ resolveWithObject: true });
    expect(layer.info).toMatchObject({ width: frame.width, height: frame.height });
    let colored = false;
    for (let at = 0; at < layer.data.length; at += 4)
      if (layer.data[at + 3]) {
        colored = true;
        expect(layer.data[at + 1]).toBeGreaterThan(layer.data[at]!);
        expect(layer.data[at + 2]).toBeGreaterThan(layer.data[at]!);
      }
    expect(colored).toBe(true);
  }
});
it('retains a tiny team-color accent when miniature colors would blend it away', async () => {
  // Exact game-size input isolates the miniature resampling from fitting. This
  // single magenta pixel used to pass game extraction but fail miniature extraction.
  const pixels = Buffer.alloc(64 * 96 * 4);
  for (let y = 2; y < 94; y++)
    for (let x = 2; x < 62; x++) {
      const at = (y * 64 + x) * 4;
      pixels.set([128, 128, 128, 255], at);
    }
  pixels.set([255, 0, 255, 255], (40 * 64 + 31) * 4);
  const source = await sharp(pixels, { raw: { width: 64, height: 96, channels: 4 } })
    .png()
    .toBuffer();
  const artwork = await processArtwork(source, 'tiny-accent', 2, 2, true);
  for (const sprite of [artwork.game, artwork.mini]) {
    const frame = sprite.frames[0]!;
    expect(frame.teamColorHash).toBeDefined();
    const mask = await sharp(artwork.assets.get(frame.teamColorHash!)!)
      .ensureAlpha()
      .raw()
      .toBuffer();
    expect(mask.some((value, index) => index % 4 === 3 && value > 0)).toBe(true);
  }
});
