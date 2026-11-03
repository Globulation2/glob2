import { randomUUID } from 'node:crypto';
import sharp from 'sharp';
import { AgentBlobs } from '@glob2/engine/blobs';
import { sql } from 'kysely';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import { Checkout, HiveError } from '@glob2/billing';
import { TerrainStudio, TERRAIN_STUDIO_CHANNEL } from '@glob2/terrain-studio';
import {
  TerrainStudioCreate,
  TerrainStudioTurn,
  TerrainStudioAdopt,
  SetPackage,
  newPackage,
  namespace,
  parse,
  type SetPackage as Package,
  Strict,
  Uuid,
} from '@glob2/protocol';
import { Type } from 'typebox';
import { requireAccount } from '../identity.ts';
import { body } from '../http/validate.ts';
import { apiError } from '../errors.ts';
import { streamStudioEvents } from '../http/studioEvents.ts';
export async function terrainStudioRoutes(app: FastifyInstance) {
  const studio = new TerrainStudio(app.services.db),
    config = app.services.config.instance.terrainStudio;
  const enabled = !!config?.enabled;
  const streams = new Map<string, number>();
  let totalStreams = 0;
  if (
    enabled &&
    (!config.textModel ||
      !config.imageModel ||
      !config.pipelineVersion ||
      !config.providerCallsPerDay ||
      !config.maxOutputTokens ||
      !config.timeoutSeconds)
  )
    throw new Error(
      'AI Terrain Studio needs a text model, a pipeline version and a provider-call budget.',
    );
  const checkout =
    process.env['TERRAIN_STRIPE_SECRET_KEY'] && process.env['TERRAIN_STRIPE_WEBHOOK_SECRET']
      ? new Checkout(
          app.services.db,
          process.env['TERRAIN_STRIPE_SECRET_KEY'] ?? '',
          process.env['TERRAIN_STRIPE_WEBHOOK_SECRET'] ?? '',
          app.services.config.publicOrigin,
          config?.packs ?? [],
          'terrain',
        )
      : undefined;
  const accountOf = async (request: FastifyRequest) => {
    const { account } = await requireAccount(app.identity, request);
    if (account.kind !== 'registered')
      throw apiError('forbidden', 'Sign in with a registered account to use AI Terrain Studio.');
    return account;
  };
  const threadOf = (request: FastifyRequest) => body(Strict({ id: Uuid }), request.params).id;
  const guarded = async <T>(fn: () => Promise<T>) => {
    try {
      return await fn();
    } catch (error) {
      if (error instanceof HiveError)
        throw apiError(
          error.code === 'not_found'
            ? 'not_found'
            : error.code === 'conflict'
              ? 'conflict'
              : error.code === 'rate_limited'
                ? 'rate_limited'
                : 'bad_request',
          error.message,
        );
      throw error;
    }
  };
  const requireEnabled = () => {
    if (!enabled) throw apiError('forbidden', 'AI Terrain Studio is not enabled on this instance.');
  };
  app.get('/api/v1/terrain-studio/account', async (request) => {
    const account = await accountOf(request);
    return {
      enabled,
      activeRequest: (await studio.active(account.id)) ?? null,
      ...(await studio.credits.balance(account.id)),
      packs:
        enabled && config?.salesEnabled && checkout
          ? (config?.packs ?? []).map(({ id, credits, amount, currency }) => ({
              id,
              credits,
              amount,
              currency,
            }))
          : [],
      usage: await app.services.db
        .selectFrom('terrain_ledger')
        .select(['id', 'amount', 'kind', 'created_at'])
        .where('account_id', '=', account.id)
        .orderBy('created_at', 'desc')
        .limit(30)
        .execute(),
    };
  });
  app.get('/api/v1/terrain-studio/threads', async (request) => ({
    items: await studio.list((await accountOf(request)).id),
  }));
  app.post('/api/v1/terrain-studio/threads', async (request) =>
    guarded(async () => {
      requireEnabled();
      const input = body(TerrainStudioCreate, request.body);
      const a = await accountOf(request);
      if (input.draftId && input.versionId)
        throw apiError('bad_request', 'Choose a draft or a released set.');
      let pack: Package | undefined;
      if (input.versionId) {
        const source = await app.services.db
          .selectFrom('set_versions as v')
          .innerJoin('asset_sets as s', 's.id', 'v.set_id')
          .select(['v.hash', 's.owner_account_id', 's.visibility', 's.hidden'])
          .where('v.id', '=', input.versionId)
          .executeTakeFirst();
        if (
          !source ||
          source.hidden ||
          (source.visibility === 'private' && source.owner_account_id !== a.id)
        )
          throw apiError('not_found', 'Release unavailable.');
        const bytes = await new AgentBlobs(app.services.blobs, app.services.db).read(
          source.hash,
          16 * 1024 * 1024,
        );
        pack = parse(SetPackage, JSON.parse(Buffer.from(bytes).toString()), 'remix source');
        const old = namespace(pack);
        pack.setId = randomUUID();
        pack.versionId = randomUUID();
        const next = namespace(pack);
        const remap = (v: unknown): unknown =>
          typeof v === 'string'
            ? v.startsWith(old)
              ? next + v.slice(old.length)
              : v
            : Array.isArray(v)
              ? v.map(remap)
              : v && typeof v === 'object'
                ? Object.fromEntries(
                    Object.entries(v).map(([k, x]) => [
                      k.startsWith(old) ? next + k.slice(old.length) : k,
                      remap(x),
                    ]),
                  )
                : v;
        pack = parse(SetPackage, remap(pack), 'remixed set');
        pack.title = input.title;
        if (pack.credits.length < 64)
          pack.credits.push({ author: a.display_name, license: pack.license });
      } else if (!input.draftId) {
        pack = newPackage(a.display_name);
        pack.title = input.title;
      }
      return studio.create(a.id, input.title, input.id, pack, input.draftId, input.versionId);
    }),
  );
  app.delete('/api/v1/terrain-studio/threads/:id', async (request) =>
    guarded(async () => {
      await studio.remove((await accountOf(request)).id, threadOf(request));
      return { deleted: true };
    }),
  );
  app.get('/api/v1/terrain-studio/threads/:id', async (request, reply) =>
    guarded(async () => {
      reply.header('cache-control', 'no-store');
      const before = body(
        Strict({ messagesBefore: Type.Optional(Uuid), requestsBefore: Type.Optional(Uuid) }),
        request.query,
      );
      return studio.get((await accountOf(request)).id, threadOf(request), before);
    }),
  );
  app.get(
    '/api/v1/terrain-studio/threads/:id/requests/:requestId/progress',
    async (request, reply) =>
      guarded(async () => {
        const params = body(Strict({ id: Uuid, requestId: Uuid }), request.params);
        reply.header('cache-control', 'no-store');
        return studio.progress((await accountOf(request)).id, params.id, params.requestId);
      }),
  );
  app.get('/api/v1/terrain-studio/threads/:id/artifacts/:artifactId', async (request, reply) =>
    guarded(async () => {
      const params = body(
        Strict({ id: Uuid, artifactId: Type.String({ minLength: 1, maxLength: 120 }) }),
        request.params,
      );
      const blob = await studio.artifactBlob(
        (await accountOf(request)).id,
        params.id,
        params.artifactId,
      );
      const stream = await app.services.blobs.get(blob.storage_key);
      if (!stream) throw apiError('not_found', 'The artwork artifact is not available.');
      return reply
        .header('content-type', blob.content_type)
        .header('cache-control', 'private, no-store')
        .header('x-content-type-options', 'nosniff')
        .send(stream);
    }),
  );
  app.get('/api/v1/terrain-studio/threads/:id/events', async (request, reply) =>
    guarded(async () => {
      const account = await accountOf(request),
        thread = threadOf(request);
      await studio.own(account.id, thread);
      const query = body(
        Strict({ cursor: Type.Optional(Type.String({ pattern: '^[0-9]{1,16}$' })) }),
        request.query,
      );
      const header = request.headers['last-event-id'];
      if (header !== undefined && (typeof header !== 'string' || !/^[0-9]{1,16}$/.test(header)))
        throw apiError('bad_request', 'Invalid studio event cursor.');
      const cursor = typeof header === 'string' ? header : (query.cursor ?? '0');
      if ((streams.get(account.id) ?? 0) >= 3 || totalStreams >= 128)
        throw apiError('rate_limited', 'Too many live Terrain Studio connections.');
      streams.set(account.id, (streams.get(account.id) ?? 0) + 1);
      totalStreams++;
      let released = false;
      const release = () => {
        if (released) return;
        released = true;
        totalStreams--;
        const n = (streams.get(account.id) ?? 1) - 1;
        if (n) streams.set(account.id, n);
        else streams.delete(account.id);
      };
      reply.raw.once('close', release);
      try {
        return await streamStudioEvents({
          studio,
          channel: TERRAIN_STUDIO_CHANNEL,
          pubsub: app.services.pubsub,
          request,
          reply,
          accountId: account.id,
          thread,
          cursor,
          authenticate: async () => (await accountOf(request)).id,
        });
      } catch (error) {
        release();
        throw error;
      }
    }),
  );
  app.get('/api/v1/terrain-studio/threads/:id/revisions/:requestId/file', async (request, reply) =>
    guarded(async () => {
      const a = await accountOf(request),
        params = body(Strict({ id: Uuid, requestId: Uuid }), request.params);
      await studio.own(a.id, params.id);
      const revision = await app.services.db
        .selectFrom('terrain_studio_revisions')
        .select('document')
        .where('request_id', '=', params.requestId)
        .where('thread_id', '=', params.id)
        .executeTakeFirst();
      if (!revision) throw apiError('not_found', 'Candidate unavailable.');
      return reply
        .header('content-type', 'application/json')
        .header('cache-control', 'private, no-store')
        .header('content-disposition', 'attachment; filename="terrain-set.json"')
        .send(JSON.stringify(revision.document));
    }),
  );
  app.post('/api/v1/terrain-studio/threads/:id/turns', async (request) =>
    guarded(async () => {
      requireEnabled();
      return studio.submit(
        (await accountOf(request)).id,
        threadOf(request),
        body(TerrainStudioTurn, request.body),
        config ??
          (() => {
            throw apiError('forbidden', 'Terrain Studio is disabled.');
          })(),
      );
    }),
  );
  app.get('/api/v1/terrain-studio/threads/:id/drafts/:revision/file', async (request, reply) =>
    guarded(async () => {
      const params = body(
        Strict({ id: Uuid, revision: Type.String({ pattern: '^[0-9]+$' }) }),
        request.params,
      );
      const saved = await studio.draftBackup(
        (await accountOf(request)).id,
        params.id,
        Number(params.revision),
      );
      return reply
        .header('content-type', 'application/json')
        .header('cache-control', 'private, no-store')
        .header('content-disposition', 'attachment; filename="saved-draft.json"')
        .send(saved);
    }),
  );
  app.post('/api/v1/terrain-studio/threads/:id/drafts/:revision/restore', async (request) =>
    guarded(async () => {
      const params = body(
          Strict({ id: Uuid, revision: Type.String({ pattern: '^[0-9]+$' }) }),
          request.params,
        ),
        input = body(TerrainStudioAdopt, request.body);
      await studio.restoreDraft(
        (await accountOf(request)).id,
        params.id,
        Number(params.revision),
        input.expectedRevision,
      );
      return { ok: true };
    }),
  );
  app.post('/api/v1/terrain-studio/threads/:id/requests/:requestId/adopt', async (request) =>
    guarded(async () => {
      const params = body(Strict({ id: Uuid, requestId: Uuid }), request.params),
        input = body(TerrainStudioAdopt, request.body);
      await studio.adopt(
        (await accountOf(request)).id,
        params.id,
        params.requestId,
        input.expectedRevision,
      );
      return { ok: true };
    }),
  );
  app.post(
    '/api/v1/terrain-studio/threads/:id/references',
    { bodyLimit: 8 * 1024 * 1024 },
    async (request) =>
      guarded(async () => {
        requireEnabled();
        const a = await accountOf(request),
          thread = threadOf(request);
        await studio.own(a.id, thread);
        if (!Buffer.isBuffer(request.body))
          throw apiError('bad_request', 'Upload an image as binary data.');
        let bytes: Buffer;
        try {
          const decoder = sharp(request.body, { limitInputPixels: 2048 * 2048 });
          const metadata = await decoder.metadata();
          if (!['png', 'jpeg', 'webp'].includes(metadata.format ?? '') || (metadata.pages ?? 1) > 1)
            throw Error();
          bytes = await decoder
            .rotate()
            .resize({ width: 1024, height: 1024, fit: 'inside', withoutEnlargement: true })
            .png()
            .toBuffer();
        } catch {
          throw apiError('bad_request', 'Choose a PNG, JPEG or WebP up to 2048×2048.');
        }
        const hash = await new AgentBlobs(app.services.blobs, app.services.db).write(
          bytes,
          'image/png',
        );
        const artifact = await app.services.db.transaction().execute(async (db) => {
          await sql`SELECT id FROM terrain_studio_threads WHERE id=${thread} FOR UPDATE`.execute(
            db,
          );
          await studio.own(a.id, thread, db);
          const n =
            (
              await sql<{
                n: number;
              }>`SELECT count(*)::int n FROM terrain_studio_artifacts WHERE thread_id=${thread} AND kind='reference'`.execute(
                db,
              )
            ).rows[0]?.n ?? 0;
          if (n >= 64) throw apiError('bad_request', 'Reference limit reached for this project.');
          return (
            (
              await sql<{
                id: string;
              }>`INSERT INTO terrain_studio_artifacts(thread_id,stage,kind,label,hash) VALUES(${thread},'prepare','reference','Uploaded visual reference',${hash}) RETURNING id`.execute(
                db,
              )
            ).rows[0] ??
            (() => {
              throw apiError('unavailable', 'Reference upload could not be saved.');
            })()
          );
        });
        return {
          hash,
          url: `/api/v1/terrain-studio/threads/${thread}/artifacts/${artifact.id}`,
          label: 'Uploaded visual reference',
        };
      }),
  );
  app.post('/api/v1/terrain-studio/checkout', async (request) =>
    guarded(async () => {
      const account = await accountOf(request);
      if (!enabled || !config?.salesEnabled || !checkout)
        throw apiError('forbidden', 'Terrain-credit sales are not enabled.');
      return checkout.begin(
        account.id,
        body(Strict({ pack: Type.String({ minLength: 1, maxLength: 64 }) }), request.body).pack,
      );
    }),
  );
  app.post('/api/v1/terrain-studio/threads/:id/requests/:requestId/cancel', async (request) =>
    guarded(async () => {
      const params = body(Strict({ id: Uuid, requestId: Uuid }), request.params);
      await studio.cancel((await accountOf(request)).id, params.id, params.requestId);
      return { ok: true };
    }),
  );
  // Long poll uses cross-replica notifications; timeout forces a state refresh after lost notifications.
  app.get('/api/v1/terrain-studio/threads/:id/updates', async (request, reply) =>
    guarded(async () => {
      const account = await accountOf(request),
        thread = threadOf(request);
      await studio.own(account.id, thread);
      await new Promise<void>((resolve) => {
        let remove: (() => Promise<void>) | undefined,
          finished = false;
        const done = () => {
          finished = true;
          clearTimeout(timer);
          reply.raw.off('close', done);
          void remove?.();
          resolve();
        };
        const timer = setTimeout(done, 20000);
        reply.raw.on('close', done);
        void app.services.pubsub
          .subscribe(TERRAIN_STUDIO_CHANNEL, (payload) => {
            const p = payload as { account?: string; thread?: string };
            if (p.account === account.id && p.thread === thread) done();
          })
          .then((unsubscribe) => {
            remove = unsubscribe;
            if (finished) void unsubscribe();
          })
          .catch(done);
      });
      reply.header('cache-control', 'no-store');
      return studio.get(account.id, thread);
    }),
  );
  app.post('/api/v1/admin/terrain-studio/requests/:id/fail', async (request) =>
    guarded(async () => {
      const account = await accountOf(request);
      if (account.role !== 'admin') throw apiError('forbidden', 'Administrator access required.');
      const reason = (request.body as { reason?: unknown } | undefined)?.reason;
      if (
        reason !== undefined &&
        (typeof reason !== 'string' || !reason.trim() || reason.length > 2000)
      )
        throw apiError('bad_request', 'Give a bounded recovery reason.');
      const id = threadOf(request),
        row = await studio.request(id);
      if (!row || row.status !== 'uncertain')
        throw apiError('conflict', 'Only uncertain requests need reconciliation.');
      await studio.finish(
        row,
        undefined,
        'Generation could not be recovered. Your credit was returned.',
        false,
        {
          actor: account.id,
          reason: typeof reason === 'string' ? reason : 'Operator reconciliation',
        },
      );
      return { reconciled: true };
    }),
  );
  if (checkout)
    await app.register(async (scope) => {
      scope.removeContentTypeParser('application/json');
      scope.addContentTypeParser(
        'application/json',
        { parseAs: 'buffer', bodyLimit: 262144 },
        (_request, raw, done) => done(null, raw),
      );
      scope.post('/api/v1/terrain-studio/stripe', async (request) => {
        const signature = request.headers['stripe-signature'];
        if (typeof signature !== 'string')
          throw apiError('bad_request', 'Missing payment signature.');
        try {
          await checkout.webhook(request.body as Buffer, signature);
        } catch (error) {
          if (error instanceof Error && error.name === 'StripeSignatureVerificationError')
            throw apiError('bad_request', 'Invalid payment signature.');
          throw error;
        }
        return { received: true };
      });
    });
}
