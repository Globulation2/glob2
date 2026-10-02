// Live activity numbers for the web app's home page (GET /api/v1/stats).
// Counts only, so nothing about any one player is revealed. Each replica
// caches the answer briefly: the home page is the busiest page and these
// numbers need not be exact to the second.
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import type { InstanceStats } from '@glob2/protocol';

export const ACTIVE_WINDOW_MINUTES = 15;
const CACHE_MS = 30_000;

export async function queryInstanceStats(db: Kysely<Database>): Promise<InstanceStats> {
  const window = `${ACTIVE_WINDOW_MINUTES} minutes`;
  const row = await sql<{ online: number; live: number; today: number }>`
    SELECT
      (SELECT count(*)::int FROM (
         SELECT id AS account_id FROM accounts
           WHERE status = 'active' AND last_seen_at > now() - ${window}::interval
         UNION
         SELECT m.account_id FROM room_members m JOIN rooms r ON r.id = m.room_id
           WHERE m.connected AND r.status <> 'closed'
         UNION
         SELECT p.account_id FROM match_participants p JOIN matches x ON x.id = p.match_id
           WHERE x.status IN ('starting', 'running') AND p.account_id IS NOT NULL
       ) AS present) AS online,
      (SELECT count(*)::int FROM matches WHERE status IN ('starting', 'running')) AS live,
      (SELECT count(*)::int FROM matches
         WHERE created_at > now() - interval '24 hours' AND status <> 'cancelled') AS today
  `.execute(db);
  const counts = row.rows[0] ?? { online: 0, live: 0, today: 0 };
  return {
    playersOnline: counts.online,
    activeWindowMinutes: ACTIVE_WINDOW_MINUTES,
    liveMatches: counts.live,
    matchesToday: counts.today,
    generatedAt: new Date().toISOString(),
  };
}

/** queryInstanceStats with a short per-process cache; concurrent callers share one query. */
export function cachedInstanceStats(db: Kysely<Database>, now: () => number = Date.now) {
  let cached: { at: number; value: Promise<InstanceStats> } | undefined;
  return (): Promise<InstanceStats> => {
    if (cached && now() - cached.at < CACHE_MS) return cached.value;
    const value = queryInstanceStats(db);
    cached = { at: now(), value };
    value.catch(() => {
      if (cached?.value === value) cached = undefined;
    });
    return value;
  };
}
