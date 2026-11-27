// Symbol uploads from the release workflows (docs/multiplayer/crash-reports.md#symbols).
//
// POST /api/v1/crash-symbols?kind=breakpad&version=0.11.8.1 takes one gzipped
// Breakpad .sym written by dump_syms and stores it under the module and debug
// ID of its MODULE line. kind=wasm with &buildId=<32 hex> takes a gzipped
// Emscripten symbol map (index:name lines) for that WebAssembly build.
//
// Only the release workflows hold CRASH_SYMBOL_UPLOAD_TOKEN (a GitHub Actions
// secret); without it configured the route answers 503. The first upload for
// a version registers it in crash_releases, which is what lets crash reports
// from that version in.
import { createHash, randomUUID, timingSafeEqual } from 'node:crypto';
import { createGunzip } from 'node:zlib';
import { Readable } from 'node:stream';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import type { CrashSymbolKind, CrashSymbolReceipt } from '@glob2/protocol';
import { apiError } from '../errors.ts';

/** Largest gzipped symbol file accepted. */
export const CRASH_SYMBOL_MAX_BYTES = 160 * 1024 * 1024;
/** Largest symbol file once decompressed. */
export const CRASH_SYMBOL_MAX_INFLATED = 2 * 1024 * 1024 * 1024;
export const CRASH_SYMBOL_TOKEN_ENV = 'CRASH_SYMBOL_UPLOAD_TOKEN';

const RELEASE = /^[0-9]+(\.[0-9]+){2,3}$/;
const WASM_BUILD_ID = /^[0-9a-f]{32}$/;
// MODULE <os> <arch> <debug id> <debug file>
const MODULE_LINE = /^MODULE (\S+) (\S+) ([0-9A-F]{32,40}) ([^/\\\0]{1,128})$/;
const SYMBOL_MAP_LINE = /^[0-9]+:\S/;

export interface ParsedSymbols {
  module: string;
  debugId: string;
  os: string;
  arch: string;
}

/**
 * Decompresses `gzipped` to check it is whole (gzip's CRC) and reads its first
 * line. Throws on corrupt or oversized input.
 */
export async function firstLine(gzipped: Buffer, maxInflated = CRASH_SYMBOL_MAX_INFLATED) {
  let line: string | undefined;
  let head = '';
  let total = 0;
  const gunzip = Readable.from([gzipped]).pipe(createGunzip());
  for await (const chunk of gunzip) {
    const bytes = chunk as Buffer;
    total += bytes.length;
    if (total > maxInflated) throw new SymbolError('symbol file too large');
    if (line === undefined) {
      head += bytes.subarray(0, 4096).toString('latin1');
      const end = head.indexOf('\n');
      if (end >= 0) line = head.slice(0, end).replace(/\r$/, '');
      else if (head.length > 4096) throw new SymbolError('first line too long');
    }
  }
  if (line === undefined) line = head.replace(/\r$/, '');
  return line;
}

export class SymbolError extends Error {}

/** The module a Breakpad symbol file describes, from its MODULE line. */
export function parseModuleLine(line: string): ParsedSymbols {
  const match = MODULE_LINE.exec(line);
  if (!match?.[1] || !match[2] || !match[3] || !match[4] || /^\.+$/.test(match[4]))
    throw new SymbolError('not a Breakpad symbol file');
  return { os: match[1], arch: match[2], debugId: match[3], module: match[4] };
}

/** Blob store key for a symbol file: lowercase, path-safe, unique per upload. */
export function symbolKey(kind: CrashSymbolKind, module: string, debugId: string): string {
  const name = module
    .toLowerCase()
    .replace(/[^a-z0-9._-]/g, '_')
    .replace(/^[._-]+/, '');
  return `crash-symbols/${kind}/${name || 'module'}/${debugId.toLowerCase()}-${randomUUID()}.sym.gz`;
}

function digest(value: string): Buffer {
  return createHash('sha256').update(value).digest();
}

/** Constant-time check of the bearer token against the configured one. */
export function authorized(header: string | undefined, token: string): boolean {
  const match = /^Bearer (\S+)$/.exec(header ?? '');
  return !!match?.[1] && timingSafeEqual(digest(match[1]), digest(token));
}

export async function crashSymbolRoutes(app: FastifyInstance) {
  const { db, blobs, config, logger } = app.services;
  const token = (config.secrets ?? process.env)[CRASH_SYMBOL_TOKEN_ENV];

  await app.register(async (scope) => {
    scope.addContentTypeParser(
      'application/gzip',
      { parseAs: 'buffer', bodyLimit: CRASH_SYMBOL_MAX_BYTES },
      (_request, raw, done) => done(null, raw),
    );
    scope.post(
      '/api/v1/crash-symbols',
      { bodyLimit: CRASH_SYMBOL_MAX_BYTES },
      async (
        request: FastifyRequest<{
          Querystring: { kind?: string; version?: string; buildId?: string };
        }>,
        reply,
      ): Promise<CrashSymbolReceipt> => {
        if (!token) throw apiError('unavailable', 'Symbol uploads are not configured.');
        if (!authorized(request.headers.authorization, token))
          throw apiError('unauthenticated', 'A release workflow token is required.');
        const { kind, version, buildId } = request.query;
        if (kind !== 'breakpad' && kind !== 'wasm')
          throw apiError('bad_request', 'Unknown symbol kind.');
        if (!version || !RELEASE.test(version))
          throw apiError('bad_request', 'Symbols are accepted only for release versions.');
        if (!Buffer.isBuffer(request.body))
          throw apiError('bad_request', 'Send the symbol file as application/gzip.');
        const file = request.body;
        let symbols: ParsedSymbols;
        try {
          const line = await firstLine(file);
          if (kind === 'breakpad') symbols = parseModuleLine(line);
          else {
            if (!buildId || !WASM_BUILD_ID.test(buildId) || !SYMBOL_MAP_LINE.test(line))
              throw new SymbolError('not an Emscripten symbol map');
            symbols = { module: 'glob2.wasm', debugId: buildId, os: 'web', arch: 'wasm32' };
          }
        } catch (error) {
          if (
            error instanceof SymbolError ||
            (error as NodeJS.ErrnoException).code?.startsWith('Z_')
          )
            throw apiError('bad_request', 'Invalid symbol file.');
          throw error;
        }
        const key = symbolKey(kind, symbols.module, symbols.debugId);
        await blobs.put(key, file);
        let previous: string | undefined;
        try {
          previous = await db.transaction().execute(async (trx) => {
            await trx
              .insertInto('crash_releases')
              .values({ version })
              .onConflict((oc) => oc.doNothing())
              .execute();
            const old = await trx
              .selectFrom('crash_symbols')
              .select('blob_key')
              .where('kind', '=', kind)
              .where('module', '=', symbols.module)
              .where('debug_id', '=', symbols.debugId)
              .forUpdate()
              .executeTakeFirst();
            await trx
              .insertInto('crash_symbols')
              .values({
                kind,
                module: symbols.module,
                debug_id: symbols.debugId,
                version,
                os: symbols.os,
                arch: symbols.arch,
                size: file.length,
                blob_key: key,
              })
              .onConflict((oc) =>
                oc.columns(['kind', 'module', 'debug_id']).doUpdateSet({
                  version,
                  os: symbols.os,
                  arch: symbols.arch,
                  size: file.length,
                  blob_key: key,
                  uploaded_at: new Date(),
                }),
              )
              .execute();
            return old?.blob_key;
          });
        } catch (error) {
          await blobs.delete(key).catch(() => undefined);
          throw error;
        }
        if (previous)
          await blobs.delete(previous).catch((err: unknown) => {
            // The retention sweep removes it later.
            logger.warn({ err, key: previous }, 'Could not delete replaced symbol file');
          });
        void reply.code(previous ? 200 : 201);
        return {
          kind,
          module: symbols.module,
          debugId: symbols.debugId,
          version,
          replaced: previous !== undefined,
        };
      },
    );
  });
}
