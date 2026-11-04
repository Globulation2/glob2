import { it, expect } from 'vitest';
import { mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import sharp from 'sharp';
import { stockReferences } from '../src/references.ts';

it('loads camera references from the deployed layout and composites the team layer', async () => {
  const root = await mkdtemp(join(tmpdir(), 'building-camera-'));
  try {
    const directory = join(root, 'studio/building-references');
    await mkdir(directory, { recursive: true });
    const base = await sharp({
      create: { width: 2, height: 2, channels: 4, background: '#554433' },
    })
      .png()
      .toBuffer();
    const layer = await sharp({
      create: { width: 2, height: 2, channels: 4, background: '#33ff99' },
    })
      .png()
      .toBuffer();
    await writeFile(join(directory, 'inn0b0.png'), base);
    await writeFile(join(directory, 'inn0b0r.png'), layer);
    const refs = await stockReferences(root);
    expect(refs).toHaveLength(1);
    const pixel = await sharp(refs[0]!.bytes).raw().toBuffer();
    expect([...pixel.subarray(0, 3)]).toEqual([51, 255, 153]);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

it('fails clearly when the installation cannot supply any stock camera references', async () => {
  const root = await mkdtemp(join(tmpdir(), 'building-camera-'));
  try {
    await expect(stockReferences(root)).rejects.toThrow('camera references are unavailable');
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
