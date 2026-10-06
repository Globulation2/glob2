// Catalog content identity is separate from the engine version used to route jobs.
import { createHash } from 'node:crypto';
import type { BuildingCatalog } from '../matchSetup.ts';
import { simVersionKey, type SimVersion } from '../simVersion.ts';

export function checkBuildingCatalogHash(catalog: BuildingCatalog): void {
  const actual = createHash('sha256').update(catalog.snapshot, 'utf8').digest('hex');
  if (actual !== catalog.hash) throw new Error('building catalog hash does not match its snapshot');
}

/** Rating partition only; must never be used for engine-agent routing. */
export function catalogRulesVersion(engine: SimVersion, catalogHash?: string): SimVersion {
  if (!catalogHash) return engine;
  if (!/^[0-9a-f]{64}$/.test(catalogHash)) throw new Error('invalid building catalog hash');
  const dataHash = createHash('sha256')
    .update(`glob2-building-rules-v1\n${simVersionKey(engine)}\n${catalogHash}`)
    .digest('hex');
  return { ...engine, dataHash };
}
