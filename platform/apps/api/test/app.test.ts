import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import type { FastifyInstance } from 'fastify';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { PgPubSub } from '@glob2/db';
import {
  DEFAULT_INSTANCE_CONFIG,
  JobQueue,
  allowAllPolicy,
  createLogger,
  FsBlobStore,
  prepareJobQueue,
  type PlatformConfig,
} from '@glob2/core';
import { checkDocument, simVersionKey } from '@glob2/protocol';
import { buildApp } from '../src/app.ts';
import { SigningKeys } from '../src/auth/keys.ts';

const logger = createLogger('api-test', 'silent');
const SIM = { versionMinor: 125, netProtocol: 49, dataHash: 'ab'.repeat(32) };
const OLD_SIM = { versionMinor: 124, netProtocol: 48, dataHash: 'cd'.repeat(32) };

let database: TestDatabase;
let app: FastifyInstance;
let pubsub: PgPubSub;
let jobs: JobQueue;

beforeAll(async () => {
  database = await createTestDatabase();
  await prepareJobQueue(database.pool, logger);
  jobs = await JobQueue.create(database.pool, logger);
  pubsub = new PgPubSub({ connectionString: database.url });
  const config: PlatformConfig = {
    publicOrigin: 'https://play.example.org',
    databaseUrl: database.url,
    logLevel: 'silent',
    http: { host: '127.0.0.1', port: 0 },
    blobs: { kind: 'fs', directory: '/tmp/unused' },
    shutdownGraceSeconds: 1,
    instance: {
      ...DEFAULT_INSTANCE_CONFIG,
      name: 'Test Instance',
      queues: [
        {
          id: 'ranked-1v1',
          name: 'Ranked 1v1',
          mode: '1v1',
          rated: true,
          aiBackfillSeconds: 90,
          mapPool: [
            {
              generatorId: 'even-ground',
              revision: 3,
              params: {},
              candidates: 5,
              startingUnitLevel: 0,
            },
          ],
        },
      ],
    },
    instanceConfigPath: undefined,
  };
  app = await buildApp({
    config,
    logger,
    db: database.db,
    pubsub,
    jobs,
    blobs: new FsBlobStore('/tmp/unused'),
    access: allowAllPolicy,
    keys: SigningKeys.ephemeral(),
  });
});

afterAll(async () => {
  await app?.close();
  await pubsub?.close();
  await jobs?.close();
  await database?.drop();
});

describe('platform api', () => {
  it('reports liveness and readiness', async () => {
    expect((await app.inject('/healthz')).json()).toEqual({ status: 'ok' });
    const ready = await app.inject('/readyz');
    expect(ready.statusCode).toBe(200);
  });

  it('describes the instance with the sim versions its engine agents serve', async () => {
    await database.db
      .insertInto('engine_agents')
      .values([
        { id: 'fresh', sim_version: simVersionKey(SIM), kinds: ['verify-match'], build: 't' },
        {
          id: 'stale',
          sim_version: simVersionKey(OLD_SIM),
          kinds: ['verify-match'],
          build: 't',
          last_seen_at: new Date(Date.now() - 3_600_000),
        },
      ])
      .execute();
    const response = await app.inject('/api/v1/instance');
    expect(response.statusCode).toBe(200);
    const body = response.json();
    expect(checkDocument('InstanceInfo', body)).toEqual({ stage: 'ok', issues: [] });
    expect(body.name).toBe('Test Instance');
    expect(body.realtimeUrl).toBe('wss://play.example.org/realtime');
    expect(body.supportedSimVersions).toEqual([SIM]);
    expect(body.queues).toEqual([
      {
        id: 'ranked-1v1',
        name: 'Ranked 1v1',
        mode: '1v1',
        rated: true,
        aiBackfillSeconds: 90,
        acceptSeconds: 10,
        maps: ['even-ground'],
      },
    ]);
  });

  it('answers unknown routes with a protocol error body', async () => {
    const response = await app.inject('/api/v1/nothing');
    expect(response.statusCode).toBe(404);
    expect(checkDocument('ErrorBody', response.json()).stage).toBe('ok');
    expect(response.json().code).toBe('not_found');
  });
});
