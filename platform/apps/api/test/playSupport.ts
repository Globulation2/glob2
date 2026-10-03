// Fixtures for room, match and relay tests: a fake engine agent, signed-in
// sockets, relay calls and ticket verification against the JWKS.
import { createPublicKey, randomBytes } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import { expect } from 'vitest';
import { putContent, type BlobStore } from '@glob2/core';
import type { Database } from '@glob2/db';
import {
  MATCH_TICKET_AUDIENCE,
  MATCH_TICKET_TYPE,
  checkDocument,
  simVersionKey,
  type EngineJobKind,
  type GeneratorDescriptor,
  type SimVersion,
} from '@glob2/protocol';
import { verifyJwt } from '@glob2/protocol/node';
import { handleEngineJobResult } from '@glob2/worker';
import { RealtimeClient, SIM, json, postJson, type Instance } from './support.ts';

export const RELAY_KEY = `relay-key-${'k'.repeat(32)}`;
export const PINNED_KEY = `pinned-key-${'p'.repeat(32)}`;
export const OTHER_SIM = { ...SIM, dataHash: 'cd'.repeat(32) };

/** Announces an engine agent for SIM, so the instance serves that sim version. */
export async function serveSim(db: Kysely<Database>, sim: SimVersion = SIM): Promise<void> {
  await db
    .insertInto('engine_agents')
    .values({
      id: `fake-agent-${simVersionKey(sim).slice(0, 12)}`,
      sim_version: simVersionKey(sim),
      kinds: ['generate-map', 'validate-map', 'render-preview', 'verify-match'],
      build: 'fake',
    })
    .onConflict((oc) => oc.column('id').doUpdateSet({ last_seen_at: sql<Date>`now()` }))
    .execute();
}

/** Bytes of a fake map: clients would load these. */
export function fakeMapBytes(teams: number, seed: number): Buffer {
  return Buffer.from(`GLOB2MAP:${teams}:${seed}`);
}

export function fakeSaveBytes(players: { name: string; team: number; kind: 'human' | 'ai' }[]) {
  return Buffer.from(`GLOB2SAVE:${players.length}:${JSON.stringify(players)}`);
}

/**
 * Plays the engine agent (and the worker's result task): completes queued
 * generate-map and validate-map jobs straight from the engine_jobs table.
 */
export class FakeEngine {
  private readonly db: Kysely<Database>;
  private readonly blobs: BlobStore;
  private timer: NodeJS.Timeout | undefined;
  private running = false;
  /** Generation fails while set. */
  failGeneration = false;
  readonly ran: { kind: EngineJobKind; jobId: string }[] = [];

  constructor(db: Kysely<Database>, blobs: BlobStore) {
    this.db = db;
    this.blobs = blobs;
  }

  start(intervalMs = 30): void {
    this.timer = setInterval(() => {
      if (this.running) return;
      this.running = true;
      void this.runPending()
        .catch(() => undefined)
        .finally(() => (this.running = false));
    }, intervalMs);
  }

  stop(): void {
    clearInterval(this.timer);
  }

  async runPending(): Promise<number> {
    const jobs = await this.db
      .selectFrom('engine_jobs')
      .select(['id', 'kind', 'payload'])
      .where('status', '=', 'queued')
      .where('kind', 'in', ['generate-map', 'validate-map'])
      .orderBy('created_at')
      .execute();
    for (const job of jobs) {
      const result = await this.run(job.kind, job.payload);
      await handleEngineJobResult(this.db, {
        jobId: job.id,
        kind: job.kind,
        agent: 'fake',
        ...result,
      });
      this.ran.push({ kind: job.kind, jobId: job.id });
    }
    return jobs.length;
  }

  private async run(kind: EngineJobKind, payload: unknown) {
    if (kind === 'generate-map') {
      const { generator } = payload as { generator: GeneratorDescriptor };
      if (this.failGeneration) {
        return { ok: false, error: { code: 'internal', message: 'generator refused' } };
      }
      const teams = generator.params['teams'] ?? 2;
      const stored = await putContent(this.blobs, fakeMapBytes(teams, generator.seed));
      return {
        ok: true,
        result: {
          mapHash: stored.sha256,
          size: stored.size,
          map: { width: 128, height: 128, teamCount: teams },
          chosenSeed: generator.seed,
        },
      };
    }
    const { blobHash, format } = payload as { blobHash: string; format: 'map' | 'save' };
    const stream = await this.blobs.get(`sha256/${blobHash.slice(0, 2)}/${blobHash}`);
    let text = '';
    for await (const chunk of stream ?? []) text += String(chunk);
    const map = /^GLOB2MAP:(\d+):/.exec(text);
    const save = /^GLOB2SAVE:(\d+):(.*)$/s.exec(text);
    if (format === 'map' && map) {
      return {
        ok: true,
        result: {
          valid: true,
          mapHash: blobHash,
          map: { width: 64, height: 64, teamCount: Number(map[1]) },
          versionMinor: SIM.versionMinor,
          title: 'Uploaded map',
        },
      };
    }
    if (format === 'save' && save) {
      return {
        ok: true,
        result: {
          valid: true,
          mapHash: blobHash,
          map: { width: 64, height: 64, teamCount: Number(save[1]) },
          versionMinor: SIM.versionMinor,
          players: JSON.parse(save[2]!),
        },
      };
    }
    return { ok: true, result: { valid: false, reason: 'not a Globulation 2 file' } };
  }
}

export interface Player {
  accountId: string;
  displayName: string;
  accessToken: string;
  client: RealtimeClient;
}

/** A new guest with a realtime socket on the given replica. */
export async function guestPlayer(instance: Instance, sim: SimVersion = SIM): Promise<Player> {
  const session = (await json(
    await postJson(`${instance.url}/api/v1/auth/guest`, { platform: 'desktop' }),
  )) as {
    account: { id: string; displayName: string };
    tokens: { accessToken: string };
  };
  const client = await RealtimeClient.connect(instance.url);
  await client.hello(session.tokens.accessToken, sim);
  return {
    accountId: session.account.id,
    displayName: session.account.displayName,
    accessToken: session.tokens.accessToken,
    client,
  };
}

/** A new registered (local) account with a socket; the instance must enable local auth. */
export async function registeredPlayer(instance: Instance, name: string): Promise<Player> {
  const response = await postJson(`${instance.url}/api/v1/auth/local/register`, {
    username: `${name.toLowerCase()}${randomBytes(3).toString('hex')}`,
    password: 'correct horse battery staple',
    displayName: `${name}${randomBytes(2).toString('hex')}`,
    platform: 'desktop',
  });
  const session = (await json(response)) as {
    account: { id: string; displayName: string };
    tokens: { accessToken: string };
  };
  expect(response.status).toBe(200);
  const client = await RealtimeClient.connect(instance.url);
  await client.hello(session.tokens.accessToken);
  return {
    accountId: session.account.id,
    displayName: session.account.displayName,
    accessToken: session.tokens.accessToken,
    client,
  };
}

export function relayRegistration(
  relayId: string,
  options: { region?: string; maxMatches?: number; draining?: boolean; matches?: number } = {},
) {
  return {
    relayId,
    publicUrl: `wss://${relayId}.relays.test/relay`,
    region: options.region ?? 'eu-west',
    build: 'glob2-relay test',
    turnProtocol: 1,
    capacity: { maxMatches: options.maxMatches ?? 100 },
    load: { matches: options.matches ?? 0, connections: 0 },
    draining: options.draining ?? false,
  };
}

export function relayCall(
  instance: Instance,
  method: 'GET' | 'POST' | 'PUT',
  path: string,
  bodyValue?: unknown,
  options: { key?: string; contentType?: string } = {},
) {
  const raw = bodyValue instanceof Uint8Array;
  return fetch(`${instance.url}/internal/v1${path}`, {
    method,
    headers: {
      authorization: `Bearer ${options.key ?? RELAY_KEY}`,
      ...(bodyValue === undefined
        ? {}
        : {
            'content-type':
              options.contentType ??
              (raw ? 'application/vnd.glob2.match-record' : 'application/json'),
          }),
    },
    ...(bodyValue === undefined
      ? {}
      : { body: raw ? (bodyValue as Uint8Array) : JSON.stringify(bodyValue) }),
  });
}

export async function registerRelay(
  instance: Instance,
  relayId: string,
  options: Parameters<typeof relayRegistration>[1] = {},
) {
  const response = await relayCall(
    instance,
    'POST',
    '/relays/register',
    relayRegistration(relayId, options),
  );
  expect(response.status).toBe(200);
  return (await response.json()) as { heartbeatIntervalSeconds: number; jwksUrl: string };
}

/** Verifies a match ticket against the instance's published JWKS, as a relay does. */
export async function verifyTicket(instance: Instance, ticket: string) {
  const jwks = (await json(await fetch(`${instance.url}/.well-known/jwks.json`))) as {
    keys: { kid: string; kty: string; crv: string; x: string }[];
  };
  const result = verifyJwt(ticket, {
    type: MATCH_TICKET_TYPE,
    audience: MATCH_TICKET_AUDIENCE,
    key: (kid) => {
      const jwk = jwks.keys.find((k) => k.kid === kid);
      return jwk
        ? createPublicKey({ key: { kty: jwk.kty, crv: jwk.crv, x: jwk.x }, format: 'jwk' })
        : undefined;
    },
  });
  expect(checkDocument('MatchTicketHeader', result.header).stage).toBe('ok');
  expect(checkDocument('MatchTicketClaims', result.claims).stage).toBe('ok');
  return result.claims as {
    matchId: string;
    seat: number;
    sub: string;
    accountId: string;
    humanSeats: number[];
    relayUrl: string;
    simVersion: SimVersion;
    entitlements: string[];
    iss: string;
    exp: number;
  };
}

export async function waitUntil(
  condition: () => Promise<boolean> | boolean,
  timeoutMs = 5000,
): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (!(await condition())) {
    if (Date.now() > deadline) throw new Error('timed out waiting for a condition');
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
}

/** Waits for a room.state event matching the predicate (older states are skipped). */
export async function roomState(
  client: RealtimeClient,
  match: (room: Record<string, unknown>) => boolean,
  timeoutMs = 5000,
): Promise<Record<string, unknown>> {
  const frame = await client.waitFor(
    (f) =>
      f.type === 'event' &&
      f.event === 'room.state' &&
      match((f.data as { room: Record<string, unknown> }).room),
    timeoutMs,
  );
  const room = (frame.data as { room: Record<string, unknown> }).room;
  expect(checkDocument('RoomState', room).stage).toBe('ok');
  return room;
}
