import { readFileSync } from 'node:fs';
import { it, expect } from 'vitest';
import { HiveProgram, parse } from '@glob2/protocol';
it('ships precisely the executable examples used by the native contract tests', () => {
  const examples = JSON.parse(
    readFileSync(new URL('../src/examples.json', import.meta.url), 'utf8'),
  ) as Record<string, string>;
  for (const [name, source] of Object.entries(examples))
    expect(source).toBe(
      readFileSync(new URL(`../../../../examples/hive-mind/${name}.js`, import.meta.url), 'utf8'),
    );
});
it('rejects fractional or sub-25-tick standing orders at the protocol boundary', () => {
  const program = {
    id: '12345678-1234-4234-8234-123456789abc',
    revision: 1,
    name: 'Guard',
    description: 'Watch our colony',
    source: 'function step(){return {}}',
    intervalTicks: 25,
  };
  expect(parse(HiveProgram, program).intervalTicks).toBe(25);
  for (const intervalTicks of [0, 24, 25.5, -25])
    expect(() => parse(HiveProgram, { ...program, intervalTicks })).toThrow();
});
it('the agent contract contains no scenario mutation or omniscient declarations', () => {
  const types = readFileSync(
    new URL('../../../../examples/hive-mind/hive.d.ts', import.meta.url),
    'utf8',
  );
  const prompt = readFileSync(new URL('../src/commander-api.txt', import.meta.url), 'utf8');
  for (const text of [types, prompt])
    for (const forbidden of [
      'objectives():',
      'hints():',
      'fertility?:',
      'type Effect',
      'ctx.game.objectives(',
      'ctx.game.hints(',
      'ctx.game.interface(',
    ])
      expect(text).not.toContain(forbidden);
});
