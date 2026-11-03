import { isDeepStrictEqual } from 'node:util';
import { sql, type Kysely, type Selectable } from 'kysely';
import type { Account, GeneratorsTable, GeneratorVersionsTable, Database } from '@glob2/db';
import type { GeneratorInfo, GeneratorVersion } from '@glob2/protocol';

type GeneratorRow = Selectable<GeneratorsTable>;
type VersionRow = Selectable<GeneratorVersionsTable>;

/** Read complete pages in batches rather than issuing queries for each catalogue card. */
export function generatorCatalogViews(db: Kysely<Database>, publicOrigin: string) {
  async function versions(rows: VersionRow[]): Promise<GeneratorVersion[]> {
    if (!rows.length) return [];
    const [validations, downloads] = await Promise.all([
      db
        .selectFrom('generator_validations')
        .select(['hash', 'example', 'report'])
        .where('hash', 'in', [...new Set(rows.map((v) => v.source_hash))])
        .where('status', 'in', ['valid', 'invalid'])
        .orderBy('created_at', 'desc')
        .execute(),
      db
        .selectFrom('generator_downloads')
        .select(['version_id', sql<number>`count(*)::int`.as('n')])
        .where(
          'version_id',
          'in',
          rows.map((v) => v.id),
        )
        .groupBy('version_id')
        .execute(),
    ]);
    const reports = new Map<string, GeneratorVersion['validations']>();
    for (const v of validations) {
      const list = reports.get(v.hash) ?? [];
      list.push(v.report);
      reports.set(v.hash, list);
    }
    const counts = new Map(downloads.map((d) => [d.version_id, d.n]));
    return rows.map((v) => ({
      id: v.id,
      hash: v.hash,
      label: v.label,
      notes: v.notes,
      profile: v.profile,
      packageHash: v.package_hash,
      metadata: v.metadata,
      example: v.example,
      createdAt: v.created_at.toISOString(),
      downloads: counts.get(v.id) ?? 0,
      downloadUrl: `${publicOrigin}/api/v1/generators/${v.generator_id}/versions/${v.id}/file`,
      validations: (reports.get(v.source_hash) ?? []).filter(
        (r) => r.samples[0] && isDeepStrictEqual(r.samples[0].settings, v.example),
      ),
    }));
  }

  async function infos(rows: GeneratorRow[], account?: Account): Promise<GeneratorInfo[]> {
    if (!rows.length) return [];
    const ids = rows.map((r) => r.id);
    const [owners, latest, likes, downloads, liked, favourited] = await Promise.all([
      db
        .selectFrom('accounts')
        .select(['id', 'display_name'])
        .where('id', 'in', [...new Set(rows.map((r) => r.owner_account_id))])
        .execute(),
      db
        .selectFrom('generator_versions')
        .selectAll()
        .distinctOn('generator_id')
        .where('generator_id', 'in', ids)
        .orderBy('generator_id')
        .orderBy('revision', 'desc')
        .orderBy('id', 'desc')
        .execute(),
      db
        .selectFrom('generator_likes')
        .select(['generator_id', sql<number>`count(*)::int`.as('n')])
        .where('generator_id', 'in', ids)
        .groupBy('generator_id')
        .execute(),
      db
        .selectFrom('generator_downloads as d')
        .innerJoin('generator_versions as v', 'v.id', 'd.version_id')
        .select(['v.generator_id', sql<number>`count(*)::int`.as('n')])
        .where('v.generator_id', 'in', ids)
        .groupBy('v.generator_id')
        .execute(),
      account
        ? db
            .selectFrom('generator_likes')
            .select('generator_id')
            .where('generator_id', 'in', ids)
            .where('account_id', '=', account.id)
            .execute()
        : Promise.resolve([]),
      account
        ? db
            .selectFrom('generator_favourites')
            .select('generator_id')
            .where('generator_id', 'in', ids)
            .where('account_id', '=', account.id)
            .execute()
        : Promise.resolve([]),
    ]);
    const ownerById = new Map(owners.map((o) => [o.id, o]));
    const versionViews = await versions(latest);
    const latestByGenerator = new Map(latest.map((v, i) => [v.generator_id, versionViews[i]]));
    const likesByGenerator = new Map(likes.map((l) => [l.generator_id, l.n]));
    const downloadsByGenerator = new Map(downloads.map((d) => [d.generator_id, d.n]));
    const likedIds = new Set(liked.map((l) => l.generator_id));
    const favouriteIds = new Set(favourited.map((f) => f.generator_id));
    return rows.map((row) => {
      const owner = ownerById.get(row.owner_account_id);
      const latestVersion = latestByGenerator.get(row.id);
      if (!owner || !latestVersion)
        throw new Error('generator catalogue entry has no owner or release.');
      return {
        id: row.id,
        name: row.name,
        description: row.description,
        tags: row.tags as GeneratorInfo['tags'],
        visibility: row.visibility,
        hidden: row.hidden,
        ...(row.hidden_reason ? { hiddenReason: row.hidden_reason } : {}),
        owner: { id: owner.id, displayName: owner.display_name },
        createdAt: row.created_at.toISOString(),
        updatedAt: row.updated_at.toISOString(),
        likes: likesByGenerator.get(row.id) ?? 0,
        downloads: downloadsByGenerator.get(row.id) ?? 0,
        latestVersion,
        liked: likedIds.has(row.id),
        favourited: favouriteIds.has(row.id),
      };
    });
  }

  return {
    versions,
    infos,
    info: async (row: GeneratorRow, account?: Account): Promise<GeneratorInfo> => {
      const [result] = await infos([row], account);
      if (!result) throw new Error('generator catalogue entry is missing.');
      return result;
    },
  };
}
