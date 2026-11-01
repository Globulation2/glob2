// Portable library fixtures for browser interaction checks; engine behavior is tested natively.
import { randomUUID } from 'node:crypto';
import type { Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import { putContent, type BlobStore } from '@glob2/core';
import { insertBlob } from '@glob2/play';
import { simVersionKey, buildingNamespacePrefix } from '@glob2/protocol';
import { writeBuildingArchive, buildingAssetHash } from '@glob2/protocol/node';
import type { SeededHistory } from '../../api/test/historySeed.ts';
export async function seedBuildingLibrary(
  db: Kysely<Database>,
  blobs: BlobStore,
  seed: SeededHistory,
) {
  const namespace = randomUUID(),
    key = buildingNamespacePrefix(namespace) + 'kitchen';
  const archive = writeBuildingArchive(
    {
      schemaVersion: 1,
      namespace,
      experiments: [],
      sprites: [],
      variants: [
        {
          key,
          properties: {
            width: 2,
            height: 2,
            hpInit: 200,
            hpMax: 200,
            gameSprite: 'data/gfx/inn0b',
            miniSprite: 'data/gfx/miniinn0b',
          },
          semantics: { placeable: true, instantPlacement: true },
        },
      ],
    },
    new Map(),
  );
  const content = await putContent(blobs, archive);
  await insertBlob(db, content.sha256, content.size, 'application/octet-stream', 'private');
  const family = await db
    .insertInto('building_families')
    .values({
      owner_account_id: seed.accounts.mirelle,
      namespace,
      name: 'Community kitchen',
      description: 'A portable kitchen family made by players.',
      visibility: 'public',
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  const baseHash = 'ab'.repeat(32),
    sim = simVersionKey({ versionMinor: 144, netProtocol: 59, dataHash: 'cd'.repeat(32) }),
    job = randomUUID();
  await db
    .insertInto('engine_jobs')
    .values({
      id: job,
      kind: 'validate-buildings',
      sim_version: sim,
      payload: JSON.stringify({ blobHash: content.sha256, baseHash, suite: 1 }),
      status: 'succeeded',
      result: JSON.stringify({
        valid: true,
        archiveHash: content.sha256,
        baseHash,
        suite: 1,
        catalog: { snapshot: '{}', hash: buildingAssetHash(Buffer.from('{}')) },
      }),
    })
    .execute();
  await db
    .insertInto('building_releases')
    .values({
      family_id: family.id,
      archive_hash: content.sha256,
      job_id: job,
      sim_version: sim,
      base_hash: baseHash,
      suite: 1,
    })
    .execute();
}
