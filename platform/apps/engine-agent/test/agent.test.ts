// EngineAgent against a recording stand-in for platform-api: what it reports,
// when it gives a job back for a retry, and how it gives up. The real HTTP
// path is covered by queue.test.ts.
import { describe, expect, it } from 'vitest';
import { createLogger } from '@glob2/core';
import type { EngineJob, EngineJobReport, EngineLease, SimVersion } from '@glob2/protocol';
import {
  EngineAgent,
  EngineJobError,
  retryDelaySeconds,
  unsupportedRunner,
  type EngineRunner,
} from '../src/agent.ts';
import type { PlatformClient } from '../src/platform.ts';

const logger = createLogger('agent-test', 'silent');
const SIM: SimVersion = { versionMinor: 125, netProtocol: 49, dataHash: 'ab'.repeat(32) };
const OTHER: SimVersion = { versionMinor: 125, netProtocol: 49, dataHash: 'cd'.repeat(32) };
const HASH = 'ee'.repeat(32);

type Call =
  | { op: 'report'; jobId: string; report: EngineJobReport }
  | { op: 'release'; jobId: string; retryAfterSeconds: number }
  | { op: 'heartbeat'; agentId: string; kinds: string[] }
  | { op: 'deregister'; agentId: string };

/** Records calls; hands out the queued leases one by one. */
function fakePlatform(leases: EngineLease[] = []) {
  const calls: Call[] = [];
  const platform = {
    heartbeat: async (beat: { agentId: string; kinds: string[] }) =>
      void calls.push({ op: 'heartbeat', agentId: beat.agentId, kinds: beat.kinds }),
    deregister: async (agentId: string) => void calls.push({ op: 'deregister', agentId }),
    lease: async () => leases.shift(),
    extend: async () => undefined,
    release: async (jobId: string, _token: string, retryAfterSeconds: number) =>
      void calls.push({ op: 'release', jobId, retryAfterSeconds }),
    report: async (jobId: string, _token: string, report: EngineJobReport) =>
      void calls.push({ op: 'report', jobId, report }),
  } as unknown as PlatformClient;
  return { platform, calls };
}

function lease(attempt = 1, maxAttempts = 3, simVersion = SIM): EngineLease {
  const job: EngineJob = {
    jobId: crypto.randomUUID(),
    kind: 'render-preview',
    simVersion,
    payload: { mapHash: HASH, maxSizePx: 256 },
  };
  return {
    job,
    leaseToken: 't'.repeat(43),
    attempt,
    maxAttempts,
    leaseExpiresAt: new Date(Date.now() + 120_000).toISOString(),
  };
}

function agent(runner: EngineRunner, platform: PlatformClient, id = 'agent-1') {
  return new EngineAgent({ id, simVersion: SIM, build: 'test', runner, platform, logger });
}

describe('EngineAgent', () => {
  it('announces itself with its kinds and deregisters', async () => {
    const { platform, calls } = fakePlatform();
    const a = agent(unsupportedRunner, platform);
    await a.heartbeat();
    await a.deregister();
    expect(calls).toEqual([
      {
        op: 'heartbeat',
        agentId: 'agent-1',
        kinds: ['generate-map', 'validate-map', 'render-preview', 'verify-match'],
      },
      { op: 'deregister', agentId: 'agent-1' },
    ]);
  });

  it('reports results and deterministic failures, and gives other errors back for a retry', async () => {
    let attempt = 0;
    const runner: EngineRunner = {
      kinds: ['render-preview'],
      run: async (job) => {
        attempt++;
        if (attempt === 1)
          return { previewHash: HASH, contentType: 'image/png', width: 256, height: 256 };
        if (attempt === 2) throw new EngineJobError('bad_request', `map ${job.kind} unreadable`);
        throw new Error('platform unavailable');
      },
    };
    const { platform, calls } = fakePlatform();
    const a = agent(runner, platform);
    const [first, second, third] = [lease(), lease(), lease(2)];
    await a.handle(first);
    await a.handle(second);
    await a.handle(third);
    expect(calls).toEqual([
      {
        op: 'report',
        jobId: first.job.jobId,
        report: {
          ok: true,
          result: { previewHash: HASH, contentType: 'image/png', width: 256, height: 256 },
        },
      },
      {
        op: 'report',
        jobId: second.job.jobId,
        report: {
          ok: false,
          error: { code: 'bad_request', message: 'map render-preview unreadable' },
        },
      },
      { op: 'release', jobId: third.job.jobId, retryAfterSeconds: retryDelaySeconds(2) },
    ]);
    expect(retryDelaySeconds(1)).toBe(5);
    expect(retryDelaySeconds(20)).toBe(300);
  });

  it('reports a retried error as internal on the last attempt', async () => {
    const runner: EngineRunner = {
      kinds: ['render-preview'],
      run: async () => {
        throw new Error('engine killed by SIGSEGV');
      },
    };
    const { platform, calls } = fakePlatform();
    await agent(runner, platform).handle(lease(3, 3));
    expect(calls).toHaveLength(1);
    const [call] = calls as Extract<Call, { op: 'report' }>[];
    expect(call!.report.ok).toBe(false);
    const error = (call!.report as { error: { code: string; message: string } }).error;
    expect(error.code).toBe('internal');
    expect(error.message).toMatch(/gave up after 3 attempts: engine killed by SIGSEGV/);
  });

  it('never runs another sim version and reports every kind unsupported by default', async () => {
    const { platform, calls } = fakePlatform();
    const a = agent(unsupportedRunner, platform);
    const foreign = lease(1, 3, OTHER);
    await a.handle(foreign);
    await a.handle(lease(3, 3, OTHER));
    expect(calls[0]).toMatchObject({ op: 'release', jobId: foreign.job.jobId });
    expect(calls[1]).toMatchObject({ op: 'report', report: { ok: false } });
    expect(JSON.stringify(calls[1])).toMatch(/reached agent/);

    const { platform: p2, calls: c2 } = fakePlatform();
    await agent(unsupportedRunner, p2).handle(lease());
    expect(c2[0]).toMatchObject({
      op: 'report',
      report: { ok: false, error: { code: 'unsupported' } },
    });
  });

  it('runs leased jobs in its loop and stops cleanly', async () => {
    const leases = [lease(), lease()];
    const ran: string[] = [];
    const runner: EngineRunner = {
      kinds: ['render-preview'],
      run: async (job) => {
        ran.push(job.jobId);
        return { previewHash: HASH, contentType: 'image/png', width: 1, height: 1 };
      },
    };
    const ids = leases.map((l) => l.job.jobId);
    const { platform, calls } = fakePlatform(leases);
    const a = new EngineAgent({
      id: 'loop',
      simVersion: SIM,
      build: 'test',
      runner,
      platform,
      logger,
      concurrency: 2,
      pollMs: 10,
    });
    a.start();
    const deadline = Date.now() + 5000;
    while (calls.length < 2 && Date.now() < deadline) await new Promise((r) => setTimeout(r, 10));
    await a.stop();
    expect(ran.sort()).toEqual(ids.sort());
    expect(calls.map((c) => c.op)).toEqual(['report', 'report']);
  });
});
