import { readFile } from 'node:fs/promises';
import { sql } from 'kysely';
import { Type } from 'typebox';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import { Checkout, Credits, HiveError } from '@glob2/billing';
import {
  AiStudioCreate,
  AiStudioSave,
  AiStudioCommand,
  AiStudioRun,
  GeneratorStudioCreate,
  GeneratorStudioSave,
  importGeneratorPackage,
  Strict,
  Uuid,
  parseSimVersionKey,
} from '@glob2/protocol';
import { putContent, ensureAiValidation } from '@glob2/core';
import { insertBlob } from '@glob2/play';
import { requireAccount, requireRole } from '../identity.ts';
import { body } from '../http/validate.ts';
import { apiError } from '../errors.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { CodingProvider } from './provider.ts';
import { StudioStore, type StudioRequest } from './store.ts';
import { StudioRunner, starter } from './runner.ts';
import {
  generatorStarter,
  draftHash,
  generatorSystemPrompt,
  generatorEdit,
} from '../generator-studio/domain.ts';
import { generatorToolsRoutes } from '../generator-studio/tools.ts';
export async function codingStudioRoutes(
  app: FastifyInstance,
  domain: 'aiStudio' | 'generatorStudio',
) {
  const generator = domain === 'generatorStudio';
  const route = generator ? 'generator-studio' : 'ai-studio';
  const ROOT = '/api/v1/' + route;
  const env = generator ? 'GENERATOR_STUDIO' : 'AI_STUDIO';
  app.addHook('onRequest', async (request, reply) => {
    if (request.url.startsWith(ROOT)) reply.header('cache-control', 'private, no-store');
  });
  const { db, blobs } = app.services,
    config = app.services.config.instance[domain];
  const store = new StudioStore(db, domain, generator ? draftHash : undefined),
    credits = new Credits(db, domain);
  const limit = new SharedLimit(db, route + '-write', 3600, 3600000);
  const requests = new SharedLimit(db, route + '-request', 60, 3600000);
  const checks = new SharedLimit(db, route + '-check', 12, 3600000);
  const key = process.env[env + '_OPENAI_API_KEY'];
  if (config?.enabled && (!config.model || !config.rate || !key))
    throw Error(`${route} requires model, rate, spending limits and ${env}_OPENAI_API_KEY.`);
  const runner =
    config?.enabled && config.model && config.rate && key
      ? new StudioRunner(
          store,
          new CodingProvider(key, config.model, generator ? generatorEdit : undefined),
          { ...config.rate, model: config.model },
          config.maxOutputTokens,
          (e) => app.log.error({ err: e }, route + ' request failed'),
          generator ? generatorSystemPrompt : undefined,
        )
      : undefined;
  if (config?.enabled && config.salesEnabled && !config.packs?.length)
    throw Error(`${generator ? 'Generator' : 'AI'} Studio sales require configured credit packs.`);
  const checkout =
    config?.enabled && config.salesEnabled && config.packs?.length
      ? new Checkout(
          db,
          process.env[env + '_STRIPE_SECRET_KEY'] ?? '',
          process.env[env + '_STRIPE_WEBHOOK_SECRET'] ?? '',
          app.services.config.publicOrigin,
          config.packs,
          domain,
        )
      : undefined;
  let pending: Promise<void> | undefined;
  const tick = () => {
    if (runner && !pending)
      pending = runner
        .sweep()
        .catch((e) => app.log.error({ err: e }, route + ' sweep failed'))
        .finally(() => {
          pending = undefined;
        });
  };
  const timer = setInterval(tick, 1000);
  timer.unref();
  app.addHook('onClose', async () => {
    clearInterval(timer);
    runner?.stop();
    await pending;
  });
  const account = async (r: FastifyRequest, generation = false) => {
    const { account } = await requireAccount(app.identity, r);
    if ((generator || generation) && !config?.enabled)
      throw apiError(
        'not_found',
        `${generator ? 'Generator' : 'AI'} Studio is not enabled on this instance.`,
      );
    return account;
  };
  const idOf = (r: FastifyRequest) => body(Strict({ id: Uuid }), r.params).id;
  const guard = async <T>(fn: () => Promise<T>) => {
    try {
      return await fn();
    } catch (e) {
      if (e instanceof HiveError)
        throw apiError(
          e.code === 'not_found' ? 'not_found' : e.code === 'conflict' ? 'conflict' : 'bad_request',
          e.message,
        );
      throw e;
    }
  };
  app.get(ROOT + '/account', async (r) => {
    const { account: a } = await requireAccount(app.identity, r);
    return {
      enabled: !!config?.enabled,
      model: config?.model ?? '',
      rate: config?.rate ?? null,
      maxRequestCredits: config?.maxRequestCredits ?? 0,
      ...(await credits.balance(a.id)),
      packs: checkout
        ? (config?.packs ?? []).map(({ id, credits, amount, currency }) => ({
            id,
            credits,
            amount,
            currency,
          }))
        : [],
    };
  });
  app.get(ROOT + '/projects', async (r) => {
    const a = await account(r);
    return {
      items: (
        await sql`SELECT id,title,revision,updated_at FROM ${store.table('projects')} WHERE account_id=${a.id} ORDER BY updated_at DESC LIMIT 100`.execute(
          db,
        )
      ).rows,
    };
  });
  app.post(ROOT + '/projects', { bodyLimit: 1024 * 1024 }, async (r, reply) =>
    guard(async () => {
      const a = await account(r),
        input = body(generator ? GeneratorStudioCreate : AiStudioCreate, r.body);
      await enforce(limit, a.id, reply, 'Too many Studio writes.');
      let source = input.source ?? (generator ? generatorStarter() : starter);
      if (input.versionId) {
        if (input.source !== undefined)
          throw apiError('bad_request', 'Choose either an import or a library version.');
        const v = generator
          ? await db
              .selectFrom('generator_versions as v')
              .innerJoin('generators as a', 'a.id', 'v.generator_id')
              .innerJoin('blobs as b', 'b.sha256', 'v.hash')
              .select('b.storage_key')
              .where('v.id', '=', input.versionId)
              .where('a.owner_account_id', '=', a.id)
              .executeTakeFirst()
          : await db
              .selectFrom('ai_versions as v')
              .innerJoin('ais as a', 'a.id', 'v.ai_id')
              .innerJoin('blobs as b', 'b.sha256', 'v.hash')
              .select('b.storage_key')
              .where('v.id', '=', input.versionId)
              .where('a.owner_account_id', '=', a.id)
              .executeTakeFirst();
        if (!v) throw apiError('not_found', `No owned ${generator ? 'generator' : 'AI'} version.`);
        const stream = await blobs.get(v.storage_key);
        if (!stream) throw apiError('not_found', 'Source unavailable.');
        const chunks: Buffer[] = [];
        let size = 0;
        for await (const chunk of stream) {
          const b = Buffer.from(chunk);
          size += b.length;
          if (size > (generator ? 262144 : 131072)) {
            stream.destroy();
            throw apiError('bad_request', 'Source too large.');
          }
          chunks.push(b);
        }
        source = new TextDecoder('utf-8', { fatal: true }).decode(Buffer.concat(chunks));
        if (generator) source = importGeneratorPackage(source);
      }
      return store.create(a.id, input.title, source);
    }),
  );
  app.get(ROOT + '/projects/:id', async (r) =>
    guard(async () => {
      const a = await account(r),
        id = idOf(r);
      return db
        .transaction()
        .setIsolationLevel('repeatable read')
        .setAccessMode('read only')
        .execute(async (tx) => {
          const p = await store.project(id, a.id, tx);
          return {
            ...p,
            current: await store.revision(id, p.revision, tx),
            revisions: (
              await sql`SELECT revision,hash,reason,created_at FROM ${store.table('revisions')} WHERE project_id=${id} ORDER BY revision DESC LIMIT 200`.execute(
                tx,
              )
            ).rows,
            requests: (
              await sql`SELECT r.id,base_revision,prompt,diagnostics,budget,r.status,response,error,c.charged FROM ${store.table('requests')} r LEFT JOIN ${store.table('calls')} c ON c.id=r.id WHERE project_id=${id} ORDER BY r.created_at DESC LIMIT 50`.execute(
                tx,
              )
            ).rows,
            cursor: String(
              (
                await sql<{
                  cursor: string;
                }>`SELECT coalesce(max(id),0)::text cursor FROM ${store.table('events')} WHERE project_id=${id}`.execute(
                  tx,
                )
              ).rows[0]?.cursor ?? '0',
            ),
            runs: (
              await sql`SELECT * FROM ${store.table('runs')} WHERE project_id=${id} ORDER BY created_at DESC LIMIT 20`.execute(
                tx,
              )
            ).rows,
          };
        });
    }),
  );
  app.patch(ROOT + '/projects/:id', { bodyLimit: 1024 * 1024 }, async (r, reply) =>
    guard(async () => {
      const a = await account(r);
      await enforce(limit, a.id, reply, 'Too many Studio writes.');
      const v = body(generator ? GeneratorStudioSave : AiStudioSave, r.body);
      return { revision: await store.save(idOf(r), a.id, v.expectedRevision, v) };
    }),
  );
  app.delete(ROOT + '/projects/:id', async (r) =>
    guard(async () => {
      const a = await account(r),
        id = idOf(r);
      await db.transaction().execute(async (tx) => {
        const p = await store.project(id, a.id, tx, true);
        await store.editable(tx, p, p.revision);
        if (
          (
            await sql`SELECT r.id FROM ${store.table('requests')} r JOIN ${store.table('calls')} c ON c.id=r.id WHERE project_id=${id} AND c.status IN ('reserved','dispatched','uncertain')`.execute(
              tx,
            )
          ).rows.length
        )
          throw apiError(
            'conflict',
            'Reconcile outstanding credit reservations before deleting this project.',
          );
        await sql`DELETE FROM ${store.table('projects')} WHERE id=${id}`.execute(tx);
      });
      return { deleted: true };
    }),
  );
  app.get(ROOT + '/projects/:id/revision', async (r) =>
    guard(async () => {
      const a = await account(r),
        id = idOf(r);
      await store.project(id, a.id);
      const q = body(Strict({ revision: Type.String({ pattern: '^[1-9][0-9]{0,8}$' }) }), r.query);
      return store.revision(id, Number(q.revision));
    }),
  );
  app.get(ROOT + '/projects/:id/events', async (r, reply) =>
    guard(async () => {
      const a = await account(r),
        id = idOf(r);
      await store.project(id, a.id);
      const q = body(Strict({ after: Type.String({ pattern: '^[0-9]{1,18}$' }) }), r.query);
      reply.header('cache-control', 'no-store');
      return {
        events: (
          await sql`SELECT id::text,id::text AS cursor,kind,body FROM ${store.table('events')} WHERE project_id=${id} AND id>${q.after}::bigint ORDER BY id LIMIT 100`.execute(
            db,
          )
        ).rows,
      };
    }),
  );
  app.post(ROOT + '/projects/:id/requests', async (r, reply) =>
    guard(async () => {
      const a = await account(r, true),
        v = body(AiStudioCommand, r.body);
      await enforce(requests, a.id, reply, 'Too many Studio requests.');
      if (!runner || v.budget > (config?.maxRequestCredits ?? 0))
        throw apiError('bad_request', 'Request cap exceeds the configured limit.');
      await store.command(idOf(r), a.id, v);
      tick();
      return { accepted: true };
    }),
  );
  app.post(ROOT + '/projects/:id/stop', async (r) =>
    guard(async () => {
      const a = await account(r),
        id = idOf(r);
      await db.transaction().execute(async (tx) => {
        await store.project(id, a.id, tx, true);
        await sql`UPDATE ${store.table('requests')} SET cancelled=true,status=CASE WHEN status='queued' THEN 'cancelled' ELSE status END WHERE project_id=${id} AND status IN ('queued','running')`.execute(
          tx,
        );
        await store.event(tx, id, 'stop', {});
      });
      return { stopped: true };
    }),
  );
  if (generator) await generatorToolsRoutes(app, store, checks);
  else {
    app.post(ROOT + '/projects/:id/check', async (r, reply) =>
      guard(async () => {
        const a = await account(r),
          id = idOf(r);
        await enforce(checks, a.id, reply, 'Too many checks; please wait.');
        const input = body(Strict({ revision: Type.Integer({ minimum: 1 }) }), r.body);
        await store.project(id, a.id);
        const revision = await store.revision(id, input.revision);
        const agents = await db
          .selectFrom('engine_agents')
          .select('sim_version')
          .where('last_seen_at', '>', new Date(Date.now() - 120000))
          .where(sql<boolean>`'validate-ai'=ANY(kinds)`)
          .execute();
        const sim = agents
          .map((x) => parseSimVersionKey(x.sim_version))
          .filter((x) => !!x)
          .sort((a, b) => b.versionMinor - a.versionMinor || b.netProtocol - a.netProtocol)[0];
        if (!sim) throw apiError('unavailable', 'No isolated AI validator is available.');
        const stored = await putContent(blobs, Buffer.from(revision.source));
        return db.transaction().execute(async (tx) => {
          // Serialize the account-wide queue limit across different projects and
          // take the same owner-before-project lock order as account deletion.
          const owner = await tx
            .selectFrom('accounts')
            .select('status')
            .where('id', '=', a.id)
            .forUpdate()
            .executeTakeFirst();
          if (owner?.status !== 'active') throw apiError('not_found', 'No active account.');
          await store.project(id, a.id, tx, true);
          const active = await tx
            .selectFrom('ai_uploads as u')
            .innerJoin('ai_validations as v', 'v.id', 'u.validation_id')
            .select('v.id')
            .where('u.owner_account_id', '=', a.id)
            .where('v.status', '=', 'pending')
            .execute();
          if (active.length >= 2)
            throw apiError('conflict', 'Wait for a pending compatibility check.');
          await insertBlob(
            tx,
            stored.sha256,
            stored.size,
            'application/javascript',
            'private',
            a.id,
          );
          const validation_id = await ensureAiValidation(tx, stored.sha256, sim, true);
          const upload = await tx
            .insertInto('ai_uploads')
            .values({ owner_account_id: a.id, validation_id })
            .returning('id')
            .executeTakeFirstOrThrow();
          await store.event(tx, id, 'validation', {
            revision: input.revision,
            uploadId: upload.id,
          });
          return { uploadId: upload.id, revision: input.revision };
        });
      }),
    );
    app.get(ROOT + '/projects/:id/checks', async (r) =>
      guard(async () => {
        const a = await account(r),
          id = idOf(r);
        await store.project(id, a.id);
        return {
          items: (
            await sql`SELECT * FROM (SELECT DISTINCT ON (v.id) v.report,v.status,v.error,v.created_at,u.id AS upload_id,u.expires_at FROM ${store.table('revisions')} r JOIN ai_validations v ON v.hash=r.hash LEFT JOIN ai_uploads u ON u.validation_id=v.id AND u.owner_account_id=${a.id} AND u.expires_at>now() WHERE r.project_id=${id} ORDER BY v.id,u.expires_at DESC) checks ORDER BY created_at DESC`.execute(
              db,
            )
          ).rows,
        };
      }),
    );
    app.post(ROOT + '/projects/:id/runs', async (r) =>
      guard(async () => {
        const a = await account(r),
          id = idOf(r),
          v = body(AiStudioRun, r.body);
        return db.transaction().execute(async (tx) => {
          await store.project(id, a.id, tx, true);
          const revision = await store.revision(id, v.expectedRevision, tx);
          const old = (
            await sql<{
              project_id: string;
              revision: number;
              seed: number;
              opponent: string;
            }>`SELECT * FROM ${store.table('runs')} WHERE id=${v.id}`.execute(tx)
          ).rows[0];
          if (
            old &&
            (old.project_id !== id ||
              old.revision !== v.expectedRevision ||
              old.seed !== v.seed ||
              old.opponent !== v.opponent)
          )
            throw apiError('conflict', 'Run identifier already used.');
          await sql`INSERT INTO ${store.table('runs')}(id,project_id,revision,seed,opponent) VALUES(${v.id},${id},${v.expectedRevision},${v.seed},${v.opponent}) ON CONFLICT DO NOTHING`.execute(
            tx,
          );
          await store.event(tx, id, 'playtest', { id: v.id, revision: v.expectedRevision });
          return {
            runId: v.id,
            revision: v.expectedRevision,
            source: revision.source,
            seed: v.seed,
            opponent: v.opponent,
          };
        });
      }),
    );
    app.post(ROOT + '/projects/:id/run-result', async (r) =>
      guard(async () => {
        const a = await account(r),
          id = idOf(r);
        await store.project(id, a.id);
        const v = body(Strict({ runId: Uuid, summary: Type.String({ maxLength: 16000 }) }), r.body);
        await sql`UPDATE ${store.table('runs')} SET summary=${v.summary.replaceAll('\0', '')} WHERE id=${v.runId} AND project_id=${id}`.execute(
          db,
        );
        return { saved: true };
      }),
    );
    app.get(ROOT + '/test-map', async (r, reply) => {
      await account(r);
      return reply
        .type('application/octet-stream')
        .header('cache-control', 'private, no-store')
        .send(
          await readFile(new URL('../../../engine-agent/fixtures/ais/two.map.gz', import.meta.url)),
        );
    });
  }
  app.post(ROOT + '/checkout', async (r) =>
    guard(async () => {
      const a = await account(r);
      if (a.kind !== 'registered' || !checkout)
        throw apiError('forbidden', 'Studio credit purchases are not available.');
      const input = body(Strict({ id: Uuid, pack: Type.String({ maxLength: 64 }) }), r.body);
      return checkout.begin(a.id, input.pack, input.id);
    }),
  );
  app.post(ROOT + '/reconcile', async (r) =>
    guard(async () => {
      const { account: actor } = await requireRole(app.identity, r, 'admin');
      const v = body(
        Strict({
          requestId: Uuid,
          evidence: Type.String({ minLength: 1, maxLength: 2000 }),
          usage: Strict({
            input: Type.Integer({ minimum: 0 }),
            cachedInput: Type.Integer({ minimum: 0 }),
            output: Type.Integer({ minimum: 0 }),
          }),
        }),
        r.body,
      );
      const row = (
        await sql<
          StudioRequest & { account_id: string }
        >`SELECT r.*,p.account_id FROM ${store.table('requests')} r JOIN ${store.table('projects')} p ON p.id=r.project_id WHERE r.id=${v.requestId} AND r.status='uncertain'`.execute(
          db,
        )
      ).rows[0];
      if (!row) throw apiError('not_found', 'No uncertain request.');
      await credits.reconcile(row.account_id, row.id, v.usage, v.evidence, {
        actor: actor.id,
        action: route + '.reconcile',
        targetType: route + '-request',
        targetId: row.id,
      });
      await db.transaction().execute(async (tx) => {
        // Use the same project-before-request lock order as edits and deletion.
        await sql`SELECT id FROM ${store.table('projects')} WHERE id=${row.project_id} FOR UPDATE`.execute(
          tx,
        );
        const changed =
          await sql`UPDATE ${store.table('requests')} SET status='failed',error='Credit usage reconciled. Submit a new request to continue.' WHERE id=${row.id} AND status='uncertain' RETURNING id`.execute(
            tx,
          );
        if (!changed.rows.length) return;
        await store.event(
          tx,
          row.project_id,
          'reconciled',
          { id: row.id, evidence: v.evidence },
          row.id,
        );
      });
      return { settled: true };
    }),
  );
  if (checkout)
    await app.register(async (scoped) => {
      scoped.removeContentTypeParser('application/json');
      scoped.addContentTypeParser(
        'application/json',
        { parseAs: 'buffer', bodyLimit: 262144 },
        (_r, raw, done) => done(null, raw),
      );
      scoped.post(ROOT + '/stripe', async (r) => {
        const signature = r.headers['stripe-signature'];
        if (typeof signature !== 'string')
          throw apiError('bad_request', 'Missing payment signature.');
        await checkout.webhook(r.body as Buffer, signature);
        return { received: true };
      });
    });
}
