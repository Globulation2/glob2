import { writeFile } from 'node:fs/promises';
import { describe, expect, it } from 'vitest';
import { DEFAULT_LIMITS, EngineCrashError, GlobEngine } from '../src/engine.ts';
import type { RunOptions } from '../src/process.ts';

const rejected = {
  hash: 'a'.repeat(64),
  suite: 1,
  valid: false,
  reason: 'No terrain definitions',
  minVersionMinor: 144,
  terrainCount: 0,
  resourceCount: 0,
};

function validator(code: number, report: unknown = rejected) {
  return new GlobEngine({
    binary: '/unused',
    workdir: '/unused',
    limits: DEFAULT_LIMITS,
    maxOutputBytes: 1024 * 1024,
    processLauncher: async (options: RunOptions) => {
      expect(options.args.slice(0, 2)).toEqual(['map', 'validate-set']);
      const output = options.args[options.args.indexOf('--report-file') + 1]!;
      await writeFile(output, JSON.stringify(report));
      return { code, signal: null, timedOut: false, stdout: '', stderr: '', ms: 1 };
    },
  });
}

describe('set validation CLI verdicts', () => {
  it('retains the rejection report when CLI 2 returns rejected-input status', async () => {
    expect(await validator(2).validateSet(Buffer.from('{}'))).toEqual({
      report: rejected,
      png: undefined,
    });
  });

  it('still rejects operational failures and malformed rejection reports', async () => {
    await expect(validator(3).validateSet(Buffer.from('{}'))).rejects.toBeInstanceOf(
      EngineCrashError,
    );
    await expect(validator(2, {}).validateSet(Buffer.from('{}'))).rejects.toThrow();
  });
});
