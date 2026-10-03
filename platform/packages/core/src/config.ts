// Service configuration: secrets and deployment settings from the environment
// (optionally a .env file), instance settings from instance.yaml. Every
// service loads the same shape and uses the parts it needs.
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { parse as parseYaml } from 'yaml';
import { databaseUrlFromEnv } from '@glob2/db';
import { schemaIssues } from '@glob2/protocol';
import { DEFAULT_INSTANCE_CONFIG, InstanceConfig } from './instanceConfig.ts';
import { resolveQueue } from './queueConfig.ts';

export type LogLevel = 'fatal' | 'error' | 'warn' | 'info' | 'debug' | 'trace' | 'silent';

export interface BlobStoreConfig {
  kind: 'fs' | 's3';
  /** fs: directory holding blobs. */
  directory: string;
  /** s3: bucket and optional endpoint (S3-compatible stores). */
  bucket?: string;
  endpoint?: string;
  region?: string;
}

/**
 * Where the platform's Ed25519 signing keys come from. A directory holds
 * `<kid>.pem` files (PKCS#8 private keys; a `<kid>.pub.pem` public key keeps a
 * retired key verifiable). JWT_PRIVATE_KEY (PEM) with JWT_KEY_ID adds one key
 * from the environment. JWT_ACTIVE_KID picks the signing key when several exist.
 */
export interface SigningKeysConfig {
  directory: string | undefined;
  inline: { kid: string; pem: string } | undefined;
  activeKid: string | undefined;
}

/**
 * A secret relays present as `Authorization: Bearer <key>` on /internal calls.
 * A key with a relay id may only act as that relay.
 */
export interface RelayKey {
  key: string;
  relayId?: string;
}

/**
 * RELAY_KEYS (comma-separated) or RELAY_KEYS_FILE (one per line, # comments):
 * entries are `<key>` (any relay) or `<relayId>:<key>` (that relay only).
 * Keys must be at least 32 characters.
 */
export function parseRelayKeys(text: string, where = 'RELAY_KEYS'): RelayKey[] {
  const keys: RelayKey[] = [];
  for (const raw of text.split(/[,\n]/)) {
    const entry = raw.replace(/#.*$/, '').trim();
    if (!entry) continue;
    const colon = entry.indexOf(':');
    const relayId = colon >= 0 ? entry.slice(0, colon) : undefined;
    const key = colon >= 0 ? entry.slice(colon + 1) : entry;
    if (relayId !== undefined && !/^[A-Za-z0-9._-]{1,64}$/.test(relayId)) {
      throw new ConfigError(`${where}: invalid relay id ${JSON.stringify(relayId)}`);
    }
    if (key.length < 32)
      throw new ConfigError(`${where}: relay keys must be at least 32 characters`);
    keys.push(relayId ? { key, relayId } : { key });
  }
  return keys;
}

/**
 * A secret engine agents present as `Authorization: Bearer <key>` on
 * /internal/v1/engine. A key with an agent id may only act as that agent.
 */
export interface EngineAgentKey {
  key: string;
  agentId?: string;
}

/** Reads `<key>` / `<id>:<key>` entries from an environment variable and a file. */
function serviceKeys(env: Env, cwd: string, variable: string, fileVariable: string): RelayKey[] {
  let keys: RelayKey[] = [];
  if (env[variable]) keys = parseRelayKeys(env[variable], variable);
  if (env[fileVariable]) {
    const path = resolve(cwd, env[fileVariable]);
    let text: string;
    try {
      text = readFileSync(path, 'utf8');
    } catch (error) {
      throw new ConfigError(`cannot read ${fileVariable} ${path}: ${(error as Error).message}`);
    }
    keys = [...keys, ...parseRelayKeys(text, fileVariable)];
  }
  return keys;
}

export interface PlatformConfig {
  /** Public origin clients use, e.g. https://play.example.org (no trailing slash). */
  publicOrigin: string;
  databaseUrl: string;
  logLevel: LogLevel;
  http: { host: string; port: number };
  blobs: BlobStoreConfig;
  /** Seconds to wait for in-flight work on SIGTERM before exiting anyway. */
  shutdownGraceSeconds: number;
  instance: InstanceConfig;
  instanceConfigPath: string | undefined;
  keys?: SigningKeysConfig;
  /** Keys relays authenticate with on /internal (none: relays are refused). */
  relayKeys?: RelayKey[];
  /** Keys engine agents authenticate with on /internal/v1/engine (none: agents are refused). */
  engineAgentKeys?: EngineAgentKey[];
  /** Largest uploaded map or save, in bytes (UPLOAD_MAX_BYTES, default 16 MiB). */
  uploadMaxBytes?: number;
  /** Largest match record a relay may upload, in bytes (RECORD_MAX_BYTES, default 64 MiB). */
  recordMaxBytes?: number;
  /** Environment that secrets named in instance.yaml (`...Env` settings) are read from. */
  secrets?: Record<string, string | undefined>;
}

/** Reads a secret named by an instance.yaml `...Env` setting. */
export function readSecret(config: PlatformConfig, name: string): string {
  const value = (config.secrets ?? process.env)[name];
  if (!value)
    throw new ConfigError(`environment variable ${name} (named in instance.yaml) is not set`);
  return value;
}

export class ConfigError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ConfigError';
  }
}

type Env = Record<string, string | undefined>;

function required(env: Env, name: string): string {
  const value = env[name];
  if (!value) throw new ConfigError(`${name} is required`);
  return value;
}

function integer(env: Env, name: string, fallback: number, min = 0, max = 65535): number {
  const raw = env[name];
  if (raw === undefined || raw === '') return fallback;
  const value = Number(raw);
  if (!Number.isInteger(value) || value < min || value > max) {
    throw new ConfigError(`${name} must be an integer between ${min} and ${max}`);
  }
  return value;
}

const LOG_LEVELS: readonly LogLevel[] = [
  'fatal',
  'error',
  'warn',
  'info',
  'debug',
  'trace',
  'silent',
];

export function loadInstanceConfig(path: string): InstanceConfig {
  let document: unknown;
  try {
    document = parseYaml(readFileSync(path, 'utf8'));
  } catch (error) {
    throw new ConfigError(`cannot read ${path}: ${(error as Error).message}`);
  }
  const issues = schemaIssues(InstanceConfig, document);
  if (issues.length > 0) {
    throw new ConfigError(
      `${path} is invalid: ${issues.map((i) => `${i.path} ${i.message}`).join('; ')}`,
    );
  }
  const config = document as InstanceConfig;
  const ids = new Set<string>();
  for (const queue of config.queues) {
    if (ids.has(queue.id)) throw new ConfigError(`${path}: duplicate queue id ${queue.id}`);
    ids.add(queue.id);
    try {
      resolveQueue(queue);
    } catch (error) {
      throw new ConfigError(`${path}: ${(error as Error).message}`);
    }
  }
  return config;
}

export interface LoadConfigOptions {
  /** Environment to read; defaults to process.env after loading `envFile`. */
  env?: Env;
  /** .env file loaded into process.env when present (default ./.env). Ignored with `env`. */
  envFile?: string;
  cwd?: string;
  /** False for services without database access (the engine agent). Default true. */
  database?: boolean;
}

export function loadConfig(options: LoadConfigOptions = {}): PlatformConfig {
  const cwd = options.cwd ?? process.cwd();
  let env = options.env;
  if (!env) {
    const envFile = resolve(cwd, options.envFile ?? '.env');
    // Variables already set in the environment win over the file.
    if (existsSync(envFile)) process.loadEnvFile(envFile);
    env = process.env;
  }

  const publicOrigin = (env['PUBLIC_ORIGIN'] ?? 'http://localhost:8080').replace(/\/+$/, '');
  if (!/^https?:\/\/[^/\s]+$/.test(publicOrigin)) {
    throw new ConfigError('PUBLIC_ORIGIN must be an origin like https://play.example.org');
  }
  const logLevel = (env['LOG_LEVEL'] ?? 'info') as LogLevel;
  if (!LOG_LEVELS.includes(logLevel)) {
    throw new ConfigError(`LOG_LEVEL must be one of ${LOG_LEVELS.join(', ')}`);
  }
  const blobKind = env['BLOB_STORE'] ?? 'fs';
  if (blobKind !== 'fs' && blobKind !== 's3') throw new ConfigError('BLOB_STORE must be fs or s3');

  const explicitInstance = env['INSTANCE_CONFIG'];
  const instancePath = resolve(cwd, explicitInstance ?? 'instance.yaml');
  let instance = DEFAULT_INSTANCE_CONFIG;
  let instanceConfigPath: string | undefined;
  if (existsSync(instancePath)) {
    instance = loadInstanceConfig(instancePath);
    instanceConfigPath = instancePath;
  } else if (explicitInstance) {
    throw new ConfigError(`INSTANCE_CONFIG ${instancePath} does not exist`);
  }

  const relayKeys = serviceKeys(env, cwd, 'RELAY_KEYS', 'RELAY_KEYS_FILE');
  const engineAgentKeys = serviceKeys(env, cwd, 'ENGINE_AGENT_KEYS', 'ENGINE_AGENT_KEYS_FILE').map(
    ({ key, relayId }) => (relayId ? { key, agentId: relayId } : { key }),
  );

  let databaseUrl = '';
  if (options.database !== false) {
    try {
      databaseUrl = databaseUrlFromEnv(env) ?? '';
    } catch (error) {
      throw new ConfigError(`DATABASE_PASSWORD_FILE: ${(error as Error).message}`);
    }
    if (!databaseUrl) throw new ConfigError('DATABASE_URL is required');
  }

  return {
    publicOrigin,
    databaseUrl,
    logLevel,
    http: { host: env['HTTP_HOST'] ?? '0.0.0.0', port: integer(env, 'HTTP_PORT', 8080, 1) },
    blobs: {
      kind: blobKind,
      directory: resolve(cwd, env['BLOB_DIR'] ?? 'data/blobs'),
      bucket: env['S3_BUCKET'],
      endpoint: env['S3_ENDPOINT'],
      region: env['S3_REGION'],
    },
    shutdownGraceSeconds: integer(env, 'SHUTDOWN_GRACE_SECONDS', 25, 0, 3600),
    instance,
    instanceConfigPath,
    keys: {
      directory: env['JWT_KEYS_DIR'] ? resolve(cwd, env['JWT_KEYS_DIR']) : undefined,
      inline: env['JWT_PRIVATE_KEY']
        ? { kid: required(env, 'JWT_KEY_ID'), pem: env['JWT_PRIVATE_KEY'].replace(/\\n/g, '\n') }
        : undefined,
      activeKid: env['JWT_ACTIVE_KID'] || undefined,
    },
    relayKeys,
    engineAgentKeys,
    uploadMaxBytes: integer(env, 'UPLOAD_MAX_BYTES', 16 * 1024 * 1024, 1024, 1024 * 1024 * 1024),
    recordMaxBytes: integer(env, 'RECORD_MAX_BYTES', 64 * 1024 * 1024, 1024, 1024 * 1024 * 1024),
    secrets: env,
  };
}
