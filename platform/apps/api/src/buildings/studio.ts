import { randomUUID } from 'node:crypto';
import sharp from 'sharp';
import { AgentBlobs } from '@glob2/engine/blobs';
import { sql } from 'kysely';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import { Checkout, HiveError } from '@glob2/billing';
import { BuildingAiStudio, BUILDING_STUDIO_CHANNEL } from '@glob2/building-studio';
import {
  BuildingAiStudioCreate,
  BuildingAiStudioTurn,
  BuildingAiStudioAdopt,
  Strict,
  Uuid,
} from '@glob2/protocol';
import { newBuildingPackage } from '@glob2/building-studio';
import { readBuildingArchive, writeBuildingArchive, buildingAssetHash } from '@glob2/protocol/node';
import { Type } from 'typebox';
import { requireAccount } from '../identity.ts';
import { body } from '../http/validate.ts';
import { apiError } from '../errors.ts';
import { streamStudioEvents } from '../http/studioEvents.ts';
// HTTP owns authentication and byte normalization; the studio service owns
// leases, reservations and compare-and-swap delivery. Provider calls run only
// in the private worker, never while an API request or transaction is open.
export async function buildingStudioRoutes(app: FastifyInstance) {
  const studio = new BuildingAiStudio(app.services.db),
    config = app.services.config.instance.buildingStudio;
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
      'AI Building Studio needs text and image models, a pipeline version, a provider-call budget, output limits and a timeout.',
    );
  const checkout =
    process.env['BUILDING_STRIPE_SECRET_KEY'] && process.env['BUILDING_STRIPE_WEBHOOK_SECRET']
      ? new Checkout(
          app.services.db,
          process.env['BUILDING_STRIPE_SECRET_KEY'] ?? '',
          process.env['BUILDING_STRIPE_WEBHOOK_SECRET'] ?? '',
          app.services.config.publicOrigin,
          config?.packs ?? [],
          'buildings',
        )
      : undefined;
  const accountOf = async (request: FastifyRequest) => {
    const { account } = await requireAccount(app.identity, request);
    if (account.kind !== 'registered' || account.status !== 'active')
      throw apiError('forbidden', 'Sign in with a registered account to use AI Building Studio.');
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
    if (!enabled)
      throw apiError('forbidden', 'AI Building Studio is not enabled on this instance.');
  };
  app.get('/api/v1/ai-building-studio/account', async (request) => {
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
        .selectFrom('building_ledger')
        .select(['id', 'amount', 'kind', 'created_at'])
        .where('account_id', '=', account.id)
        .orderBy('created_at', 'desc')
        .limit(30)
        .execute(),
    };
  });
  app.get('/api/v1/ai-building-studio/threads', async (request) => ({
    items: await studio.list((await accountOf(request)).id),
  }));
  app.post('/api/v1/ai-building-studio/threads', async (request) =>
    guarded(async () => {
      requireEnabled();
      const input = body(BuildingAiStudioCreate, request.body);
      const a = await accountOf(request);
      const archive = !input.draftId
        ? writeBuildingArchive(newBuildingPackage(randomUUID()), new Map())
        : undefined;
      return studio.create(a.id, input.title, input.id, archive, input.draftId);
    }),
  );
  app.delete('/api/v1/ai-building-studio/threads/:id', async (request) =>
    guarded(async () => {
      await studio.remove((await accountOf(request)).id, threadOf(request));
      return { deleted: true };
    }),
  );
  app.get('/api/v1/ai-building-studio/threads/:id', async (request, reply) =>
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
    '/api/v1/ai-building-studio/threads/:id/requests/:requestId/progress',
    async (request, reply) =>
      guarded(async () => {
        const params = body(Strict({ id: Uuid, requestId: Uuid }), request.params);
        reply.header('cache-control', 'no-store');
        return studio.progress((await accountOf(request)).id, params.id, params.requestId);
      }),
  );
  app.get('/api/v1/ai-building-studio/threads/:id/artifacts/:artifactId', async (request, reply) =>
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
  app.get('/api/v1/ai-building-studio/threads/:id/events', async (request, reply) =>
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
        throw apiError('rate_limited', 'Too many live Building Studio connections.');
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
          channel: BUILDING_STUDIO_CHANNEL,
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
  app.get(
    '/api/v1/ai-building-studio/threads/:id/revisions/:requestId/file',
    async (request, reply) =>
      guarded(async () => {
        const a = await accountOf(request),
          params = body(Strict({ id: Uuid, requestId: Uuid }), request.params);
        await studio.own(a.id, params.id);
        const revision = await app.services.db
          .selectFrom('building_studio_revisions')
          .select('hash')
          .where('request_id', '=', params.requestId)
          .where('thread_id', '=', params.id)
          .executeTakeFirst();
        if (!revision) throw apiError('not_found', 'Candidate unavailable.');
        return reply
          .header('content-type', 'application/zip')
          .header('cache-control', 'private, no-store')
          .header('content-disposition', 'attachment; filename="building-family.zip"')
          .send(
            Buffer.from(
              await new AgentBlobs(app.services.blobs, app.services.db).read(
                revision.hash,
                32 * 1024 * 1024,
              ),
            ),
          );
      }),
  );
  app.get(
    '/api/v1/ai-building-studio/threads/:id/revisions/:requestId/assets/:hash',
    async (request, reply) =>
      guarded(async () => {
        const params = body(
          Strict({ id: Uuid, requestId: Uuid, hash: Type.String({ pattern: '^[a-f0-9]{64}$' }) }),
          request.params,
        );
        await studio.own((await accountOf(request)).id, params.id);
        const candidate = await app.services.db
          .selectFrom('building_studio_revisions')
          .select('hash')
          .where('thread_id', '=', params.id)
          .where('request_id', '=', params.requestId)
          .executeTakeFirst();
        if (!candidate) throw apiError('not_found', 'Candidate unavailable.');
        const archive = readBuildingArchive(
          Buffer.from(
            await new AgentBlobs(app.services.blobs, app.services.db).read(
              candidate.hash,
              32 * 1024 * 1024,
            ),
          ),
        );
        const image = archive.assets.get(params.hash);
        if (!image) throw apiError('not_found', 'Image unavailable.');
        return reply
          .header('content-type', 'image/webp')
          .header('cache-control', 'private, no-store')
          .header('x-content-type-options', 'nosniff')
          .send(image);
      }),
  );
  app.post('/api/v1/ai-building-studio/threads/:id/turns', async (request) =>
    guarded(async () => {
      requireEnabled();
      const a = await accountOf(request),
        t = await studio.own(a.id, threadOf(request));
      const draft = await app.services.db
        .selectFrom('building_drafts')
        .select('archive')
        .where('id', '=', t.draftId)
        .where('owner_account_id', '=', a.id)
        .executeTakeFirstOrThrow();
      const archiveHash = await new AgentBlobs(app.services.blobs, app.services.db).write(
        draft.archive,
        'application/zip',
      );
      return studio.submit(
        a.id,
        threadOf(request),
        body(BuildingAiStudioTurn, request.body),
        config ??
          (() => {
            throw apiError('forbidden', 'Building Studio is disabled.');
          })(),
        archiveHash,
      );
    }),
  );
  app.get('/api/v1/ai-building-studio/threads/:id/drafts/:revision/file', async (request, reply) =>
    guarded(async () => {
      const params = body(Strict({ id: Uuid, revision: Uuid }), request.params);
      const saved = await studio.draftBackup(
        (await accountOf(request)).id,
        params.id,
        params.revision,
      );
      return reply
        .header('content-type', 'application/zip')
        .header('cache-control', 'private, no-store')
        .header('content-disposition', 'attachment; filename="saved-draft.zip"')
        .send(saved.archive);
    }),
  );
  app.post('/api/v1/ai-building-studio/threads/:id/drafts/:revision/restore', async (request) =>
    guarded(async () => {
      const params = body(Strict({ id: Uuid, revision: Uuid }), request.params),
        input = body(BuildingAiStudioAdopt, request.body);
      await studio.restoreDraft(
        (await accountOf(request)).id,
        params.id,
        params.revision,
        input.expectedRevision,
      );
      return { ok: true };
    }),
  );
  app.post('/api/v1/ai-building-studio/threads/:id/requests/:requestId/adopt', async (request) =>
    guarded(async () => {
      const params = body(Strict({ id: Uuid, requestId: Uuid }), request.params),
        input = body(BuildingAiStudioAdopt, request.body);
      const a = await accountOf(request);
      await studio.own(a.id, params.id);
      const candidate = await app.services.db
        .selectFrom('building_studio_revisions')
        .select('hash')
        .where('thread_id', '=', params.id)
        .where('request_id', '=', params.requestId)
        .executeTakeFirst();
      if (!candidate) throw apiError('not_found', 'Candidate unavailable.');
      const archive = Buffer.from(
        await new AgentBlobs(app.services.blobs, app.services.db).read(
          candidate.hash,
          32 * 1024 * 1024,
        ),
      );
      await studio.adopt(
        (await accountOf(request)).id,
        params.id,
        params.requestId,
        input.expectedRevision,
        archive,
      );
      return { ok: true };
    }),
  );
  app.post(
    '/api/v1/ai-building-studio/threads/:id/references',
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
        const hash = buildingAssetHash(bytes);
        const artifact = await app.services.db.transaction().execute(async (db) => {
          // Reference capacity is account-wide. Match the studio's account-first
          // lock order so concurrent uploads into different projects cannot each
          // pass the aggregate quota independently.
          await sql`SELECT id FROM accounts WHERE id=${a.id} FOR UPDATE`.execute(db);
          await sql`SELECT id FROM building_studio_threads WHERE id=${thread} FOR UPDATE`.execute(
            db,
          );
          await studio.own(a.id, thread, db);
          const usage = (
            await sql<{ bytes: string; existing: boolean | null }>`
              SELECT coalesce(sum(size),0)::bigint AS bytes,bool_or(hash=${hash}) AS existing
              FROM (
                SELECT DISTINCT r.hash,b.size FROM building_studio_artifacts r
                JOIN building_studio_threads t ON t.id=r.thread_id
                JOIN blobs b ON b.sha256=r.hash
                WHERE t.account_id=${a.id} AND r.kind='reference'
              ) retained_references`.execute(db)
          ).rows[0];
          if (Number(usage?.bytes ?? 0) + (usage?.existing ? 0 : bytes.length) > 64 * 1024 * 1024)
            throw apiError(
              'bad_request',
              'Your building references are limited to 64 MiB. Delete old project history before uploading more.',
            );
          const n =
            (
              await sql<{
                n: number;
              }>`SELECT count(*)::int n FROM building_studio_artifacts WHERE thread_id=${thread} AND kind='reference'`.execute(
                db,
              )
            ).rows[0]?.n ?? 0;
          if (n >= 64) throw apiError('bad_request', 'Reference limit reached for this project.');
          // Reject known capacity failures before persisting payloads. Holding the
          // account lock across this bounded write also serializes cross-project
          // uploads; GC still collects an orphan if the artifact commit fails.
          await new AgentBlobs(app.services.blobs, app.services.db).write(bytes, 'image/png');
          return (
            (
              await sql<{
                id: string;
              }>`INSERT INTO building_studio_artifacts(thread_id,stage,kind,label,hash) VALUES(${thread},'prepare','reference','Uploaded visual reference',${hash}) RETURNING id`.execute(
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
          url: `/api/v1/ai-building-studio/threads/${thread}/artifacts/${artifact.id}`,
          label: 'Uploaded visual reference',
        };
      }),
  );
  app.post('/api/v1/ai-building-studio/checkout', async (request) =>
    guarded(async () => {
      const account = await accountOf(request);
      if (!enabled || !config?.salesEnabled || !checkout)
        throw apiError('forbidden', 'Building-credit sales are not enabled.');
      return checkout.begin(
        account.id,
        body(Strict({ pack: Type.String({ minLength: 1, maxLength: 64 }) }), request.body).pack,
      );
    }),
  );
  app.post('/api/v1/ai-building-studio/threads/:id/requests/:requestId/cancel', async (request) =>
    guarded(async () => {
      const params = body(Strict({ id: Uuid, requestId: Uuid }), request.params);
      await studio.cancel((await accountOf(request)).id, params.id, params.requestId);
      return { ok: true };
    }),
  );
  // Long poll uses cross-replica notifications; timeout forces a state refresh after lost notifications.
  app.get('/api/v1/ai-building-studio/threads/:id/updates', async (request, reply) =>
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
          .subscribe(BUILDING_STUDIO_CHANNEL, (payload) => {
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
  app.post('/api/v1/admin/ai-building-studio/requests/:id/fail', async (request) =>
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
      scope.post('/api/v1/ai-building-studio/stripe', async (request) => {
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
