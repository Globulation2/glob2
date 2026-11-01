// /internal/v1/engine: what an engine agent key can and cannot do. The happy
// path end to end (agent, fake engine, worker) is apps/engine-agent's
// queue.test.ts.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { putContent, submitEngineJob } from '@glob2/core';
import { ENGINE_LEASE_HEADER, simVersionKey } from '@glob2/protocol';
import { SIM, createHarness, type Harness, type Instance } from './support.ts';

const KEY = 'a'.repeat(40);
const PINNED = 'b'.repeat(40);
let harness: Harness;
let api: Instance;

beforeAll(async () => {
  harness = await createHarness();
  api = await harness.start({
    engineAgentKeys: [{ key: KEY }, { key: PINNED, agentId: 'agent-pinned' }],
  });
});

afterAll(async () => {
  await harness?.close();
});

function call(
  method: string,
  path: string,
  options: { key?: string; json?: unknown; lease?: string; body?: Buffer } = {},
) {
  const headers: Record<string, string> = {};
  if (options.key !== undefined) headers['authorization'] = `Bearer ${options.key}`;
  if (options.lease) headers[ENGINE_LEASE_HEADER] = options.lease;
  let body: string | Buffer | undefined;
  if (options.json !== undefined) {
    headers['content-type'] = 'application/json';
    body = JSON.stringify(options.json);
  } else if (options.body) {
    headers['content-type'] = 'application/octet-stream';
    body = options.body;
  }
  return fetch(`${api.url}${path}`, { method, headers, ...(body ? { body } : {}) });
}

const leaseBody = (agentId = 'agent-1') => ({
  agentId,
  simVersion: SIM,
  kinds: ['render-preview'],
  leaseSeconds: 60,
});

describe('engine agent API', () => {
  it('refuses requests without a known agent key and pinned keys acting as others', async () => {
    expect(
      (await call('POST', '/internal/v1/engine/jobs/lease', { json: leaseBody() })).status,
    ).toBe(401);
    expect(
      (
        await call('POST', '/internal/v1/engine/jobs/lease', {
          key: 'c'.repeat(40),
          json: leaseBody(),
        })
      ).status,
    ).toBe(401);
    const beat = { agentId: 'agent-1', simVersion: SIM, kinds: ['render-preview'], build: 't' };
    expect(
      (await call('POST', '/internal/v1/engine/agents/heartbeat', { key: PINNED, json: beat }))
        .status,
    ).toBe(403);
    expect(
      (
        await call('POST', '/internal/v1/engine/agents/heartbeat', {
          key: PINNED,
          json: { ...beat, agentId: 'agent-pinned' },
        })
      ).status,
    ).toBe(204);
    const row = await harness.database.db
      .selectFrom('engine_agents')
      .selectAll()
      .where('id', '=', 'agent-pinned')
      .executeTakeFirstOrThrow();
    expect(row.sim_version).toBe(simVersionKey(SIM));
    // Relay keys are not agent keys, and the reverse.
    expect(
      (await call('POST', '/internal/v1/relays/heartbeat', { key: KEY, json: {} })).status,
    ).toBe(401);
  });

  it('scopes blob reads to the leased job and needs a live lease for everything else', async () => {
    const db = harness.database.db;
    const named = await putContent(harness.blobs, Buffer.from('the map the job names'));
    const other = await putContent(harness.blobs, Buffer.from('someone else’s upload'));
    const jobId = await submitEngineJob(db, {
      kind: 'render-preview',
      simVersion: SIM,
      payload: { mapHash: named.sha256, maxSizePx: 64 },
    });

    const leased = await call('POST', '/internal/v1/engine/jobs/lease', {
      key: KEY,
      json: leaseBody(),
    });
    expect(leased.status).toBe(200);
    const lease = (await leased.json()) as { leaseToken: string; job: { jobId: string } };
    expect(lease.job.jobId).toBe(jobId);
    // Nothing else to lease.
    expect(
      (
        await call('POST', '/internal/v1/engine/jobs/lease', {
          key: KEY,
          json: leaseBody('agent-2'),
        })
      ).status,
    ).toBe(204);

    const read = await call('GET', `/internal/v1/engine/blobs/${named.sha256}`, {
      key: KEY,
      lease: lease.leaseToken,
    });
    expect(read.status).toBe(200);
    expect(Buffer.from(await read.arrayBuffer()).toString()).toBe('the map the job names');
    expect(
      (
        await call('GET', `/internal/v1/engine/blobs/${other.sha256}`, {
          key: KEY,
          lease: lease.leaseToken,
        })
      ).status,
    ).toBe(403);
    expect(
      (await call('GET', `/internal/v1/engine/blobs/${named.sha256}`, { key: KEY })).status,
    ).toBe(401);
    expect(
      (
        await call('GET', `/internal/v1/engine/blobs/${named.sha256}`, {
          key: KEY,
          lease: 'x'.repeat(43),
        })
      ).status,
    ).toBe(409);

    // Stores outputs by content, only with known content types.
    const stored = await fetch(
      `${api.url}/internal/v1/engine/blobs?contentType=image%2Fpng&visibility=public`,
      {
        method: 'PUT',
        headers: {
          authorization: `Bearer ${KEY}`,
          [ENGINE_LEASE_HEADER]: lease.leaseToken,
          'content-type': 'application/octet-stream',
        },
        body: Buffer.from('png bytes'),
      },
    );
    expect(stored.status).toBe(201);
    const receipt = (await stored.json()) as { sha256: string; size: number };
    expect(receipt.size).toBe(9);
    expect(
      await db
        .selectFrom('blobs')
        .selectAll()
        .where('sha256', '=', receipt.sha256)
        .executeTakeFirst(),
    ).toMatchObject({ content_type: 'image/png', visibility: 'public' });
    const badType = await fetch(`${api.url}/internal/v1/engine/blobs?contentType=text%2Fhtml`, {
      method: 'PUT',
      headers: {
        authorization: `Bearer ${KEY}`,
        [ENGINE_LEASE_HEADER]: lease.leaseToken,
        'content-type': 'application/octet-stream',
      },
      body: Buffer.from('<script>'),
    });
    expect(badType.status).toBe(400);

    // A pinned key cannot touch another agent's lease.
    expect(
      (
        await call('POST', `/internal/v1/engine/jobs/${jobId}/extend`, {
          key: PINNED,
          lease: lease.leaseToken,
          json: { leaseSeconds: 60 },
        })
      ).status,
    ).toBe(403);

    // Report once; a repeat is accepted as a duplicate; then the lease is gone.
    const report = {
      ok: true,
      result: { previewHash: receipt.sha256, contentType: 'image/png', width: 1, height: 1 },
    };
    for (let i = 0; i < 2; i++) {
      expect(
        (
          await call('POST', `/internal/v1/engine/jobs/${jobId}/result`, {
            key: KEY,
            lease: lease.leaseToken,
            json: report,
          })
        ).status,
      ).toBe(204);
    }
    expect(
      (
        await call('GET', `/internal/v1/engine/blobs/${named.sha256}`, {
          key: KEY,
          lease: lease.leaseToken,
        })
      ).status,
    ).toBe(409);
    const job = await db
      .selectFrom('engine_jobs')
      .select(['reported_at', 'leased_by'])
      .where('id', '=', jobId)
      .executeTakeFirstOrThrow();
    expect(job.leased_by).toBe('agent-1');
    expect(job.reported_at).not.toBeNull();
  });
});
