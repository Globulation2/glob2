import { readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createPublicKey } from 'node:crypto';
import { Ajv2020 } from 'ajv/dist/2020.js';
import { describe, expect, it } from 'vitest';
import {
  MATCH_TICKET_AUDIENCE,
  MATCH_TICKET_TYPE,
  checkDocument,
  schemaRegistry,
} from '../src/index.ts';
import { JwtError, verifyJwt } from '../src/node/index.ts';
import { buildFixtureFiles } from '../scripts/fixtureFiles.ts';

const fixturesRoot = join(dirname(fileURLToPath(import.meta.url)), '..', 'fixtures');

function listFiles(dir: string): string[] {
  return readdirSync(dir).flatMap((entry) => {
    const path = join(dir, entry);
    return statSync(path).isDirectory() ? listFiles(path) : [relative(fixturesRoot, path)];
  });
}

function read(path: string): string {
  return readFileSync(join(fixturesRoot, path), 'utf8');
}

interface Manifest {
  schemas: Record<string, string>;
  fixtures: { file: string; schema: string; valid: boolean; stage?: string }[];
  tickets: {
    jwks: string;
    verifyAt: number;
    leewaySeconds: number;
    tickets: { file: string; valid: boolean; reason?: string }[];
  };
}

const manifest = JSON.parse(read('manifest.json')) as Manifest;

describe('protocol fixtures', () => {
  it('are current (run `npm run fixtures` after changing schemas or cases)', () => {
    const expected = buildFixtureFiles();
    const actual = listFiles(fixturesRoot).sort();
    expect(actual).toEqual([...expected.keys()].sort());
    for (const [path, content] of expected) {
      expect(read(path), path).toBe(content);
    }
  });

  it('export a JSON Schema for every registered schema', () => {
    expect(Object.keys(manifest.schemas).sort()).toEqual(Object.keys(schemaRegistry).sort());
  });

  it('validate as declared, using the TypeScript validators', () => {
    expect(manifest.fixtures.length).toBeGreaterThan(40);
    for (const fixture of manifest.fixtures) {
      const check = checkDocument(fixture.schema, JSON.parse(read(fixture.file)));
      expect(check.stage, fixture.file).toBe(fixture.valid ? 'ok' : fixture.stage);
    }
  });

  it('validate as declared with an independent JSON Schema 2020-12 validator', () => {
    const ajv = new Ajv2020({ strict: false, allErrors: true });
    for (const fixture of manifest.fixtures) {
      const schema = JSON.parse(read(manifest.schemas[fixture.schema]!)) as object;
      const validate = ajv.compile({ ...schema, $id: undefined });
      const schemaValid = validate(JSON.parse(read(fixture.file)));
      // Semantic failures pass the JSON Schema by definition.
      expect(schemaValid, fixture.file).toBe(fixture.valid || fixture.stage === 'semantic');
    }
  });

  it('include tickets that verify exactly as the manifest says', () => {
    const jwks = JSON.parse(read(manifest.tickets.jwks)) as { keys: ({ kid: string } & object)[] };
    const keys = new Map(
      jwks.keys.map((jwk) => [jwk.kid, createPublicKey({ key: jwk as never, format: 'jwk' })]),
    );
    for (const ticket of manifest.tickets.tickets) {
      const verify = () =>
        verifyJwt(read(ticket.file).trim(), {
          type: MATCH_TICKET_TYPE,
          audience: MATCH_TICKET_AUDIENCE,
          now: manifest.tickets.verifyAt,
          leewaySeconds: manifest.tickets.leewaySeconds,
          key: (kid) => keys.get(kid),
        });
      if (ticket.valid) {
        const { claims } = verify();
        expect(checkDocument('MatchTicketClaims', claims).stage).toBe('ok');
      } else {
        expect(verify, ticket.file).toThrow(JwtError);
        try {
          verify();
        } catch (error) {
          expect((error as JwtError).reason, ticket.file).toBe(ticket.reason);
        }
      }
    }
  });
});
