import type { FastifyInstance } from 'fastify';
import { sql } from 'kysely';
import { CREDIT_PRODUCTS } from '@glob2/billing';
import type { AdminFinances } from '@glob2/protocol';
import { requireRole, type Identity } from '../identity.ts';
import { apiError } from '../errors.ts';

export async function financeRoutes(app: FastifyInstance, identity: Identity) {
  const { db, config } = app.services;
  for (const rate of config.instance.analytics?.providerRates ?? []) {
    await sql`INSERT INTO admin_provider_rates(version,model,currency,effective_at,input_micros,cached_input_micros,output_micros,call_micros) VALUES(${rate.version},${rate.model},${rate.currency},${new Date(rate.effectiveAt)},${rate.inputMicros},${rate.cachedInputMicros},${rate.outputMicros},${rate.callMicros}) ON CONFLICT DO NOTHING`.execute(
      db,
    );
    const old = await db
      .selectFrom('admin_provider_rates')
      .selectAll()
      .where('version', '=', rate.version)
      .where('model', '=', rate.model)
      .executeTakeFirstOrThrow();
    if (
      old.currency !== rate.currency ||
      old.effective_at.toISOString() !== new Date(rate.effectiveAt).toISOString() ||
      old.input_micros !== rate.inputMicros ||
      old.cached_input_micros !== rate.cachedInputMicros ||
      old.output_micros !== rate.outputMicros ||
      old.call_micros !== rate.callMicros
    )
      throw new Error('Provider monetary rate versions are immutable; use a new version.');
  }
  const cache = new Map<string, { at: number; value: Promise<AdminFinances> }>();
  app.get<{ Querystring: { days?: string; mode?: string } }>(
    '/api/v1/admin/finances',
    async (r) => {
      await requireRole(identity, r, 'admin');
      if (config.instance.analytics?.display === false)
        throw apiError('unavailable', 'Analytics display is disabled.');
      const days = Number(r.query.days ?? 30),
        mode = r.query.mode ?? 'live';
      if (![7, 30, 90].includes(days) || !['live', 'test', 'unclassified'].includes(mode))
        throw apiError('bad_request', 'Invalid range or payment mode.');
      const key = days + mode,
        cached = cache.get(key);
      if (cached && Date.now() - cached.at < 60000) return cached.value;
      const cutoff = sql`((now() AT TIME ZONE 'UTC')::date-${days - 1}::int)::timestamp AT TIME ZONE 'UTC'`,
        start = sql`((now() AT TIME ZONE 'UTC')::date-${2 * days - 1}::int)::timestamp AT TIME ZONE 'UTC'`;
      const value = (async (): Promise<AdminFinances> => {
        const cash = (
          await sql<
            AdminFinances['cash'][number]
          >`SELECT product,currency,mode,CASE WHEN occurred_at>=${cutoff} THEN 'current' ELSE 'previous' END AS period,kind,sum(amount)::float8 AS amount,count(*)::int AS events FROM admin_financial_events WHERE mode=${mode} AND occurred_at>=${start} GROUP BY 1,2,3,4,5 ORDER BY 1,2,4,5`.execute(
            db,
          )
        ).rows;
        const credits = (
          await sql<AdminFinances['credits'][number]>`${sql.join(
            Object.entries(CREDIT_PRODUCTS).map(
              ([product, { prefix }]) =>
                sql`SELECT ${product}::text AS product,kind,sum(amount)::float8 AS amount FROM ${sql.table(prefix + '_ledger')} WHERE created_at>=${cutoff} GROUP BY kind UNION ALL ${product === 'hive' || product === 'aiStudio' || product === 'generatorStudio' ? sql`SELECT ${product},'returned',coalesce(sum(greatest(0,reserved-charged)),0)::float8 FROM ${sql.table(prefix + '_calls')} WHERE status='settled' AND completed_at>=${cutoff}` : sql`SELECT ${product},'returned',count(*)::float8 FROM ${sql.table(prefix + '_ledger')} WHERE created_at>=${cutoff} AND details->>'returned'='true'`} UNION ALL SELECT ${product},'reserved',coalesce(sum(reserved),0)::float8 FROM ${sql.table(prefix + '_wallets')}`,
            ),
            sql` UNION ALL `,
          )}`.execute(db)
        ).rows;
        const costs = (
          await sql<AdminFinances['costs'][number]>`WITH usage AS (
    SELECT a.*,CASE WHEN a.created_at>=${cutoff} THEN 'current' ELSE 'previous' END AS period,
    CASE WHEN jsonb_typeof(coalesce(usage->'input',usage->'input_tokens'))='number' THEN (coalesce(usage->>'input',usage->>'input_tokens'))::numeric END AS input,
    CASE WHEN jsonb_typeof(coalesce(usage->'output',usage->'output_tokens'))='number' THEN (coalesce(usage->>'output',usage->>'output_tokens'))::numeric END AS output,
    CASE WHEN jsonb_typeof(coalesce(usage->'cachedInput',usage->'input_tokens_details'->'cached_tokens','0'::jsonb))='number' THEN coalesce(usage->>'cachedInput',usage->'input_tokens_details'->>'cached_tokens','0')::numeric END AS cached,
    CASE WHEN jsonb_typeof(coalesce(usage->'cacheWrite','0'::jsonb))='number' THEN coalesce(usage->>'cacheWrite','0')::numeric END AS cache_write
    FROM admin_provider_attempts a WHERE a.created_at>=${start}
   ), priced AS (
    SELECT u.*,r.version,r.currency,
    (input>=0 AND output>=0 AND cached>=0 AND cache_write>=0 AND cached+cache_write<=input AND cache_write=trunc(cache_write) AND input=trunc(input) AND output=trunc(output) AND cached=trunc(cached)) IS TRUE AS measured,
    r.input_micros,r.cached_input_micros,r.output_micros,r.call_micros
    FROM usage u LEFT JOIN LATERAL (SELECT * FROM admin_provider_rates WHERE model=u.model AND effective_at<=u.created_at ORDER BY effective_at DESC,version DESC LIMIT 1) r ON TRUE
   ) SELECT product,model,period,currency,version AS "rateVersion",count(*)::int AS attempts,count(*) FILTER(WHERE measured)::int AS metered,count(*) FILTER(WHERE measured AND version IS NOT NULL AND cache_write=0)::int AS priced,
   sum(CASE WHEN measured AND version IS NOT NULL AND cache_write=0 THEN ceil(((input-cached)*input_micros+cached*cached_input_micros+output*output_micros)/1000000)+call_micros END)::float8 AS "estimatedMicros"
   FROM priced GROUP BY product,model,period,currency,version ORDER BY product,model,period`.execute(
            db,
          )
        ).rows;
        const unknown =
          (
            await sql<{
              count: number;
            }>`SELECT count(*)::int AS count FROM admin_payment_totals WHERE mode=${mode} AND (paid_amount IS NULL OR historical)`.execute(
              db,
            )
          ).rows[0]?.count ?? 0;
        return {
          days,
          mode,
          generatedAt: new Date().toISOString(),
          cash,
          credits,
          costs,
          unknownPurchases: unknown,
          historicalIncomplete: unknown > 0 || mode === 'unclassified',
        };
      })();
      cache.set(key, { at: Date.now(), value });
      value.catch(() => {
        if (cache.get(key)?.value === value) cache.delete(key);
      });
      return value;
    },
  );
}
