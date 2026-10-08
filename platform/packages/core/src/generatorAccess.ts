import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import {
  GENERATOR_VALIDATION_SUITE,
  passedGeneratorReport,
  type ScriptGeneratorDescriptor,
} from '@glob2/protocol';
/** Recheck even on a cache hit. The host's access authorizes output, never package sharing. */
export async function usableGenerator(
  db: Kysely<Database>,
  descriptor: ScriptGeneratorDescriptor,
  hostId: string,
  simVersion: string,
) {
  const row = await db
    .selectFrom('generator_versions as v')
    .innerJoin('generators as g', 'g.id', 'v.generator_id')
    .select([
      'v.hash',
      'v.package_hash',
      'v.metadata',
      'g.owner_account_id',
      'g.visibility',
      'g.hidden',
      'g.deleted_at',
      'v.example',
      'v.source_hash',
    ])
    .where('v.id', '=', descriptor.versionId)
    .where('g.id', '=', descriptor.libraryId)
    .executeTakeFirst();
  if (
    !row ||
    row.deleted_at ||
    row.hidden ||
    (row.visibility === 'private' && row.owner_account_id !== hostId)
  )
    return undefined;
  if (
    row.hash !== descriptor.fileHash ||
    row.package_hash !== descriptor.packageHash ||
    row.metadata.id !== descriptor.generatorId ||
    row.metadata.revision !== descriptor.revision ||
    row.metadata.editorOnly
  )
    return undefined;
  const validation = await db
    .selectFrom('generator_validations')
    .select('report')
    .where('hash', '=', row.source_hash)
    .where('sim_version', '=', simVersion)
    .where('suite', '=', GENERATOR_VALIDATION_SUITE)
    .where('status', '=', 'valid')
    .where(sql<boolean>`example=${JSON.stringify(row.example)}::jsonb`)
    .executeTakeFirst();
  return validation &&
    passedGeneratorReport(validation.report) &&
    validation.report.packageHash === descriptor.packageHash
    ? row
    : undefined;
}
