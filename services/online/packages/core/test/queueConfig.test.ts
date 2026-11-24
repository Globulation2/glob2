import { describe, expect, it } from 'vitest';
import { join } from 'node:path';
import { writeFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import {
  QueueConfigError,
  RANKED_ACCEPT_SECONDS,
  defaultMapPool,
  loadInstanceConfig,
  resolveQueue,
} from '../src/index.ts';

describe('queue config', () => {
  it('serves the current Even Ground revision in every queue mode', () => {
    for (const mode of ['1v1', '2v2'] as const) {
      expect(defaultMapPool(mode).find((m) => m.generatorId === 'even-ground')?.revision).toBe(5);
    }
  });
  it('serves the Caravanserai revision with owner-specific resource placement', () => {
    expect(defaultMapPool('1v1').find((m) => m.generatorId === 'caravanserai')?.revision).toBe(5);
  });
  it('fills defaults: ranked accept step, casual none, fair 128x128 map pool', () => {
    const ranked = resolveQueue({ id: 'r', name: 'R', mode: '1v1', rated: true });
    expect(ranked.acceptSeconds).toBe(RANKED_ACCEPT_SECONDS);
    expect(ranked.aiBackfillSeconds).toBeUndefined();
    expect(ranked.aiPool).toHaveLength(8);
    expect(ranked.ratingWindow).toEqual({ initial: 100, perSecond: 5, max: 800 });
    const ids = ranked.mapPool.map((m) => m.generatorId);
    for (const required of ['symmetric-arena', 'even-ground', 'marchland', 'carousel', 'polder']) {
      expect(ids).toContain(required);
    }
    expect(ids).not.toContain('emoji');
    expect(ids).not.toContain('gauntlet');
    expect(ranked.mapPool.every((m) => m.params['width'] === 7 && m.params['teams'] === 2)).toBe(
      true,
    );

    const casual = resolveQueue({
      id: 'c',
      name: 'C',
      mode: '1v1',
      rated: false,
      ratingWindow: { max: 400 },
    });
    expect(casual.acceptSeconds).toBe(0);
    expect(casual.ratingWindow).toEqual({ initial: 100, perSecond: 5, max: 400 });
  });

  it('uses four-colony maps for 2v2 and drops generators that cannot fit them at 128x128', () => {
    const pool = defaultMapPool('2v2');
    expect(pool.every((m) => m.params['teams'] === 4)).toBe(true);
    const ids = pool.map((m) => m.generatorId);
    expect(ids).not.toContain('hills');
    expect(ids).not.toContain('rice-terraces');
    expect(
      defaultMapPool('1v1').find((m) => m.generatorId === 'rice-terraces')?.params['slant'],
    ).toBe(0);
    expect(pool.length).toBeLessThan(defaultMapPool('1v1').length);
  });

  it('rejects map pools whose team count does not match the mode', () => {
    expect(() =>
      resolveQueue({
        id: 'x',
        name: 'X',
        mode: '2v2',
        rated: true,
        mapPool: [
          {
            generatorId: 'even-ground',
            revision: 2,
            params: { width: 7, height: 7, teams: 2 },
            candidates: 5,
            startingUnitLevel: 0,
          },
        ],
      }),
    ).toThrow(QueueConfigError);
    const dir = mkdtempSync(join(tmpdir(), 'glob2-queue-'));
    const path = join(dir, 'instance.yaml');
    writeFileSync(
      path,
      [
        'name: X',
        'guests: { enabled: true }',
        'auth: { providers: [], local: { enabled: false } }',
        'access: { policy: allow-all }',
        'queues:',
        '  - { id: q, name: Q, mode: 2v2, rated: true, aiPool: [] , aiBackfillSeconds: 10 }',
      ].join('\n'),
    );
    expect(() => loadInstanceConfig(path)).toThrow(/non-empty aiPool/);
  });

  it('marks a queue assisted only when the operator says so', () => {
    expect(resolveQueue({ id: 'r', name: 'R', mode: '1v1', rated: true }).assisted).toBe(false);
    expect(
      resolveQueue({ id: 'r', name: 'R', mode: '1v1', rated: true, assisted: false }).assisted,
    ).toBe(false);
    const mcp = resolveQueue({ id: 'm', name: 'M', mode: '2v2', rated: true, assisted: true });
    expect(mcp.assisted).toBe(true);
    // Everything else resolves as for any queue.
    expect(mcp.acceptSeconds).toBe(RANKED_ACCEPT_SECONDS);
    expect(mcp.mapPool).toEqual(defaultMapPool('2v2'));
  });

  it('rejects an assisted setting that is not a boolean', () => {
    const dir = mkdtempSync(join(tmpdir(), 'glob2-queue-'));
    const path = join(dir, 'instance.yaml');
    writeFileSync(
      path,
      [
        'name: X',
        'guests: { enabled: true }',
        'auth: { providers: [], local: { enabled: false } }',
        'access: { policy: allow-all }',
        'queues:',
        '  - { id: q, name: Q, mode: 1v1, rated: false, assisted: "yes" }',
      ].join('\n'),
    );
    expect(() => loadInstanceConfig(path)).toThrow();
  });

  it('ships assisted twins of the ranked and casual 1v1 queues in the example', () => {
    const example = join(import.meta.dirname, '..', '..', '..', 'instance.example.yaml');
    const queues = loadInstanceConfig(example).queues.map((q) => resolveQueue(q));
    const assisted = queues.filter((q) => q.assisted);
    const ordinary = queues.filter((q) => !q.assisted);
    expect(assisted.map((q) => q.id)).toEqual(['mcp-ranked-1v1', 'mcp-casual-1v1']);
    expect(ordinary.map((q) => q.id)).toEqual(['ranked-1v1', 'ranked-2v2', 'casual-1v1']);
    // Separate ids are separate ladders; a rated and an unrated one, as for everyone else.
    expect(new Set(queues.map((q) => q.id)).size).toBe(queues.length);
    expect(assisted.map((q) => q.rated)).toEqual([true, false]);
    // The ordinary queues come first, so clients that ignore the flag default to them.
    expect(queues[0]?.assisted).toBe(false);
  });
});
