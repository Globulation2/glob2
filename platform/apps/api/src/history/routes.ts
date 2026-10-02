// History, leaderboard and profile REST (read only; see service.ts for the
// visibility rules) and the moderators' match lookup.
import type { FastifyInstance, FastifyRequest } from 'fastify';
import type {
  AiLeaderboard,
  LeaderboardPage,
  MatchDetail,
  MatchList,
  PlayerProfile,
} from '@glob2/protocol';
import { supportedSimVersions } from '../app.ts';
import { apiError } from '../errors.ts';
import { authenticate, requireRole, type Identity } from '../identity.ts';
import { HistoryService, type Viewer } from './service.ts';
import { pageLimit } from './summaries.ts';

interface PageQuery {
  cursor?: string;
  limit?: string;
  queue?: string;
}

const QUEUE = /^(room|[a-z0-9][a-z0-9-]{0,63})$/;

function queueFilter(value: string | undefined): string | undefined {
  if (value === undefined || value === '') return undefined;
  if (!QUEUE.test(value)) throw apiError('bad_request', 'Unknown queue.');
  return value;
}

export async function historyRoutes(app: FastifyInstance, identity: Identity): Promise<void> {
  const { services } = app;
  const history = new HistoryService({
    db: services.db,
    origin: services.config.publicOrigin,
    queueNames: new Map(services.config.instance.queues.map((q) => [q.id, q.name])),
    currentSimVersions: () => supportedSimVersions(services.db),
  });

  const viewerOf = async (request: FastifyRequest): Promise<Viewer | undefined> => {
    const caller = await authenticate(identity, request);
    return caller ? { account: caller.account } : undefined;
  };

  app.get<{ Params: { ladder: string }; Querystring: PageQuery & { provisional?: string } }>(
    '/api/v1/leaderboards/:ladder',
    async (request): Promise<LeaderboardPage> => {
      const { provisional = 'include' } = request.query;
      if (provisional !== 'include' && provisional !== 'exclude') {
        throw apiError('bad_request', 'provisional must be include or exclude.');
      }
      return history.leaderboard(request.params.ladder, {
        ...(request.query.cursor ? { cursor: request.query.cursor } : {}),
        limit: pageLimit(request.query.limit, 50, 200),
        provisional,
      });
    },
  );

  app.get<{ Params: { ladder: string } }>(
    '/api/v1/leaderboards/:ladder/ai',
    async (request): Promise<AiLeaderboard> => history.aiLeaderboard(request.params.ladder),
  );

  app.get<{ Params: { id: string } }>(
    '/api/v1/players/:id',
    async (request): Promise<PlayerProfile> =>
      history.profile(request.params.id, await viewerOf(request)),
  );

  app.get<{ Params: { id: string }; Querystring: PageQuery }>(
    '/api/v1/players/:id/matches',
    async (request): Promise<MatchList> => {
      const queue = queueFilter(request.query.queue);
      return history.playerMatches(request.params.id, await viewerOf(request), {
        ...(request.query.cursor ? { cursor: request.query.cursor } : {}),
        limit: pageLimit(request.query.limit, 20, 100),
        ...(queue ? { queue } : {}),
      });
    },
  );

  app.get<{ Querystring: PageQuery }>('/api/v1/matches', async (request): Promise<MatchList> => {
    const queue = queueFilter(request.query.queue);
    return history.recentMatches({
      ...(request.query.cursor ? { cursor: request.query.cursor } : {}),
      limit: pageLimit(request.query.limit, 20, 100),
      ...(queue ? { queue } : {}),
    });
  });

  app.get<{ Params: { id: string } }>(
    '/api/v1/matches/:id',
    async (request): Promise<MatchDetail> =>
      history.matchDetail(request.params.id, await viewerOf(request)),
  );

  app.get<{ Params: { id: string; kind: string } }>(
    '/api/v1/matches/:id/artifacts/:kind',
    async (request, reply) => {
      const artifact = await history.artifact(
        request.params.id,
        request.params.kind,
        await viewerOf(request),
      );
      const stream = await services.blobs.get(artifact.storageKey);
      if (!stream) throw apiError('not_found', 'The file is missing from storage.');
      const extension = { record: 'g2mr', replay: 'replay', result: 'json' }[artifact.kind];
      const name = `glob2-match-${request.params.id.slice(0, 8)}.${extension}`;
      void reply
        .header(
          'content-type',
          artifact.kind === 'result' ? 'application/json' : 'application/octet-stream',
        )
        .header('content-length', String(artifact.size))
        .header('content-disposition', `attachment; filename="${name}"`)
        .header('etag', `"${artifact.sha256}"`)
        .header(
          'cache-control',
          artifact.kind === 'record' ? 'private, max-age=3600' : 'public, max-age=86400, immutable',
        )
        .header('x-content-type-options', 'nosniff');
      // Replays and results are public: the browser client may fetch them
      // from another origin (Watch in browser on a separately hosted client).
      if (artifact.kind !== 'record') void reply.header('access-control-allow-origin', '*');
      return reply.send(stream);
    },
  );

  app.get<{ Querystring: PageQuery & { q?: string; status?: string } }>(
    '/api/v1/admin/matches',
    async (request): Promise<MatchList> => {
      await requireRole(identity, request, 'moderator');
      return history.adminMatches({
        ...(request.query.q ? { q: String(request.query.q) } : {}),
        ...(request.query.status ? { status: request.query.status } : {}),
        ...(request.query.cursor ? { cursor: request.query.cursor } : {}),
        limit: pageLimit(request.query.limit, 50, 100),
      });
    },
  );
}
