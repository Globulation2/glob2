import { Type } from 'typebox';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import { Credits, Sessions, Commander, OpenAICommander, Checkout, HiveError } from '@glob2/hive';
import {
  HiveContext,
  HiveCommand,
  HiveClientPoll,
  HiveToolResult,
  HiveWake,
  HiveCheckout,
  Strict,
  Uuid,
} from '@glob2/protocol';
import { requireAccount } from '../identity.ts';
import { body } from '../http/validate.ts';
import { apiError } from '../errors.ts';

export async function hiveRoutes(app: FastifyInstance) {
  const config = app.services.config.instance.hiveMind;
  const credits = new Credits(app.services.db),
    sessions = new Sessions(app.services.db);
  const key = process.env['HIVE_OPENAI_API_KEY'];
  const commander =
    config?.enabled && config.model && config.rate && key
      ? new Commander(app.services.db, new OpenAICommander(key, config.model), {
          ...config.rate,
          model: config.model,
        })
      : undefined;
  const checkout =
    config?.enabled && config.salesEnabled && config.packs?.length
      ? new Checkout(
          app.services.db,
          process.env['HIVE_STRIPE_SECRET_KEY'] ?? '',
          process.env['HIVE_STRIPE_WEBHOOK_SECRET'] ?? '',
          app.services.config.publicOrigin,
          config.packs,
        )
      : undefined;
  if (config?.enabled && !commander)
    throw new Error(
      'Enabled Hive Mind requires a configured model, rate card and HIVE_OPENAI_API_KEY.',
    );
  const tasks = new Set<Promise<void>>();
  const launch = (id: string) => {
    if (!commander) return;
    const task = commander
      .run(id)
      .catch((error) => app.log.error({ err: error }, 'commander run failed'))
      .finally(() => tasks.delete(task));
    tasks.add(task);
  };
  app.addHook('onClose', async () => {
    commander?.shutdown();
    await Promise.allSettled(tasks);
  });
  app.get('/api/v1/hive/account', async (request) => {
    const { account } = await requireAccount(app.identity, request);
    return {
      enabled: !!commander,
      ...(await credits.balance(account.id)),
      rate: config?.rate ?? null,
      usage: await app.services.db
        .selectFrom('hive_ledger')
        .select(['id', 'amount', 'kind', 'created_at'])
        .where('account_id', '=', account.id)
        .orderBy('created_at', 'desc')
        .limit(30)
        .execute(),
      packs: checkout
        ? config?.packs?.map(({ id, credits, amount, currency }) => ({
            id,
            credits,
            amount,
            currency,
          }))
        : [],
    };
  });
  // Errors remain player-facing; private interpreter diagnostics only travel in execution results.
  const guarded = async <T>(fn: () => Promise<T>) => {
    try {
      return await fn();
    } catch (error) {
      if (error instanceof HiveError)
        throw apiError(
          error.code === 'not_found'
            ? 'not_found'
            : error.code === 'forbidden'
              ? 'forbidden'
              : error.code === 'conflict'
                ? 'conflict'
                : 'bad_request',
          error.message,
        );
      throw error;
    }
  };
  const current = async (request: FastifyRequest) => {
    if (!commander) throw apiError('forbidden', 'Hive Mind is not available on this instance.');
    const { account } = await requireAccount(app.identity, request);
    const params = body(HiveContext, request.params);
    const self = await app.identity.accounts.selfView(account);
    const decision = await app.services.access.canUseHiveMind(
      {
        accountId: account.id,
        kind: account.kind,
        role: account.role,
        entitlements: self.entitlements,
      },
      params,
    );
    if (!decision.allowed) throw apiError('access_denied', decision.reason);
    return sessions.open(account.id, params.matchId, params.seat);
  };
  // Path numbers are strings; normalize only the known seat field before schema validation.
  app.addHook('preValidation', async (request) => {
    if (request.routeOptions.url?.startsWith('/api/v1/hive/matches/')) {
      const params = request.params as { seat?: string | number };
      if (typeof params.seat === 'string' && /^\d{1,2}$/.test(params.seat))
        params.seat = Number(params.seat);
    }
  });
  const prefix = '/api/v1/hive/matches/:matchId/:seat';
  app.post(prefix + '/poll', async (request) =>
    guarded(async () => {
      const s = await current(request),
        p = body(HiveClientPoll, request.body);
      const result = await sessions.poll(s.id, p.clientId, p.lease, p.tick, p.caughtUp);
      if (p.caughtUp && (await sessions.get(s.id)).pending_run) launch(s.id);
      return result;
    }),
  );
  app.get(prefix + '/events', async (request) =>
    guarded(async () => {
      const s = await current(request),
        query = body(
          Strict({ after: Type.Optional(Type.String({ pattern: '^[0-9]{1,18}$' })) }),
          request.query,
        );
      return { events: await sessions.events(s.id, query.after) };
    }),
  );
  app.post(prefix + '/command', async (request) =>
    guarded(async () => {
      const s = await current(request),
        command = body(HiveCommand, request.body);
      if ((await credits.balance(s.account_id)).available <= 0)
        throw new HiveError(
          'credits',
          'Your commander needs more credits. Standing orders remain active.',
        );
      commander?.stop(s.id);
      if (await sessions.command(s.id, command.id, command.text, command.ongoing)) launch(s.id);
      return { accepted: true };
    }),
  );
  app.post(prefix + '/stop', async (request) =>
    guarded(async () => {
      const s = await current(request);
      commander?.stop(s.id);
      await sessions.stop(s.id);
      return { stopped: true };
    }),
  );
  app.post(prefix + '/result', async (request) =>
    guarded(async () => {
      const s = await current(request),
        r = body(HiveToolResult, request.body);
      return {
        accepted: await sessions.result(s.id, r.operationId, r.lease, r.status, r.output, r.tick),
      };
    }),
  );
  app.post(prefix + '/wake', async (request) =>
    guarded(async () => {
      const s = await current(request),
        r = body(Strict({ lease: Uuid, wake: HiveWake }), request.body);
      if (await sessions.wake(s.id, r.wake, r.lease)) launch(s.id);
      return { accepted: true };
    }),
  );
  app.post(prefix + '/standing-orders', async (request) =>
    guarded(async () => {
      const s = await current(request),
        p = body(
          Strict({
            programId: Uuid,
            expectedRevision: Type.Integer({ minimum: 1 }),
            action: Type.Union([
              Type.Literal('pause'),
              Type.Literal('resume'),
              Type.Literal('remove'),
            ]),
          }),
          request.body,
        );
      return {
        operationId: await sessions.enqueue(s.id, s.generation, {
          kind: p.action,
          programId: p.programId,
          expectedRevision: p.expectedRevision,
        }),
      };
    }),
  );
  app.post('/api/v1/hive/checkout', async (request) =>
    guarded(async () => {
      const { account } = await requireAccount(app.identity, request);
      if (account.kind !== 'registered' || !checkout)
        throw apiError('forbidden', 'Credit purchases are not available.');
      return checkout.begin(account.id, body(HiveCheckout, request.body).pack);
    }),
  );
  if (checkout)
    await app.register(async (scoped) => {
      scoped.removeContentTypeParser('application/json');
      scoped.addContentTypeParser(
        'application/json',
        { parseAs: 'buffer', bodyLimit: 262144 },
        (_request, raw, done) => done(null, raw),
      );
      scoped.post('/api/v1/hive/stripe', async (request, reply) => {
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
        return reply.send({ received: true });
      });
    });
}
