// Test harness: API instances on a per-file test database, a realtime client
// and a cookie-keeping "browser".
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import type { AddressInfo } from 'node:net';
import type { FastifyInstance } from 'fastify';
import WebSocket from 'ws';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { PgPubSub } from '@glob2/db';
import {
  DEFAULT_INSTANCE_CONFIG,
  FsBlobStore,
  JobQueue,
  allowAllPolicy,
  createLogger,
  prepareJobQueue,
  type AccessPolicy,
  type InstanceConfig,
  type EngineAgentKey,
  type PlatformConfig,
  type RelayKey,
} from '@glob2/core';
import { buildApp, type BuildOptions } from '../src/app.ts';
import { SigningKeys } from '../src/auth/keys.ts';

export const logger = createLogger(
  'api-test',
  (process.env['TEST_LOG_LEVEL'] as 'silent') ?? 'silent',
);
export const SIM = { versionMinor: 125, netProtocol: 49, dataHash: 'ab'.repeat(32) };

export interface Instance {
  app: FastifyInstance;
  /** http://127.0.0.1:<port> */
  url: string;
  pubsub: PgPubSub;
  close(): Promise<void>;
}

export interface Harness {
  database: TestDatabase;
  keys: SigningKeys;
  jobs: JobQueue;
  /** Blob store shared by every replica of the harness (a temporary directory). */
  blobs: FsBlobStore;
  /** Starts an API replica listening on a free port. `origin` defaults to the replica's own URL. */
  start(options?: {
    instance?: Partial<InstanceConfig>;
    origin?: string;
    secrets?: Record<string, string>;
    build?: BuildOptions;
    access?: AccessPolicy;
    relayKeys?: RelayKey[];
    engineAgentKeys?: EngineAgentKey[];
  }): Promise<Instance>;
  close(): Promise<void>;
}

export async function createHarness(): Promise<Harness> {
  const database = await createTestDatabase();
  await prepareJobQueue(database.pool, logger);
  const jobs = await JobQueue.create(database.pool, logger);
  const keys = SigningKeys.ephemeral('test-1');
  const blobDir = mkdtempSync(join(tmpdir(), 'glob2-blobs-'));
  const blobs = new FsBlobStore(blobDir);
  const instances: Instance[] = [];
  return {
    database,
    keys,
    jobs,
    blobs,
    async start(options = {}) {
      const pubsub = new PgPubSub({ connectionString: database.url });
      // Replicas of one instance share an origin; tests that need a fixed one pass it.
      let resolvedOrigin = options.origin;
      const config: PlatformConfig = {
        publicOrigin: 'http://placeholder.invalid',
        databaseUrl: database.url,
        logLevel: 'silent',
        http: { host: '127.0.0.1', port: 0 },
        blobs: { kind: 'fs', directory: blobDir },
        shutdownGraceSeconds: 1,
        instance: { ...DEFAULT_INSTANCE_CONFIG, name: 'Test Instance', ...options.instance },
        instanceConfigPath: undefined,
        secrets: options.secrets ?? {},
        relayKeys: options.relayKeys ?? [],
        engineAgentKeys: options.engineAgentKeys ?? [],
      };
      // The origin must be known before building (cookies, redirect URIs), but
      // the port only after listening: reserve one first.
      if (!resolvedOrigin) {
        const probe = (await import('node:net')).createServer();
        await new Promise<void>((resolve) => probe.listen(0, '127.0.0.1', resolve));
        const port = (probe.address() as AddressInfo).port;
        await new Promise<void>((resolve) => probe.close(() => resolve()));
        resolvedOrigin = `http://127.0.0.1:${port}`;
      }
      config.publicOrigin = resolvedOrigin;
      const app = await buildApp(
        {
          config,
          logger,
          db: database.db,
          pubsub,
          jobs,
          blobs,
          access: options.access ?? allowAllPolicy,
          keys,
        },
        { roomSweepMs: 0, ...options.build },
      );
      const listenPort = options.origin ? 0 : Number(new URL(resolvedOrigin).port);
      await app.listen({ host: '127.0.0.1', port: listenPort });
      const port = (app.server.address() as AddressInfo).port;
      const instance: Instance = {
        app,
        url: `http://127.0.0.1:${port}`,
        pubsub,
        async close() {
          await app.close();
          await pubsub.close();
        },
      };
      instances.push(instance);
      return instance;
    },
    async close() {
      for (const instance of instances) await instance.close().catch(() => undefined);
      await jobs.close();
      await database.drop();
      rmSync(blobDir, { recursive: true, force: true });
    },
  };
}

// ----------------------------------------------------------- realtime client

export interface ServerFrame {
  type: 'response' | 'event';
  id?: string;
  ok?: boolean;
  result?: Record<string, unknown>;
  error?: { code: string; message: string; details?: unknown };
  event?: string;
  data?: Record<string, unknown>;
}

export class RealtimeClient {
  readonly socket: WebSocket;
  readonly frames: ServerFrame[] = [];
  private nextId = 1;
  private waiters: (() => void)[] = [];
  closed: { code: number; reason: string } | undefined;

  private constructor(socket: WebSocket) {
    this.socket = socket;
    socket.on('message', (data) => {
      this.frames.push(JSON.parse(data.toString()) as ServerFrame);
      this.wake();
    });
    socket.on('close', (code, reason) => {
      this.closed = { code, reason: reason.toString() };
      this.wake();
    });
  }

  private wake() {
    const waiters = this.waiters;
    this.waiters = [];
    for (const waiter of waiters) waiter();
  }

  static connect(url: string, headers: Record<string, string> = {}): Promise<RealtimeClient> {
    return new Promise((resolve, reject) => {
      const socket = new WebSocket(`${url.replace(/^http/, 'ws')}/realtime`, { headers });
      socket.once('open', () => resolve(new RealtimeClient(socket)));
      socket.once('unexpected-response', (_request, response) =>
        reject(new Error(`upgrade refused: ${response.statusCode}`)),
      );
      socket.once('error', reject);
    });
  }

  /** Waits for the first frame matching `match` (already received or future). */
  async waitFor(match: (frame: ServerFrame) => boolean, timeoutMs = 5000): Promise<ServerFrame> {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const index = this.frames.findIndex(match);
      if (index >= 0) return this.frames.splice(index, 1)[0]!;
      if (this.closed) throw new Error(`socket closed (${this.closed.code} ${this.closed.reason})`);
      const left = deadline - Date.now();
      if (left <= 0) throw new Error('timed out waiting for a frame');
      await new Promise<void>((resolve) => {
        const timer = setTimeout(resolve, left);
        this.waiters.push(() => {
          clearTimeout(timer);
          resolve();
        });
      });
    }
  }

  event(name: string, timeoutMs?: number) {
    return this.waitFor((f) => f.type === 'event' && f.event === name, timeoutMs).then(
      (f) => f.data!,
    );
  }

  async waitClosed(timeoutMs = 5000) {
    const deadline = Date.now() + timeoutMs;
    while (!this.closed && Date.now() < deadline) await new Promise((r) => setTimeout(r, 20));
    return this.closed;
  }

  sendRaw(text: string) {
    this.socket.send(text);
  }

  /** Sends a request and resolves with its response frame. */
  async call(method: string, params: object = {}): Promise<ServerFrame> {
    const id = String(this.nextId++);
    this.socket.send(JSON.stringify({ type: 'request', id, method, params }));
    return this.waitFor((f) => f.type === 'response' && f.id === id);
  }

  /** Like call, but returns the result or throws the error. */
  async ok(method: string, params: object = {}): Promise<Record<string, unknown>> {
    const response = await this.call(method, params);
    if (!response.ok) throw new Error(`${method} failed: ${JSON.stringify(response.error)}`);
    return response.result!;
  }

  hello(accessToken?: string, simVersion: object = SIM) {
    return this.ok('session.hello', {
      protocol: 1,
      client: { platform: 'desktop', version: 'test', simVersion },
      ...(accessToken ? { accessToken } : {}),
    });
  }

  /** Drops frames received so far (events from earlier steps). */
  clear() {
    this.frames.length = 0;
  }

  close() {
    this.socket.close();
  }
}

// ------------------------------------------------------------------ browser

/**
 * A minimal browser: keeps cookies per host and follows nothing automatically.
 * `routes` maps a public origin to the replica that should serve it, so tests
 * can choose which API replica the "user" reaches.
 */
export class Browser {
  private readonly cookies = new Map<string, Map<string, string>>();
  routes: Record<string, string>;

  constructor(routes: Record<string, string> = {}) {
    this.routes = routes;
  }

  cookie(host: string, name: string): string | undefined {
    return this.cookies.get(host)?.get(name);
  }

  async request(url: string, init: RequestInit = {}): Promise<Response> {
    const target = new URL(url);
    const jar = this.cookies.get(target.host);
    const headers = new Headers(init.headers);
    if (jar && jar.size > 0) {
      headers.set('cookie', [...jar].map(([k, v]) => `${k}=${v}`).join('; '));
    }
    const route = this.routes[target.origin];
    const physical = route ? `${route}${target.pathname}${target.search}` : url;
    const response = await fetch(physical, { ...init, headers, redirect: 'manual' });
    for (const line of response.headers.getSetCookie()) {
      const [pair] = line.split(';');
      const eq = pair!.indexOf('=');
      const name = pair!.slice(0, eq).trim();
      const value = pair!.slice(eq + 1).trim();
      let hostJar = this.cookies.get(target.host);
      if (!hostJar) this.cookies.set(target.host, (hostJar = new Map()));
      if (value === '' || /max-age=0|expires=thu, 01 jan 1970/i.test(line)) hostJar.delete(name);
      else hostJar.set(name, value);
    }
    return response;
  }

  get(url: string) {
    return this.request(url);
  }

  /** Submits a form the way a browser does, with the page's origin. */
  post(url: string, form: Record<string, string>, origin = new URL(url).origin) {
    return this.request(url, {
      method: 'POST',
      headers: { 'content-type': 'application/x-www-form-urlencoded', origin },
      body: new URLSearchParams(form).toString(),
    });
  }

  /** Follows redirects (manually, keeping cookies) until a non-redirect response. */
  async follow(response: Response, base: string): Promise<{ response: Response; url: string }> {
    let url = base;
    let current = response;
    for (let hops = 0; hops < 10 && current.status >= 300 && current.status < 400; hops++) {
      url = new URL(current.headers.get('location')!, url).href;
      current = await this.get(url);
    }
    return { response: current, url };
  }
}

export async function json(response: Response): Promise<Record<string, unknown>> {
  return (await response.json()) as Record<string, unknown>;
}

export function postJson(url: string, body: unknown, headers: Record<string, string> = {}) {
  return fetch(url, {
    method: 'POST',
    headers: { 'content-type': 'application/json', ...headers },
    body: JSON.stringify(body),
  });
}
