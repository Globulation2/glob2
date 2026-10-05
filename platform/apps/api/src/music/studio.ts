import { sql } from 'kysely';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import { Checkout, HiveError } from '@glob2/billing';
import { MusicStudio, MUSIC_STUDIO_CHANNEL } from '@glob2/music-studio';
import {
  MusicStudioCreate,
  MusicStudioMessage,
  MusicStudioGenerate,
  Strict,
  Uuid,
} from '@glob2/protocol';
import { Type } from 'typebox';
import { requireAccount } from '../identity.ts';
import { body } from '../http/validate.ts';
import { apiError } from '../errors.ts';
import { streamStudioEvents } from '../maps/studioEvents.ts';
export async function musicStudioRoutes(app: FastifyInstance) {
  const studio = new MusicStudio(app.services.db),
    config = app.services.config.instance.musicStudio;
  const enabled = !!config?.enabled;
  const streams = new Map<string, number>();
  let totalStreams = 0;
  if (
    enabled &&
    (!config.textModel ||
      !config.pipelineVersion ||
      !config.providerCallsPerDay ||
      !config.maxCalls ||
      !config.maxOutputTokens ||
      !config.maxTotalTokens ||
      !config.timeoutSeconds)
  )
    throw new Error(
      'AI Music Studio needs a text model, a pipeline version and a provider-call budget.',
    );
  const checkout =
    process.env['MUSIC_STRIPE_SECRET_KEY'] && process.env['MUSIC_STRIPE_WEBHOOK_SECRET']
      ? new Checkout(
          app.services.db,
          process.env['MUSIC_STRIPE_SECRET_KEY'] ?? '',
          process.env['MUSIC_STRIPE_WEBHOOK_SECRET'] ?? '',
          app.services.config.publicOrigin,
          config?.packs ?? [],
          'music',
        )
      : undefined;
  const accountOf = async (request: FastifyRequest) => {
    const { account } = await requireAccount(app.identity, request);
    if (account.kind !== 'registered')
      throw apiError('forbidden', 'Sign in with a registered account to use AI Music Studio.');
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
    if (!enabled) throw apiError('forbidden', 'AI Music Studio is not enabled on this instance.');
  };
  app.get('/api/v1/music-studio/account', async (request) => {
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
        .selectFrom('music_ledger')
        .select(['id', 'amount', 'kind', 'created_at'])
        .where('account_id', '=', account.id)
        .orderBy('created_at', 'desc')
        .limit(30)
        .execute(),
    };
  });
  app.get('/api/v1/music-studio/threads', async (request) => ({
    items: await studio.list((await accountOf(request)).id),
  }));
  app.post('/api/v1/music-studio/threads', async (request) =>
    guarded(async () => {
      requireEnabled();
      const input = body(MusicStudioCreate, request.body);
      return studio.create((await accountOf(request)).id, input.title, input.id);
    }),
  );
  app.delete('/api/v1/music-studio/threads/:id', async (request) =>
    guarded(async () => {
      await studio.remove((await accountOf(request)).id, threadOf(request));
      return { deleted: true };
    }),
  );
  app.get('/api/v1/music-studio/threads/:id', async (request, reply) =>
    guarded(async () => {
      reply.header('cache-control', 'no-store');
      const before = body(
        Strict({ messagesBefore: Type.Optional(Uuid), requestsBefore: Type.Optional(Uuid) }),
        request.query,
      );
      return studio.get((await accountOf(request)).id, threadOf(request), before);
    }),
  );
  app.get('/api/v1/music-studio/threads/:id/requests/:requestId/progress', async (request, reply) =>
    guarded(async () => {
      const params = body(Strict({ id: Uuid, requestId: Uuid }), request.params);
      reply.header('cache-control', 'no-store');
      return studio.progress((await accountOf(request)).id, params.id, params.requestId);
    }),
  );
  app.get('/api/v1/music-studio/threads/:id/artifacts/:artifactId', async (request, reply) =>
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
      if (!stream) throw apiError('not_found', 'The stage audio is not available.');
      return reply
        .header('content-type', blob.content_type)
        .header('cache-control', 'private, no-store')
        .header('x-content-type-options', 'nosniff')
        .send(stream);
    }),
  );
  app.get('/api/v1/music-studio/threads/:id/events', async (request, reply) =>
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
        throw apiError('rate_limited', 'Too many live Music Studio connections.');
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
          channel: MUSIC_STUDIO_CHANNEL,
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
  app.post('/api/v1/music-studio/threads/:id/messages', async (request) =>
    guarded(async () => {
      requireEnabled();
      return studio.submit(
        (await accountOf(request)).id,
        threadOf(request),
        'chat',
        body(MusicStudioMessage, request.body),
        config?.pipelineVersion ?? '',
        config?.chatPerHour ?? 60,
        config,
      );
    }),
  );
  app.post('/api/v1/music-studio/threads/:id/generate', async (request) =>
    guarded(async () => {
      requireEnabled();
      return studio.submit(
        (await accountOf(request)).id,
        threadOf(request),
        'generate',
        body(MusicStudioGenerate, request.body),
        config?.pipelineVersion ?? '',
        config?.chatPerHour ?? 60,
        config,
      );
    }),
  );
  app.post('/api/v1/music-studio/checkout', async (request) =>
    guarded(async () => {
      const account = await accountOf(request);
      if (!enabled || !config?.salesEnabled || !checkout)
        throw apiError('forbidden', 'Music-credit sales are not enabled.');
      return checkout.begin(
        account.id,
        body(Strict({ pack: Type.String({ minLength: 1, maxLength: 64 }) }), request.body).pack,
      );
    }),
  );
  app.post('/api/v1/music-studio/threads/:id/requests/:requestId/cancel', async (request) =>
    guarded(async () => {
      const params = body(Strict({ id: Uuid, requestId: Uuid }), request.params);
      await studio.cancel((await accountOf(request)).id, params.id, params.requestId);
      return { ok: true };
    }),
  );
  // Long poll uses cross-replica notifications; timeout forces a state refresh after lost notifications.
  app.get('/api/v1/music-studio/threads/:id/updates', async (request, reply) =>
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
          .subscribe(MUSIC_STUDIO_CHANNEL, (payload) => {
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
  app.post('/api/v1/admin/music-studio/requests/:id/fail', async (request) =>
    guarded(async () => {
      const account = await accountOf(request);
      if (account.role !== 'admin') throw apiError('forbidden', 'Administrator access required.');
      const id = threadOf(request),
        row = await studio.request(id);
      if (!row || row.status !== 'uncertain')
        throw apiError('conflict', 'Only uncertain requests need reconciliation.');
      await studio.finish(
        row,
        undefined,
        'Generation could not be recovered. Your credit was returned.',
      );
      await sql`INSERT INTO admin_audit_log(actor_account_id,action,target_type,target_id,details) VALUES(${account.id},'music-studio.fail','music-studio-request',${id},'{}'::jsonb)`.execute(
        app.services.db,
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
      scope.post('/api/v1/music-studio/stripe', async (request) => {
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
