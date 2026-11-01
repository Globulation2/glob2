// The MatchSetup seats a room starts with (roomMatchSeats): taken seats become
// the players in room seat order and keep their map teams; empty and locked
// seats become closed seats after them. Pure: no database.
import { describe, expect, it } from 'vitest';
import { STANDARD_RULES, checkDocument as check, type MatchSetup } from '@glob2/protocol';
import { roomMatchSeats } from '../src/play/rooms.ts';

const SIM = { versionMinor: 125, netProtocol: 49, dataHash: 'ab'.repeat(32) };
const HOST = '11111111-1111-4111-8111-111111111111';
const GUEST = '22222222-2222-4222-8222-222222222222';
type Row = Parameters<typeof roomMatchSeats>[0][number];
const empty = (seat: number): Row => ({
  seat,
  team: seat,
  occupant: 'open',
  account_id: null,
  ai_id: null,
  ai_name: null,
});
const human = (seat: number, account: string): Row => ({
  ...empty(seat),
  occupant: 'human',
  account_id: account,
});
const names = new Map([
  [HOST, 'Host'],
  [GUEST, 'Guest'],
]);

function setupOf(rows: Row[], teams = rows.length): MatchSetup {
  return {
    schemaVersion: 1,
    simVersion: SIM,
    seed: 1,
    map: { kind: 'catalog', hash: 'a'.repeat(64) },
    teams: Array.from({ length: teams }, (_, team) => ({ team, alliance: team })),
    seats: roomMatchSeats(rows, names),
    rules: STANDARD_RULES,
    experiments: [],
  };
}

describe('roomMatchSeats', () => {
  it('numbers the players first and closes the empty teams after them', () => {
    const setup = setupOf([
      human(0, HOST),
      empty(1),
      human(2, GUEST),
      { ...empty(3), occupant: 'ai', ai_id: 'nicowar', ai_name: 'Nico' },
      empty(4),
    ]);
    expect(setup.seats).toEqual([
      { seat: 0, kind: 'human', team: 0, name: 'Host', accountId: HOST },
      { seat: 1, kind: 'human', team: 2, name: 'Guest', accountId: GUEST },
      { seat: 2, kind: 'ai', team: 3, name: 'Nico', ai: 'nicowar' },
      { seat: 3, kind: 'closed', team: 1 },
      { seat: 4, kind: 'closed', team: 4 },
    ]);
    expect(check('MatchSetup', setup).stage).toBe('ok');
  });

  it('sends no closed seats when every seat is taken', () => {
    const setup = setupOf([human(0, HOST), human(1, GUEST)]);
    expect(setup.seats.map((s) => s.kind)).toEqual(['human', 'human']);
    expect(check('MatchSetup', setup).stage).toBe('ok');
  });

  it('closes a team only when no taken seat plays it', () => {
    const setup = setupOf([human(0, HOST), { ...empty(1), team: 0 }, human(2, GUEST)]);
    expect(setup.seats.map((s) => [s.kind, s.team])).toEqual([
      ['human', 0],
      ['human', 2],
    ]);
    expect(check('MatchSetup', setup).stage).toBe('ok');
  });
});
