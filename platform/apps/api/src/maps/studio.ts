import { sql } from 'kysely';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import { Checkout, HiveError } from '@glob2/billing';
import { Studio, STUDIO_CHANNEL } from '@glob2/map-studio';
import {
  StudioCreate,
  StudioMessage,
  StudioGenerate,
  Strict,
  Uuid,
  parseSimVersionKey,
} from '@glob2/protocol';
import { Type } from 'typebox';
import { requireAccount } from '../identity.ts';
import { body } from '../http/validate.ts';
import type { RoomService } from '../play/rooms.ts';
import { supportedSimVersions } from '../app.ts';
import { apiError } from '../errors.ts';
export async function studioRoutes(app: FastifyInstance, rooms: RoomService) {
  const studio = new Studio(app.services.db),
    config = app.services.config.instance.mapStudio;
  const enabled = !!config?.enabled;
  if (
    enabled &&
    (!config.textModel ||
      !config.imageModel ||
      !config.pipelineVersion ||
      !config.providerCallsPerDay)
  )
    throw new Error(
      'AI Map Studio needs text/image models, a pipeline version and a provider-call budget.',
    );
  const checkout =
    process.env['MAP_STRIPE_SECRET_KEY'] && process.env['MAP_STRIPE_WEBHOOK_SECRET']
      ? new Checkout(
          app.services.db,
          process.env['MAP_STRIPE_SECRET_KEY'] ?? '',
          process.env['MAP_STRIPE_WEBHOOK_SECRET'] ?? '',
          app.services.config.publicOrigin,
          config?.packs ?? [],
          'maps',
        )
      : undefined;
  const accountOf = async (request: FastifyRequest) => {
    const { account } = await requireAccount(app.identity, request);
    if (account.kind !== 'registered')
      throw apiError('forbidden', 'Sign in with a registered account to use AI Map Studio.');
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
    if (!enabled) throw apiError('forbidden', 'AI Map Studio is not enabled on this instance.');
  };
  app.get('/api/v1/map-studio/account', async (request) => {
    const account = await accountOf(request);
    return {
      enabled,
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
        .selectFrom('map_ledger')
        .select(['id', 'amount', 'kind', 'created_at'])
        .where('account_id', '=', account.id)
        .orderBy('created_at', 'desc')
        .limit(30)
        .execute(),
    };
  });
  app.get('/api/v1/map-studio/threads', async (request) => ({
    items: await studio.list((await accountOf(request)).id),
  }));
  app.post('/api/v1/map-studio/threads', async (request) => {
    requireEnabled();
    return studio.create((await accountOf(request)).id, body(StudioCreate, request.body).title);
  });
  app.get('/api/v1/map-studio/threads/:id', async (request, reply) =>
    guarded(async () => {
      reply.header('cache-control', 'no-store');
      const before = body(
        Strict({ messagesBefore: Type.Optional(Uuid), requestsBefore: Type.Optional(Uuid) }),
        request.query,
      );
      return studio.get((await accountOf(request)).id, threadOf(request), before);
    }),
  );
  app.post('/api/v1/map-studio/threads/:id/messages', async (request) =>
    guarded(async () => {
      requireEnabled();
      return studio.submit(
        (await accountOf(request)).id,
        threadOf(request),
        'chat',
        body(StudioMessage, request.body),
        config?.pipelineVersion ?? '',
        config?.chatPerHour ?? 60,
      );
    }),
  );
  app.post('/api/v1/map-studio/threads/:id/generate', async (request) =>
    guarded(async () => {
      requireEnabled();
      return studio.submit(
        (await accountOf(request)).id,
        threadOf(request),
        'generate',
        body(StudioGenerate, request.body),
        config?.pipelineVersion ?? '',
      );
    }),
  );
  app.post('/api/v1/map-studio/checkout', async (request) =>
    guarded(async () => {
      const account = await accountOf(request);
      if (!enabled || !config?.salesEnabled || !checkout)
        throw apiError('forbidden', 'Map-credit sales are not enabled.');
      return checkout.begin(
        account.id,
        body(Strict({ pack: Type.String({ minLength: 1, maxLength: 64 }) }), request.body).pack,
      );
    }),
  );
  app.post('/api/v1/map-studio/threads/:id/versions/:version/room', async (request) =>
    guarded(async () => {
      const account = await accountOf(request);
      const params = body(Strict({ id: Uuid, version: Uuid }), request.params);
      await studio.own(account.id, params.id);
      const version = await studio.request(params.version);
      if (
        !version ||
        version.thread_id !== params.id ||
        version.status !== 'ready' ||
        !version.map_id ||
        !version.map_hash
      )
        throw apiError('not_found', 'No such delivered map.');
      const existing = await app.services.db
        .selectFrom('rooms')
        .select('code')
        .where('host_account_id', '=', account.id)
        .where('status', '=', 'open')
        .where(sql<string>`settings->'map'->>'mapId'`, '=', version.map_id)
        .executeTakeFirst();
      if (existing) return { code: existing.code };
      const saved = await app.services.db
        .selectFrom('map_versions')
        .select('sim_version')
        .where('map_id', '=', version.map_id)
        .where('hash', '=', version.map_hash)
        .executeTakeFirst();
      const sim = saved?.sim_version ? parseSimVersionKey(saved.sim_version) : undefined;
      if (
        !sim ||
        !(await supportedSimVersions(app.services.db)).some(
          (v) =>
            v.versionMinor === sim.versionMinor &&
            v.netProtocol === sim.netProtocol &&
            v.dataHash === sim.dataHash,
        )
      )
        throw apiError('unavailable', 'No compatible engine is available for this map.');
      const thread = await studio.own(account.id, params.id);
      const room = await rooms.create(account, sim, {
        name: thread.title.slice(0, 64),
        visibility: 'link',
        map: { kind: 'catalog', mapId: version.map_id, hash: version.map_hash },
      });
      return { code: room.code };
    }),
  );
  // Long poll uses cross-replica notifications; timeout forces a state refresh after lost notifications.
  app.get('/api/v1/map-studio/threads/:id/updates', async (request, reply) =>
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
          .subscribe(STUDIO_CHANNEL, (payload) => {
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
  app.post('/api/v1/admin/map-studio/requests/:id/fail', async (request) =>
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
      await sql`INSERT INTO admin_audit_log(actor_account_id,action,target_type,target_id,details) VALUES(${account.id},'studio.fail','studio-request',${id},'{}'::jsonb)`.execute(
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
      scope.post('/api/v1/map-studio/stripe', async (request) => {
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
