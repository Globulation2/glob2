import { writeFile } from 'node:fs/promises';
import type * as FsPromises from 'node:fs/promises';
import { join } from 'node:path';
import { expect, it, vi } from 'vitest';
import { runProcess } from '@glob2/engine/process';
import type * as EngineProcess from '@glob2/engine/process';
import { checksumRecords, createAiValidator, sameContinuation } from '../src/aiValidation.ts';
import { pendingAiReport, passedAiReport, AI_CHECKS } from '@glob2/protocol';

vi.mock('node:fs/promises', async (importOriginal) => {
  const actual = await importOriginal<typeof FsPromises>();
  return {
    ...actual,
    // These tests exercise result classification, independent of whether the
    // test host supports Bubblewrap. Real namespace probes run separately.
    access: async (path: string) => {
      if (path !== '/usr/bin/bwrap') await actual.access(path);
    },
    statfs: async () => ({ type: 0x01021994, blocks: 1024, bsize: 4096 }),
  };
});
vi.mock('@glob2/engine/process', async (importOriginal) => ({
  ...(await importOriginal<typeof EngineProcess>()),
  runProcess: vi.fn(),
}));

async function validator(diagnostic: 'controller' | 'isolation' = 'controller') {
  vi.mocked(runProcess).mockImplementation(async (options) => {
    const args = options.args;
    const scratch = args[args.indexOf('--bind') + 1]!;
    const sourceCheck = args.includes('/job/source.js');
    const isolatedFailure = sourceCheck && diagnostic === 'isolation';
    const output = args[args.indexOf('--output-dir') + 1];
    if (args.includes('--output-dir') && !output?.endsWith('-initial')) {
      await writeFile(
        join(scratch, output!.slice('/job/'.length), 'result.json'),
        JSON.stringify({
          status: 'completed',
          ticks: 4096,
          javascriptControllers: [
            {
              disabled: output !== '/job/probe',
              rejectedDecision: false,
              diagnostic: 'bwrap: script-authored failure',
            },
          ],
        }),
      );
    }
    return {
      code: isolatedFailure ? 1 : 0,
      signal: null,
      timedOut: false,
      ms: 1,
      stdout: JSON.stringify({
        valid: true,
        metadata: { apiVersion: 1, name: 'Example', description: '', author: '', version: '' },
      }),
      stderr: isolatedFailure
        ? 'bwrap: Creating new namespace failed: Operation not permitted'
        : 'JavaScript AI player 0: bwrap: script-authored failure',
    };
  });
  return createAiValidator(
    { binary: '/fake-engine', workdir: process.cwd() },
    { versionMinor: 1, netProtocol: 1, dataHash: 'a'.repeat(64) },
  );
}

it('keeps script-authored bwrap text as a compatibility diagnostic', async () => {
  const validate = await validator();
  const report = await validate(Buffer.from('function step() {}'), new AbortController().signal);
  expect(report.valid).toBe(false);
  expect(report.checks.find((c) => c.id === 'gameplay')).toMatchObject({
    status: 'failed',
    message: 'bwrap: script-authored failure',
  });
  expect(report.checks.find((c) => c.id === 'determinism')?.status).toBe('skipped');
});

it('still treats an actual Bubblewrap failure as retryable infrastructure failure', async () => {
  const validate = await validator('isolation');
  await expect(
    validate(Buffer.from('function step() {}'), new AbortController().signal),
  ).rejects.toThrow('AI isolation failed: bwrap: Creating new namespace failed');
});

it('reports malformed UTF-8 as a failed file check with dependent checks skipped', async () => {
  const validate = await validator();
  const report = await validate(Buffer.from([0xff]), new AbortController().signal);
  expect(report.valid).toBe(false);
  expect(report.checks[0]).toMatchObject({
    status: 'failed',
    message: expect.stringContaining('UTF-8'),
  });
  expect(report.checks.slice(1).every((c) => c.status === 'skipped')).toBe(true);
});

function trace(ticks: number[], alter = 0) {
  const b = Buffer.alloc(20 + ticks.length * 8);
  b.write('GCS1');
  b.writeUInt32LE(ticks.length, 12);
  ticks.forEach((t, i) => {
    b.writeUInt32LE(t, 20 + i * 8);
    b.writeUInt32LE(t + 100 + alter, 24 + i * 8);
  });
  return b;
}
it('compares complete continuation records and refuses missing, altered and malformed traces', () => {
  const all = trace([2047, 2048, 2049]);
  expect(sameContinuation(all, trace([2048, 2049]), 2048)).toBe(true);
  expect(sameContinuation(all, trace([2048]), 2048)).toBe(false);
  expect(sameContinuation(all, trace([2048, 2049], 1), 2048)).toBe(false);
  expect(() => checksumRecords(Buffer.from('GCS1'))).toThrow();
  expect(() => checksumRecords(trace([2, 2]))).toThrow();
  expect(() => checksumRecords(Buffer.concat([all, Buffer.alloc(1)]))).toThrow();
});
it('a valid flag never substitutes for all distinct passing checks', () => {
  const r = pendingAiReport('a'.repeat(64), 'engine');
  r.valid = true;
  expect(passedAiReport(r)).toBe(false);
  r.checks = AI_CHECKS.map((id) => ({ id, status: 'passed' }));
  expect(passedAiReport(r)).toBe(true);
  r.checks[6] = r.checks[0]!;
  expect(passedAiReport(r)).toBe(false);
});
