// Builds the contents of packages/protocol/fixtures/ (JSON Schemas, fixture
// documents, signed tickets and the manifest) in memory. scripts/generate.ts
// writes them; test/fixtures.test.ts checks the committed files are current.
import { createHash, createPrivateKey, type KeyObject } from 'node:crypto';
import {
  ACCESS_TOKEN_TYPE,
  MATCH_TICKET_TYPE,
  PLATFORM_JWT_ALGORITHM,
  CONNECTION_METRICS,
  CONNECTION_QUALITY_CASES,
  CONNECTION_RATING_LABELS,
  checkDocument,
  schemaRegistry,
} from '../src/index.ts';
import { publicJwk, signJwt, type JwtHeader } from '../src/node/index.ts';
import { TICKET_CLAIMS, fixtureCases } from './fixtureCases.ts';

const JSON_SCHEMA_DIALECT = 'https://json-schema.org/draft/2020-12/schema';

function json(value: unknown): string {
  return `${JSON.stringify(value, null, 2)}\n`;
}

/** Deterministic Ed25519 key for fixtures only. Never use it outside tests. */
export function fixtureSigningKey(): KeyObject {
  const seed = createHash('sha256').update('glob2 protocol fixture key, TEST ONLY').digest();
  const pkcs8Prefix = Buffer.from('302e020100300506032b657004220420', 'hex');
  return createPrivateKey({
    key: Buffer.concat([pkcs8Prefix, seed]),
    format: 'der',
    type: 'pkcs8',
  });
}

export const FIXTURE_KID = 'fixture-key-1';
/** Seconds since the epoch at which ticket fixtures are to be verified. */
export const TICKET_VERIFY_AT = TICKET_CLAIMS.iat + 60;

function ticketFiles(): { files: Map<string, string>; manifest: unknown } {
  const key = fixtureSigningKey();
  const header: JwtHeader = {
    alg: PLATFORM_JWT_ALGORITHM,
    typ: MATCH_TICKET_TYPE,
    kid: FIXTURE_KID,
  };
  const valid = signJwt(header, TICKET_CLAIMS, key);
  const [h, c, s] = valid.split('.') as [string, string, string];
  const flipped = `${s.slice(0, -2)}${s.endsWith('AA') ? 'AB' : 'AA'}`;
  const otherClaims = Buffer.from(JSON.stringify({ ...TICKET_CLAIMS, seat: 1 })).toString(
    'base64url',
  );
  const cases: { name: string; token: string; valid: boolean; reason?: string; note: string }[] = [
    { name: 'valid', token: valid, valid: true, note: 'Correctly signed ticket.' },
    {
      name: 'tampered-signature',
      token: `${h}.${c}.${flipped}`,
      valid: false,
      reason: 'signature',
      note: 'Signature bytes changed.',
    },
    {
      name: 'tampered-claims',
      token: `${h}.${otherClaims}.${s}`,
      valid: false,
      reason: 'signature',
      note: 'Claims changed (seat 1) without re-signing.',
    },
    {
      name: 'alg-none',
      token: `${Buffer.from(JSON.stringify({ ...header, alg: 'none' })).toString('base64url')}.${c}.`,
      valid: false,
      reason: 'algorithm',
      note: 'Unsigned token must be refused.',
    },
    {
      name: 'wrong-type',
      token: signJwt({ ...header, typ: ACCESS_TOKEN_TYPE }, TICKET_CLAIMS, key),
      valid: false,
      reason: 'type',
      note: `An access token (typ ${ACCESS_TOKEN_TYPE}) is not a match ticket.`,
    },
    {
      name: 'unknown-kid',
      token: signJwt({ ...header, kid: 'retired-key' }, TICKET_CLAIMS, key),
      valid: false,
      reason: 'key',
      note: 'kid not in the JWKS.',
    },
    {
      name: 'expired',
      token: signJwt(header, { ...TICKET_CLAIMS, exp: TICKET_VERIFY_AT - 3600 }, key),
      valid: false,
      reason: 'expired',
      note: 'exp is an hour before the verification time.',
    },
    {
      name: 'wrong-audience',
      token: signJwt(header, { ...TICKET_CLAIMS, aud: 'glob2-api' }, key),
      valid: false,
      reason: 'audience',
      note: 'aud must be glob2-relay.',
    },
  ];
  const files = new Map<string, string>();
  files.set('tickets/jwks.json', json({ keys: [publicJwk(key, FIXTURE_KID)] }));
  for (const ticket of cases) files.set(`tickets/${ticket.name}.jwt`, `${ticket.token}\n`);
  return {
    files,
    manifest: {
      jwks: 'tickets/jwks.json',
      verifyAt: TICKET_VERIFY_AT,
      leewaySeconds: 30,
      expectedClaims: 'valid/MatchTicketClaims/player-ticket.json',
      tickets: cases.map((t) => ({
        file: `tickets/${t.name}.jwt`,
        valid: t.valid,
        ...(t.reason ? { reason: t.reason } : {}),
        note: t.note,
      })),
    },
  };
}

/** Relative path → file content for everything under fixtures/. */
export function buildFixtureFiles(): Map<string, string> {
  const files = new Map<string, string>();
  const schemaNames = Object.keys(schemaRegistry).sort();
  for (const name of schemaNames) {
    const { schema } = schemaRegistry[name]!;
    files.set(
      `schemas/${name}.schema.json`,
      json({ $schema: JSON_SCHEMA_DIALECT, $id: `glob2-protocol:${name}`, title: name, ...schema }),
    );
  }

  const seen = new Set<string>();
  const fixtures = fixtureCases.map((fixture) => {
    const file = `${fixture.valid ? 'valid' : 'invalid'}/${fixture.schema}/${fixture.name}.json`;
    if (seen.has(file)) throw new Error(`duplicate fixture ${file}`);
    seen.add(file);
    const check = checkDocument(fixture.schema, fixture.value);
    if (fixture.valid && check.stage !== 'ok') {
      throw new Error(`${file} should be valid: ${JSON.stringify(check.issues)}`);
    }
    if (!fixture.valid && check.stage !== fixture.stage) {
      throw new Error(`${file} should fail at ${fixture.stage} but got ${check.stage}`);
    }
    files.set(file, json(fixture.value));
    return {
      file,
      schema: fixture.schema,
      valid: fixture.valid,
      ...(fixture.stage ? { stage: fixture.stage } : {}),
      note: fixture.note,
    };
  });

  const tickets = ticketFiles();
  for (const [path, content] of tickets.files) files.set(path, content);

  // The connection-quality table, for the C++ copy in src/gui/ConnectionQuality.h
  // (test/ConnectionQualityTest.cpp compares the two).
  files.set(
    'connection-quality.json',
    json({
      description:
        'Connection-quality thresholds shared by the game and the platform (src/connectionQuality.ts). Generated; do not edit by hand.',
      formatVersion: 1,
      metrics: CONNECTION_METRICS,
      ratings: CONNECTION_RATING_LABELS,
      cases: CONNECTION_QUALITY_CASES,
    }),
  );

  files.set(
    'manifest.json',
    json({
      description:
        'Contract fixtures generated from packages/protocol (npm run fixtures). Do not edit by hand.',
      formatVersion: 1,
      schemas: Object.fromEntries(schemaNames.map((n) => [n, `schemas/${n}.schema.json`])),
      fixtures,
      tickets: tickets.manifest,
    }),
  );
  return files;
}
