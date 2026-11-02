import { describe, expect, it } from 'vitest';
import {
  MatchSetup,
  STANDARD_RULES,
  RealtimeServerMessage,
  engineTaskIdentifier,
  isValid,
  matchSetupProblems,
  parse,
  parseSimVersionKey,
  ProtocolValidationError,
  realtimeMethods,
  realtimeSchemaName,
  sameSimVersion,
  schemaRegistry,
  simVersionKey,
} from '../src/index.ts';
import { SETUP_SAVE_SHARED, SIM_VERSION } from '../scripts/fixtureCases.ts';

describe('sim versions', () => {
  it('round-trip through their key', () => {
    const key = simVersionKey(SIM_VERSION);
    expect(key).toBe(`125-49-${SIM_VERSION.dataHash}`);
    const parsed = parseSimVersionKey(key);
    expect(parsed && sameSimVersion(parsed, SIM_VERSION)).toBe(true);
    expect(parseSimVersionKey('125-49-xyz')).toBeUndefined();
  });

  it('partition engine task identifiers', () => {
    expect(engineTaskIdentifier('verify-match', SIM_VERSION)).toBe(
      `engine:verify-match:125-49-${SIM_VERSION.dataHash}`,
    );
  });
});

describe('MatchSetup', () => {
  it('parses a valid setup and reports no problems', () => {
    const setup = parse(MatchSetup, JSON.parse(JSON.stringify(SETUP_SAVE_SHARED)));
    expect(matchSetupProblems(setup)).toEqual([]);
  });

  it('throws a ProtocolValidationError listing the offending path', () => {
    expect(() => parse(MatchSetup, { ...SETUP_SAVE_SHARED, seed: -1 }, 'setup')).toThrow(
      ProtocolValidationError,
    );
    try {
      parse(MatchSetup, { ...SETUP_SAVE_SHARED, seed: -1 }, 'setup');
    } catch (error) {
      expect((error as ProtocolValidationError).issues.some((i) => i.path === '/seed')).toBe(true);
    }
  });
});

describe('realtime', () => {
  it('registers params and result schemas for every method', () => {
    for (const method of Object.keys(realtimeMethods)) {
      expect(schemaRegistry[realtimeSchemaName(method, 'Params')]).toBeDefined();
      expect(schemaRegistry[realtimeSchemaName(method, 'Result')]).toBeDefined();
    }
    expect(realtimeSchemaName('room.setSeat', 'Params')).toBe('RealtimeRoomSetSeatParams');
    expect(realtimeSchemaName('match.start', 'Event')).toBe('RealtimeEventMatchStart');
  });

  it('tells responses and events apart', () => {
    expect(isValid(RealtimeServerMessage, { type: 'event', event: 'room.closed', data: {} })).toBe(
      true,
    );
    expect(
      isValid(RealtimeServerMessage, { type: 'response', id: '1', ok: true, result: {} }),
    ).toBe(true);
    expect(isValid(RealtimeServerMessage, { type: 'response', id: '1', ok: true })).toBe(false);
  });
});

it('new matches explicitly default to eight tick AI decisions', () => {
  expect(STANDARD_RULES.aiOrderDelay).toBe(8);
});

it('new matches spell out the eight tick building gradient delay', () => {
  expect(STANDARD_RULES.buildingGradientDelay).toBe(8);
});
