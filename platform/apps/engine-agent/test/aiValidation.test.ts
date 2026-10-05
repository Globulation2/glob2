import { expect, it } from 'vitest';
import { checksumRecords, sameContinuation } from '../src/aiValidation.ts';
import { pendingAiReport, passedAiReport, AI_CHECKS } from '@glob2/protocol';
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
