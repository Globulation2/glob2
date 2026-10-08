import { afterAll, beforeAll, it, expect, vi } from 'vitest';
import { createHarness, type Harness, type Instance } from './support.ts';
import { registeredPlayer, serveSim, type Player } from './playSupport.ts';
import { Credits } from '@glob2/billing';
import { sourceHash, StudioStore } from '../src/ai-studio/store.ts';
import type { AiStudioDetail, AiStudioProject } from '@glob2/protocol';
let harness: Harness, app: Instance, owner: Player, other: Player;
beforeAll(async () => {
  vi.stubEnv('AI_STUDIO_OPENAI_API_KEY', 'test-not-dispatched');
  harness = await createHarness();
  await serveSim(harness.database.db);
  await harness.database.db
    .updateTable('engine_agents')
    .set({ kinds: ['validate-ai'] })
    .execute();
  app = await harness.start({
    instance: {
      auth: { providers: [], local: { enabled: true } },
      aiStudio: {
        enabled: true,
        model: 'test',
        rate: { version: 'test', input: 10, cachedInput: 1, output: 20 },
        maxRequestCredits: 100,
        maxOutputTokens: 2048,
      },
    },
  });
  owner = await registeredPlayer(app, 'StudioOwner');
  other = await registeredPlayer(app, 'StudioOther');
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  await harness?.close();
  vi.unstubAllEnvs();
});
async function call(method: string, path: string, p: Player = owner, data?: unknown) {
  return fetch(app.url + '/api/v1/ai-studio' + path, {
    method,
    headers: {
      authorization: 'Bearer ' + p.accessToken,
      ...(data === undefined ? {} : { 'content-type': 'application/json' }),
    },
    ...(data === undefined ? {} : { body: JSON.stringify(data) }),
  });
}
it('persists private revisions, resumes events, binds checks and runs to source, and exports data', async () => {
  const created = await call('POST', '/projects', owner, { title: 'My colony' });
  expect(created.status, await created.clone().text()).toBe(200);
  const p = (await created.json()) as AiStudioProject;
  expect((await call('GET', '/projects/' + p.id, other)).status).toBe(404);
  const initial = (await (await call('GET', '/projects/' + p.id)).json()) as AiStudioDetail;
  expect(initial.current.source).toContain('apiVersion: 2');
  const saved = await call('PATCH', '/projects/' + p.id, owner, {
    expectedRevision: 1,
    source: initial.current.source + '\n// user edit',
  });
  expect(saved.status).toBe(200);
  expect(
    (
      await call('PATCH', '/projects/' + p.id, owner, {
        expectedRevision: 1,
        source: 'function step(){}',
      })
    ).status,
  ).toBe(409);
  const detail = (await (await call('GET', '/projects/' + p.id)).json()) as AiStudioDetail;
  expect(detail.revision).toBe(2);
  const events = (await (await call('GET', '/projects/' + p.id + '/events?after=0')).json()) as {
    events: unknown[];
  };
  expect(events.events.length).toBeGreaterThan(0);
  const checked = await call('POST', '/projects/' + p.id + '/check', owner, { revision: 2 });
  expect(checked.status, await checked.clone().text()).toBe(200);
  const check = (await checked.json()) as { uploadId: string };
  const upload = await harness.database.db
    .selectFrom('ai_uploads as u')
    .innerJoin('ai_validations as v', 'v.id', 'u.validation_id')
    .select('v.hash')
    .where('u.id', '=', check.uploadId)
    .executeTakeFirstOrThrow();
  expect(upload.hash).toBe(detail.current.hash);
  const reports = await call('GET', '/projects/' + p.id + '/checks');
  expect(reports.status, await reports.clone().text()).toBe(200);
  expect(((await reports.json()) as { items: unknown[] }).items).toHaveLength(1);
  const runId = crypto.randomUUID();
  const run = await call('POST', '/projects/' + p.id + '/runs', owner, {
    id: runId,
    expectedRevision: 1,
    seed: 19,
    opponent: 'numbi',
  });
  expect(run.status).toBe(200);
  expect(((await run.json()) as { source: string }).source).toBe(initial.current.source);
  const map = await call('GET', '/test-map');
  expect(map.status).toBe(200);
  expect((await map.arrayBuffer()).byteLength).toBeGreaterThan(10);
  expect(sourceHash(detail.current.source)).toBe(detail.current.hash);
  const exported = await fetch(app.url + '/api/v1/accounts/me/export', {
    headers: { authorization: 'Bearer ' + owner.accessToken },
  });
  expect(exported.status).toBe(200);
  expect(await exported.text()).toContain('My colony');
  expect((await call('DELETE', '/projects/' + p.id)).status).toBe(200);
  expect((await call('GET', '/projects/' + p.id)).status).toBe(404);
});

it('enforces the pending-check limit across simultaneous projects', async () => {
  const projects = await Promise.all(
    [1, 2, 3].map(async (n) => {
      const response = await call('POST', '/projects', other, {
        title: 'Concurrent colony ' + n,
        source: 'export function metadata(){return {apiVersion:2,name:"Colony ' + n + '"}}',
      });
      expect(response.status).toBe(200);
      return (await response.json()) as AiStudioProject;
    }),
  );
  const responses = await Promise.all(
    projects.map((p) => call('POST', '/projects/' + p.id + '/check', other, { revision: 1 })),
  );
  expect(responses.map((r) => r.status).sort()).toEqual([200, 200, 409]);
});

it('reconciles simultaneous operator submissions with one durable event', async () => {
  await harness.database.db
    .updateTable('accounts')
    .set({ role: 'admin' })
    .where('id', '=', owner.accountId)
    .execute();
  const response = await call('POST', '/projects', owner, { title: 'Reconciliation' });
  const project = (await response.json()) as AiStudioProject;
  const requestId = crypto.randomUUID();
  await harness.database.db
    .insertInto('ai_studio_requests')
    .values({
      id: requestId,
      project_id: project.id,
      base_revision: 1,
      prompt: 'Lost reply',
      budget: 10,
      status: 'uncertain',
    })
    .execute();
  const credits = new Credits(harness.database.db, 'aiStudio');
  await credits.adjust(owner.accountId, crypto.randomUUID(), 10, 'grant');
  await credits.reserve(owner.accountId, requestId, 5, {
    version: 'test',
    model: 'test',
    input: 10,
    cachedInput: 1,
    output: 20,
  });
  await credits.dispatch(requestId);
  await credits.uncertain(requestId);
  const reconcile = Credits.prototype.reconcile;
  let arrivals = 0;
  let release: () => void = () => {};
  const barrier = new Promise<void>((resolve) => {
    release = resolve;
  });
  const spy = vi.spyOn(Credits.prototype, 'reconcile').mockImplementation(async function (
    this: Credits,
    ...args
  ) {
    if (++arrivals === 2) release();
    await barrier;
    return reconcile.apply(this, args);
  });
  try {
    const responses = await Promise.all(
      [1, 2].map(() =>
        call('POST', '/reconcile', owner, {
          requestId,
          evidence: 'Provider confirmed usage above the reservation',
          usage: { input: 1000000, cachedInput: 0, output: 10 },
        }),
      ),
    );
    expect(responses.map((r) => r.status)).toEqual([200, 200]);
  } finally {
    spy.mockRestore();
  }
  const events = await harness.database.db
    .selectFrom('ai_studio_events')
    .select('id')
    .where('project_id', '=', project.id)
    .where('kind', '=', 'reconciled')
    .execute();
  expect(events).toHaveLength(1);
  expect(await credits.balance(owner.accountId)).toEqual({ balance: 5, reserved: 0, available: 5 });
});

it('keeps owned saved projects and manual editing available when generation is disabled', async () => {
  const disabled = await harness.start({
    instance: {
      auth: { providers: [], local: { enabled: true } },
      aiStudio: { enabled: false, maxOutputTokens: 2048, maxRequestCredits: 100 },
    },
  });
  const author = await registeredPlayer(disabled, 'DisabledStudioOwner');
  try {
    const p = await new StudioStore(harness.database.db).create(
      author.accountId,
      'Saved colony',
      'function step() {}',
    );
    const headers = {
      authorization: 'Bearer ' + author.accessToken,
      'content-type': 'application/json',
    };
    const url = disabled.url + '/api/v1/ai-studio/projects/' + p.id;
    const read = await fetch(url, { headers });
    expect(read.status).toBe(200);
    const saved = await fetch(url, {
      method: 'PATCH',
      headers,
      body: JSON.stringify({ expectedRevision: 1, source: 'function step() {}\n// manual edit' }),
    });
    expect(saved.status).toBe(200);
    const generation = await fetch(url + '/requests', {
      method: 'POST',
      headers,
      body: JSON.stringify({
        id: crypto.randomUUID(),
        expectedRevision: 2,
        text: 'Create an AI',
        budget: 1,
      }),
    });
    expect(generation.status).toBe(404);
  } finally {
    author.client.close();
  }
});
