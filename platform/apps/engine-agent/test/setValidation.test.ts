import { expect, it } from 'vitest';
import { createRunner, SIM } from './support.ts';
import { HeadlessEngineRunner } from '../src/runners.ts';
import { createSetValidator } from '../src/setValidation.ts';
import { putContent } from '@glob2/core';
import { randomUUID } from 'node:crypto';
import { DEFAULT_LIMITS } from '../src/engine.ts';

it('advertises set checks only with isolation and binds results to the input hash', async () => {
  const h = await createRunner();
  try {
    expect(h.runner.kinds).not.toContain('validate-set');
    const catalog = await h.engine.catalog();
    const source = new Uint8Array([123, 125]);
    const stored = await putContent(h.store, source);
    let resultHash = stored.sha256;
    const runner = new HeadlessEngineRunner({
      engine: h.engine,
      catalog: { ...catalog, commands: [...catalog.commands, 'validate_set'] },
      simVersion: SIM,
      blobs: h.blobs,
      setValidator: async (bytes) => {
        expect([...bytes]).toEqual([...source]);
        return {
          report: {
            hash: resultHash,
            suite: 1,
            valid: false,
            minVersionMinor: 144,
            terrainCount: 0,
            resourceCount: 0,
            reason: 'No entries',
          },
        };
      },
    });
    expect(runner.kinds).toContain('validate-set');
    const job = {
      kind: 'validate-set' as const,
      jobId: randomUUID(),
      simVersion: SIM,
      payload: { blobHash: stored.sha256, suite: 1 as const },
    };
    expect(await runner.run(job, new AbortController().signal)).toMatchObject({
      valid: false,
      hash: stored.sha256,
    });
    resultHash = '00'.repeat(32);
    await expect(runner.run(job, new AbortController().signal)).rejects.toThrow(/hash mismatch/);
    if (process.platform !== 'linux')
      await expect(
        createSetValidator({
          binary: '/engine',
          workdir: '/',
          limits: DEFAULT_LIMITS,
          maxOutputBytes: 65536,
        }),
      ).rejects.toThrow(/Linux/);
  } finally {
    await h.close();
  }
});
