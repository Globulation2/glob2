// Extra rows for the browser smoke test on top of the history seed: catalog
// maps with real engine-rendered previews (fixtures/maps, made with
// `glob2 map generate <generator> --preview <png> --preview-size 384`), and
// an open room so the invite page (/j/<code>) has something to join.
import { randomBytes } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import type { Kysely } from 'kysely';
import { putContent, type BlobStore } from '@glob2/core';
import type { Database } from '@glob2/db';
import { simVersionKey } from '@glob2/protocol';
import type { SeededHistory } from '../../api/test/historySeed.ts';

const FIXTURES = join(import.meta.dirname, 'fixtures/maps');

export const INVITE_CODE = 'GLOBJOIN';

/** The real preview of a generated map, for the history seed's catalog map. */
export function previewFixture(name: string) {
  return { png: readFileSync(join(FIXTURES, `${name}.png`)), width: 384, height: 384 };
}

const MAPS = [
  { file: 'isles', title: 'Isles of Plenty', owner: 'ana', teams: 4, likes: 14, plays: 52 },
  { file: 'canals', title: 'Canal Country', owner: 'mirelle', teams: 4, likes: 9, plays: 31 },
  {
    file: 'honeycomb-isle',
    title: 'Honeycomb Isle',
    owner: 'bradley',
    teams: 4,
    likes: 6,
    plays: 18,
  },
  { file: 'crater-lakes', title: 'Crater Lakes', owner: 'mirelle', teams: 4, likes: 4, plays: 11 },
  { file: 'river', title: 'Long River', owner: 'ana', teams: 4, likes: 2, plays: 7 },
  {
    file: 'symmetric-arena',
    title: 'Symmetric Arena',
    owner: 'bradley',
    teams: 4,
    likes: 0,
    plays: 3,
  },
] as const;

export const SHOWCASE_MAP_COUNT = MAPS.length;

async function blob(db: Kysely<Database>, blobs: BlobStore, bytes: Uint8Array, type: string) {
  const content = await putContent(blobs, bytes);
  await db
    .insertInto('blobs')
    .values({
      sha256: content.sha256,
      size: content.size,
      content_type: type,
      storage_key: content.key,
      visibility: 'private',
    })
    .onConflict((oc) => oc.doNothing())
    .execute();
  return content;
}

export async function seedShowcase(
  db: Kysely<Database>,
  blobs: BlobStore,
  seed: SeededHistory,
): Promise<void> {
  for (const [i, entry] of MAPS.entries()) {
    const owner = seed.accounts[entry.owner];
    const file = await blob(
      db,
      blobs,
      Buffer.from(`showcase map ${entry.file} ${randomBytes(8).toString('hex')}`),
      'application/x-glob2-map',
    );
    const preview = await blob(
      db,
      blobs,
      readFileSync(join(FIXTURES, `${entry.file}.png`)),
      'image/png',
    );
    const map = await db
      .insertInto('maps')
      .values({
        owner_account_id: owner,
        title: entry.title,
        description: `Generated with the ${entry.file} map generator.`,
        visibility: 'public',
        made_with: 'generator',
        play_count: entry.plays,
        download_count: entry.plays * 2,
        like_count: entry.likes,
        created_at: new Date(Date.now() - (i + 2) * 86_400_000),
        updated_at: new Date(Date.now() - (i + 2) * 86_400_000),
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    const version = await db
      .insertInto('map_versions')
      .values({
        map_id: map.id,
        hash: file.sha256,
        size: file.size,
        width: 128,
        height: 128,
        team_count: entry.teams,
        min_version_minor: seed.sim.versionMinor,
        sim_version: simVersionKey(seed.sim),
        validation: 'valid',
        preview_hash: preview.sha256,
        preview_status: 'ready',
        preview_width: 384,
        preview_height: 384,
        file_title: entry.title,
        uploader_account_id: owner,
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    await db
      .updateTable('maps')
      .set({ latest_version_id: version.id })
      .where('id', '=', map.id)
      .execute();
  }

  await db
    .insertInto('rooms')
    .values({
      code: INVITE_CODE,
      name: 'Saturday night colony',
      visibility: 'link',
      status: 'open',
      host_account_id: seed.accounts.mirelle,
      sim_version: simVersionKey(seed.sim),
      settings: '{}',
    })
    .execute();
}
