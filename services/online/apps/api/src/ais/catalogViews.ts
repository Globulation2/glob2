import { sql, type Kysely, type Selectable } from 'kysely';
import type { Account, AisTable, AiVersionsTable, Database } from '@glob2/db';
import type { AiInfo, AiVersion } from '@glob2/protocol';

type AiRow = Selectable<AisTable>;
type VersionRow = Selectable<AiVersionsTable>;

/** Read complete pages in batches rather than issuing queries for each catalogue card. */
export function aiCatalogViews(db: Kysely<Database>, publicOrigin: string) {
  async function versions(rows: VersionRow[]): Promise<AiVersion[]> {
    if (!rows.length) return [];
    const [validations, downloads] = await Promise.all([
      db
        .selectFrom('ai_validations')
        .select(['hash', 'report'])
        .where('hash', 'in', [...new Set(rows.map((v) => v.hash))])
        .where('status', 'in', ['valid', 'invalid'])
        .orderBy('created_at', 'desc')
        .execute(),
      db
        .selectFrom('ai_downloads')
        .select(['version_id', sql<number>`count(*)::int`.as('n')])
        .where(
          'version_id',
          'in',
          rows.map((v) => v.id),
        )
        .groupBy('version_id')
        .execute(),
    ]);
    const reports = new Map<string, AiVersion['validations']>();
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
      createdAt: v.created_at.toISOString(),
      downloads: counts.get(v.id) ?? 0,
      downloadUrl: `${publicOrigin}/api/v1/ais/${v.ai_id}/versions/${v.id}/file`,
      validations: reports.get(v.hash) ?? [],
    }));
  }

  async function infos(rows: AiRow[], account?: Account): Promise<AiInfo[]> {
    if (!rows.length) return [];
    const ids = rows.map((r) => r.id);
    const [owners, latest, likes, downloads, liked, favourited] = await Promise.all([
      db
        .selectFrom('accounts')
        .select(['id', 'display_name'])
        .where('id', 'in', [...new Set(rows.map((r) => r.owner_account_id))])
        .execute(),
      db
        .selectFrom('ai_versions')
        .selectAll()
        .distinctOn('ai_id')
        .where('ai_id', 'in', ids)
        .orderBy('ai_id')
        .orderBy('created_at', 'desc')
        .orderBy('id', 'desc')
        .execute(),
      db
        .selectFrom('ai_likes')
        .select(['ai_id', sql<number>`count(*)::int`.as('n')])
        .where('ai_id', 'in', ids)
        .groupBy('ai_id')
        .execute(),
      db
        .selectFrom('ai_downloads as d')
        .innerJoin('ai_versions as v', 'v.id', 'd.version_id')
        .select(['v.ai_id', sql<number>`count(*)::int`.as('n')])
        .where('v.ai_id', 'in', ids)
        .groupBy('v.ai_id')
        .execute(),
      account
        ? db
            .selectFrom('ai_likes')
            .select('ai_id')
            .where('ai_id', 'in', ids)
            .where('account_id', '=', account.id)
            .execute()
        : Promise.resolve([]),
      account
        ? db
            .selectFrom('ai_favourites')
            .select('ai_id')
            .where('ai_id', 'in', ids)
            .where('account_id', '=', account.id)
            .execute()
        : Promise.resolve([]),
    ]);
    const ownerById = new Map(owners.map((o) => [o.id, o]));
    const versionViews = await versions(latest);
    const latestByAi = new Map(latest.map((v, i) => [v.ai_id, versionViews[i]]));
    const likesByAi = new Map(likes.map((l) => [l.ai_id, l.n]));
    const downloadsByAi = new Map(downloads.map((d) => [d.ai_id, d.n]));
    const likedIds = new Set(liked.map((l) => l.ai_id));
    const favouriteIds = new Set(favourited.map((f) => f.ai_id));
    return rows.map((row) => {
      const owner = ownerById.get(row.owner_account_id);
      const latestVersion = latestByAi.get(row.id);
      if (!owner || !latestVersion) throw new Error('AI catalogue entry has no owner or release.');
      return {
        id: row.id,
        name: row.name,
        description: row.description,
        tags: row.tags as AiInfo['tags'],
        visibility: row.visibility,
        hidden: row.hidden,
        ...(row.hidden_reason ? { hiddenReason: row.hidden_reason } : {}),
        owner: { id: owner.id, displayName: owner.display_name },
        createdAt: row.created_at.toISOString(),
        updatedAt: row.updated_at.toISOString(),
        likes: likesByAi.get(row.id) ?? 0,
        downloads: downloadsByAi.get(row.id) ?? 0,
        latestVersion,
        liked: likedIds.has(row.id),
        favourited: favouriteIds.has(row.id),
      };
    });
  }

  return {
    versions,
    infos,
    info: async (row: AiRow, account?: Account): Promise<AiInfo> => {
      const [result] = await infos([row], account);
      if (!result) throw new Error('AI catalogue entry is missing.');
      return result;
    },
  };
}
