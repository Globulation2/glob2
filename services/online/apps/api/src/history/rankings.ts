import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import { parseSimVersionKey, type AiId, type LeaderboardEntry } from '@glob2/protocol';
import { displayRating, PROVISIONAL_SIGMA } from '@glob2/play';
import { avatarUrl } from '../avatars/urls.ts';
import type { ParticipantFilter } from './players.ts';

function present<T>(value: T | null | undefined): T {
  if (value === null || value === undefined) throw new Error('Incomplete public ranking row');
  return value;
}
/** Shared ordering for the new leaderboard and overall profile ranks. */
export function rankedParticipants(
  ladders: string[],
  versions: string[],
  kind: ParticipantFilter,
  settled = false,
) {
  return sql<{
    rank: string;
    ladder: string;
    entity_id: string;
    kind: 'account' | 'ai';
    account_id: string | null;
    display_name: string | null;
    created_at: Date | null;
    avatar_revision: number | null;
    ai_id: AiId | null;
    ai_sim_version: string | null;
    mu: number;
    sigma: number;
    games: number;
    wins: number;
  }>`SELECT row_number() OVER (PARTITION BY r.ladder ORDER BY r.ordinal DESC, r.games DESC,
        CASE WHEN e.kind = 'account' THEN e.account_id::text ELSE e.ai_id || ':' || e.ai_sim_version END) AS rank, r.ladder,
      e.id AS entity_id, e.kind, e.account_id, a.display_name, a.created_at, a.avatar_revision,
      e.ai_id, e.ai_sim_version, r.mu, r.sigma, r.games, r.wins
    FROM ratings r JOIN rating_entities e ON e.id = r.entity_id
    LEFT JOIN accounts a ON a.id = e.account_id
    WHERE r.ladder = ANY(${ladders}::text[]) AND r.games > 0
      AND (${settled}::boolean IS FALSE OR r.sigma <= ${PROVISIONAL_SIGMA})
      AND ((e.kind = 'account' AND ${kind !== 'ai'} AND a.kind = 'registered' AND a.status = 'active')
        OR (e.kind = 'ai' AND ${kind !== 'humans'} AND e.ai_sim_version = ANY(${versions}::text[])))`;
}
export function leaderboardEntry(
  row: Awaited<ReturnType<ReturnType<typeof rankedParticipants>['execute']>>['rows'][number],
): LeaderboardEntry {
  const entity: LeaderboardEntry['entity'] =
    row.kind === 'account'
      ? {
          kind: 'account',
          account: {
            id: present(row.account_id),
            displayName: present(row.display_name),
            kind: 'registered',
            createdAt: present(row.created_at).toISOString(),
            avatarUrl: avatarUrl(present(row.account_id), row.avatar_revision ?? 0),
          },
        }
      : {
          kind: 'ai',
          ai: present(row.ai_id),
          simVersion: present(parseSimVersionKey(present(row.ai_sim_version))),
        };
  return {
    rank: Number(row.rank),
    entity,
    rating: Math.round(displayRating(row) * 10) / 10,
    mu: row.mu,
    sigma: row.sigma,
    games: row.games,
    wins: row.wins,
    provisional: row.sigma > PROVISIONAL_SIGMA,
  };
}

/** Read all profile ranks in one query, using the leaderboard's exact ordering. */
export async function overallRanks(
  db: Kysely<Database>,
  ladders: string[],
  versions: string[],
  participant: { accountId: string } | { entityId: string },
): Promise<Map<string, number>> {
  if (!ladders.length) return new Map();
  const filter =
    'accountId' in participant
      ? sql`account_id = ${participant.accountId}`
      : sql`entity_id = ${participant.entityId}`;
  const result = await sql<{ ladder: string; rank: string }>`SELECT ladder, rank FROM
    (${rankedParticipants(ladders, versions, 'all')}) ranked WHERE ${filter}`.execute(db);
  return new Map(result.rows.map((row) => [row.ladder, Number(row.rank)]));
}
