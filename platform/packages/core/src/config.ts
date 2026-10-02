// Service configuration: secrets and deployment settings from the environment
// (optionally a .env file), instance settings from instance.yaml. Every
// service loads the same shape and uses the parts it needs.
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { parse as parseYaml } from 'yaml';
import { schemaIssues } from '@glob2/protocol';
import { DEFAULT_INSTANCE_CONFIG, InstanceConfig } from './instanceConfig.ts';

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
  /** Environment that secrets named in instance.yaml (`...Env` settings) are read from. */
  secrets?: Record<string, string | undefined>;
}

/** Reads a secret named by an instance.yaml `...Env` setting. */
export function readSecret(config: PlatformConfig, name: string): string {
  const value = (config.secrets ?? process.env)[name];
  if (!value) throw new ConfigError(`environment variable ${name} (named in instance.yaml) is not set`);
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
  }
  return config;
}

export interface LoadConfigOptions {
  /** Environment to read; defaults to process.env after loading `envFile`. */
  env?: Env;
  /** .env file loaded into process.env when present (default ./.env). Ignored with `env`. */
  envFile?: string;
  cwd?: string;
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

  return {
    publicOrigin,
    databaseUrl: required(env, 'DATABASE_URL'),
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
    secrets: env,
  };
}
