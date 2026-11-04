import type { FastifyInstance } from 'fastify';
import { sql } from 'kysely';
import type { AdminAnalytics } from '@glob2/protocol';
import { requireRole, type Identity } from '../identity.ts';
import { apiError } from '../errors.ts';
import { queryInstanceStats } from '../history/stats.ts';
import { listContent } from './moderation.ts';

export async function analyticsRoutes(app: FastifyInstance, identity: Identity) {
  const { db, config } = app.services;
  const cache = new Map<number, { at: number; value: Promise<AdminAnalytics> }>();
  app.get<{ Querystring: { days?: string } }>('/api/v1/admin/analytics', async (r) => {
    await requireRole(identity, r, 'admin');
    if (config.instance.analytics?.display === false)
      throw apiError('unavailable', 'Analytics display is disabled.');
    const days = Number(r.query.days ?? 30);
    if (![7, 30, 90].includes(days)) throw apiError('bad_request', 'Choose 7, 30 or 90 days.');
    const cached = cache.get(days);
    if (cached && Date.now() - cached.at < 60000) return cached.value;
    const value = (async (): Promise<AdminAnalytics> => {
      const today = sql`(now() AT TIME ZONE 'UTC')::date`;
      const currentStart = sql`(${today}-${days - 1}::int)::timestamp AT TIME ZONE 'UTC'`;
      const previousStart = sql`(${today}-${2 * days - 1}::int)::timestamp AT TIME ZONE 'UTC'`;
      const tomorrow = sql`(${today}+1)::timestamp AT TIME ZONE 'UTC'`;
      const metrics = (
        await sql<
          AdminAnalytics['metrics'][number]
        >`SELECT day::text,metric,dimension,value FROM admin_daily_metrics
          WHERE day BETWEEN ${today}-${2 * days - 1}::int AND ${today}
          ORDER BY day,metric,dimension`.execute(db)
      ).rows;
      const countActivity = async (kind?: 'registered' | 'guest') =>
        (
          await sql<{ daily: number; weekly: number; monthly: number }>`
          SELECT count(DISTINCT account_id) FILTER (WHERE day=${today})::int AS daily,
            count(DISTINCT account_id) FILTER (WHERE day>=${today}-6)::int AS weekly,
            count(DISTINCT account_id)::int AS monthly
          FROM account_activity_days WHERE day BETWEEN ${today}-29 AND ${today}
            ${kind ? sql`AND kind=${kind}` : sql``}`.execute(db)
        ).rows[0] ?? { daily: 0, weekly: 0, monthly: 0 };
      const [total, registered, guests] = await Promise.all([
        countActivity(),
        countActivity('registered'),
        countActivity('guest'),
      ]);
      const active = { ...total, registered, guests };
      const participants = (
        await sql<AdminAnalytics['participants']>`SELECT
            count(DISTINCT p.account_id) FILTER (WHERE m.created_at>=${currentStart})::int AS current,
            count(DISTINCT p.account_id) FILTER (WHERE m.created_at<${currentStart})::int AS previous
          FROM match_participants p JOIN matches m ON m.id=p.match_id
          WHERE m.created_at>=${previousStart} AND m.created_at<${tomorrow}`.execute(db)
      ).rows[0] ?? { current: 0, previous: 0 };
      const coverage = await db
        .selectFrom('admin_metric_coverage')
        .select(['metric', 'since', 'historical_incomplete as historicalIncomplete'])
        .orderBy('metric')
        .execute();
      const top = await listContent(db, { limit: '10', hidden: 'all', sort: 'downloads' });
      const attention = (
        await sql<{ reports: number; uncertain: number; failedJobs: number }>`SELECT
       (SELECT count(*)::int FROM map_reports WHERE status='open')+(SELECT count(*)::int FROM ai_reports WHERE status='open')+(SELECT count(*)::int FROM building_reports WHERE NOT resolved)+(SELECT count(*)::int FROM set_reports WHERE NOT resolved)+(SELECT count(*)::int FROM colony_skin_reports WHERE resolution IS NULL)+(SELECT count(*)::int FROM music_reports WHERE NOT resolved) AS reports,
       (SELECT count(*)::int FROM studio_requests WHERE status='uncertain')+(SELECT count(*)::int FROM music_studio_requests WHERE status='uncertain')+(SELECT count(*)::int FROM terrain_studio_requests WHERE status='uncertain')+(SELECT count(*)::int FROM building_studio_requests WHERE status='uncertain')+(SELECT count(*)::int FROM generator_studio_calls WHERE status='uncertain')+(SELECT count(*)::int FROM ai_studio_calls WHERE status='uncertain')+(SELECT count(*)::int FROM hive_calls WHERE status='uncertain') AS uncertain,
       (SELECT count(*)::int FROM engine_jobs WHERE status='failed') AS "failedJobs"`.execute(db)
      ).rows[0] ?? { reports: 0, uncertain: 0, failedJobs: 0 };
      return {
        days,
        attention,
        generatedAt: new Date().toISOString(),
        live: await queryInstanceStats(
          db,
          config.instance.queues.map((q) => q.id),
        ),
        metrics,
        active,
        participants,
        coverage,
        topContent: top.items
          .sort((a, b) => (b.downloads ?? -1) - (a.downloads ?? -1))
          .slice(0, 10),
        collection: config.instance.analytics?.collection !== false,
      };
    })();
    cache.set(days, { at: Date.now(), value });
    value.catch(() => {
      if (cache.get(days)?.value === value) cache.delete(days);
    });
    return value;
  });
}
