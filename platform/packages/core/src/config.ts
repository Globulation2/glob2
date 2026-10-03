// Service configuration: secrets and deployment settings from the environment
// (optionally a .env file), instance settings from instance.yaml. Every
// service loads the same shape and uses the parts it needs.
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { parse as parseYaml } from 'yaml';
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
  };
}
