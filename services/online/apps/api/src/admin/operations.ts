import type { FastifyInstance } from 'fastify';
import { sql } from 'kysely';
import { Credits, HiveError, CREDIT_PRODUCTS, meteredUsage } from '@glob2/billing';
import { AdminUsageReconcile, type AdminOperation } from '@glob2/protocol';
import { requireRole, type Identity } from '../identity.ts';
import { body } from '../http/validate.ts';
import { apiError } from '../errors.ts';
import { cursorTimeSql } from '../http/cursorTime.ts';
import { decodeCursor, encodeCursor, pageSize } from './cursor.ts';

const REQUESTS = [
  ['maps', 'studio_requests'],
  ['music', 'music_studio_requests'],
  ['terrain', 'terrain_studio_requests'],
  ['buildings', 'building_studio_requests'],
] as const;
export async function operationsRoutes(app: FastifyInstance, identity: Identity) {
  const { db } = app.services;
  app.get<{ Querystring: { cursor?: string; limit?: string; product?: string } }>(
    '/api/v1/admin/operations',
    async (r) => {
      const before = decodeCursor(r.query.cursor),
        limit = pageSize(r.query.limit);
      await requireRole(identity, r, 'admin');
      const requests = REQUESTS.map(
        ([product, table]) =>
          sql`SELECT id::text,${product}::text AS product,account_id AS "accountId",status,created_at AS "createdAt",CASE WHEN kind='generate' THEN 1 ELSE 0 END::int AS reserved,kind,'Provider outcome needs review'::text AS error FROM ${sql.table(table)} WHERE status='uncertain'`,
      );
      const rows = (
        await sql<
          AdminOperation & { cursorAt: string }
        >`SELECT *,${cursorTimeSql(sql<Date>`"createdAt"`)} AS "cursorAt" FROM (${sql.join(requests, sql` UNION ALL `)} UNION ALL
   SELECT c.id::text,'aiStudio',c.account_id,c.status,c.created_at,c.reserved::int,'usage','Provider usage needs review' FROM ai_studio_calls c WHERE c.status='uncertain' UNION ALL
   SELECT c.id::text,'generatorStudio',c.account_id,c.status,c.created_at,c.reserved::int,'usage','Provider usage needs review' FROM generator_studio_calls c WHERE c.status='uncertain' UNION ALL
   SELECT c.id::text,'hive',c.account_id,c.status,c.created_at,c.reserved::int,'usage','Provider usage needs review' FROM hive_calls c WHERE c.status='uncertain' UNION ALL
   SELECT id::text,'engine',NULL::uuid,status,created_at,0,kind,CASE WHEN status='failed' THEN 'Engine job failed; inspect verification or library validation' ELSE NULL END FROM engine_jobs WHERE status IN ('queued','failed')) x WHERE TRUE ${r.query.product ? sql`AND product=${r.query.product}` : sql``} ${before ? sql`AND ("createdAt",product || ':' || id::text)>(${before.exactAt},${before.id})` : sql``} ORDER BY "createdAt" ASC,product || ':' || id::text ASC LIMIT ${limit + 1}`.execute(
          db,
        )
      ).rows;
      const agents = (
        await db
          .selectFrom('engine_agents')
          .select(['id', 'last_seen_at as lastSeenAt'])
          .orderBy('last_seen_at', 'desc')
          .limit(100)
          .execute()
      ).map((a) => ({ ...a, lastSeenAt: a.lastSeenAt.toISOString() }));
      const age =
        (
          await sql<{
            age: number | null;
          }>`SELECT extract(epoch FROM now()-min(created_at))::float8 AS age FROM engine_jobs WHERE status='queued'`.execute(
            db,
          )
        ).rows[0]?.age ?? null;
      const workers = (
        await db
          .selectFrom('leader_leases')
          .select(['name', 'holder', 'renewed_at as renewedAt'])
          .execute()
      ).map((w) => ({ ...w, renewedAt: w.renewedAt.toISOString() }));
      const page = rows.slice(0, limit),
        last = page.at(-1);
      const reservedCredits = await Promise.all(
        Object.entries(CREDIT_PRODUCTS).map(async ([product, { prefix }]) => ({
          product,
          reserved:
            (
              await sql<{
                reserved: number;
              }>`SELECT coalesce(sum(reserved),0)::float8 AS reserved FROM ${sql.table(prefix + '_wallets')}`.execute(
                db,
              )
            ).rows[0]?.reserved ?? 0,
        })),
      );
      return {
        reservedCredits,
        workers,
        ...(rows.length > limit && last
          ? { nextCursor: encodeCursor(last.cursorAt, last.product + ':' + last.id) }
          : {}),
        items: page.map(({ cursorAt, ...row }) => ({
          ...row,
          createdAt: new Date(cursorAt).toISOString(),
        })),
        agents,
        queueAgeSeconds: age === null ? null : Math.max(0, age),
      };
    },
  );
  app.get<{ Params: { product: string; id: string } }>(
    '/api/v1/admin/operations/:product/:id',
    async (r) => {
      await requireRole(identity, r, 'admin');
      const spec = REQUESTS.find(([p]) => p === r.params.product);
      const metered = ['aiStudio', 'generatorStudio', 'hive'].includes(r.params.product);
      if (!spec && !metered && r.params.product !== 'engine')
        throw apiError('bad_request', 'Unknown product.');
      if (
        (spec ||
          r.params.product === 'engine' ||
          ['aiStudio', 'generatorStudio'].includes(r.params.product)) &&
        !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(r.params.id)
      )
        throw apiError('bad_request', 'Invalid request identifier.');
      const table = spec
        ? spec[1]
        : r.params.product === 'engine'
          ? 'engine_jobs'
          : r.params.product === 'hive'
            ? 'hive_calls'
            : r.params.product === 'generatorStudio'
              ? 'generator_studio_calls'
              : 'ai_studio_calls';
      const row = (
        await sql<{
          id: string;
          status: string;
          createdAt: Date;
          completedAt: Date | null;
          reserved: number;
          charged: number | null;
          usage: unknown;
        }>`SELECT id::text,status,created_at AS "createdAt",completed_at AS "completedAt",${metered ? sql`reserved::float8` : spec ? sql`CASE WHEN kind='generate' AND status NOT IN ('ready','failed') THEN 1 ELSE 0 END::float8` : sql`0::float8`} AS reserved,${metered ? sql`charged::float8` : sql`NULL::float8`} AS charged,${metered ? sql`coalesce(nullif(usage,'null'::jsonb),(SELECT a.usage FROM admin_provider_attempts a WHERE a.product=${r.params.product} AND a.attempt_id=id::text))` : sql`NULL::jsonb`} AS usage FROM ${sql.table(table)} WHERE id::text=${r.params.id}`.execute(
          db,
        )
      ).rows[0];
      if (!row) throw apiError('not_found', 'Request not found.');
      // Failed/late responses retain metering independently from their private output.
      const evidence = spec
        ? (
            await sql<{
              model: string;
              stage: string;
              status: string;
              usage: unknown;
              createdAt: Date;
            }>`SELECT a.model,a.stage,a.status,coalesce(nullif(a.output->'usage','null'::jsonb),j.usage) AS usage,a.created_at AS "createdAt" FROM ${sql.table(spec[1].replace('requests', 'attempts'))} a LEFT JOIN admin_provider_attempts j ON j.product=${spec[0]} AND j.attempt_id=a.id::text WHERE a.request_id::text=${r.params.id} ORDER BY a.created_at DESC,a.id DESC LIMIT 50`.execute(
              db,
            )
          ).rows
        : metered
          ? (
              await sql<{
                model: string;
                stage: string;
                status: string;
                usage: unknown;
                createdAt: Date;
              }>`SELECT coalesce(c.rate->>'model','unknown') AS model,'usage'::text AS stage,c.status,coalesce(nullif(c.usage,'null'::jsonb),j.usage) AS usage,c.created_at AS "createdAt" FROM ${sql.table(table)} c LEFT JOIN admin_provider_attempts j ON j.product=${r.params.product} AND j.attempt_id=c.id::text WHERE c.id::text=${r.params.id}`.execute(
                db,
              )
            ).rows
          : [];
      const attempts = evidence.map((a) => ({
        ...a,
        usage: meteredUsage(a.usage) ?? null,
        createdAt: a.createdAt.toISOString(),
      }));
      return {
        ...row,
        product: r.params.product,
        createdAt: row.createdAt.toISOString(),
        completedAt: row.completedAt?.toISOString() ?? null,
        usage: meteredUsage(row.usage) ?? null,
        attempts,
        creditConsequence: metered
          ? 'Verified usage is charged up to the original reservation. The rest is released.'
          : row.reserved
            ? 'Marking an unrecoverable request failed returns one reserved generation credit.'
            : 'No generation credit is reserved.',
      };
    },
  );
  app.post<{ Params: { id: string } }>('/api/v1/admin/hive/calls/:id/reconcile', async (r) => {
    const { account } = await requireRole(identity, r, 'admin'),
      input = body(AdminUsageReconcile, r.body);
    const row = (
      await sql<{
        account_id: string;
      }>`SELECT account_id FROM hive_calls WHERE id=${r.params.id} AND status='uncertain'`.execute(
        db,
      )
    ).rows[0];
    if (!row) throw apiError('conflict', 'Only uncertain calls can be reconciled.');
    try {
      const charged = await new Credits(db).reconcile(
        row.account_id,
        r.params.id,
        input.usage,
        input.evidence,
        {
          actor: account.id,
          action: 'hive.reconcile',
          targetType: 'hive-call',
          targetId: r.params.id,
        },
      );
      return { charged };
    } catch (error) {
      if (error instanceof HiveError) throw apiError('conflict', error.message);
      throw error;
    }
  });
}
