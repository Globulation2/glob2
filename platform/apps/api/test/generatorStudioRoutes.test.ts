import { beforeAll, afterAll, expect, it, vi } from 'vitest';
import { randomUUID } from 'node:crypto';
import { createHarness, type Harness, type Instance } from './support.ts';
import { registeredPlayer, serveSim, type Player } from './playSupport.ts';
import {
  decodeGeneratorDraft,
  encodeGeneratorDraft,
  type AiStudioDetail,
  type AiStudioProject,
} from '@glob2/protocol';
let harness: Harness, app: Instance, owner: Player, other: Player;
beforeAll(async () => {
  vi.stubEnv('GENERATOR_STUDIO_OPENAI_API_KEY', 'test-not-dispatched');
  harness = await createHarness();
  await serveSim(harness.database.db);
  await harness.database.db
    .updateTable('engine_agents')
    .set({ kinds: ['validate-generator'] })
    .execute();
  app = await harness.start({
    instance: {
      auth: { providers: [], local: { enabled: true } },
      generatorStudio: {
        enabled: true,
        model: 'test',
        rate: { version: 'test', input: 10, cachedInput: 1, output: 20 },
        maxRequestCredits: 100,
        maxOutputTokens: 2048,
      },
    },
  });
  owner = await registeredPlayer(app, 'GeneratorOwner');
  other = await registeredPlayer(app, 'GeneratorOther');
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  await harness?.close();
  vi.unstubAllEnvs();
});
const call = (method: string, path: string, data?: unknown, who = owner) =>
  fetch(app.url + '/api/v1/generator-studio' + path, {
    method,
    headers: {
      authorization: 'Bearer ' + who.accessToken,
      ...(data === undefined ? {} : { 'content-type': 'application/json' }),
    },
    ...(data === undefined ? {} : { body: JSON.stringify(data) }),
  });
it('keeps generator authoring and tools disabled until the instance opts in', async () => {
  const disabled = await harness.start({ origin: app.url });
  const headers = { authorization: 'Bearer ' + owner.accessToken };
  const account = await disabled.app.inject({
    method: 'GET',
    url: '/api/v1/generator-studio/account',
    headers,
  });
  expect(account.statusCode).toBe(200);
  expect(account.json().enabled).toBe(false);
  for (const [method, url] of [
    ['GET', '/projects'],
    ['POST', '/projects'],
    ['POST', '/projects/' + randomUUID() + '/runs'],
    ['POST', '/projects/' + randomUUID() + '/check'],
  ] as const) {
    const result = await disabled.app.inject({
      method,
      url: '/api/v1/generator-studio' + url,
      headers,
    });
    expect(result.statusCode).toBe(404);
  }
});
it('owns private drafts, runs and check receipts and exports the complete project', async () => {
  const created = await call('POST', '/projects', { title: 'Landscape' });
  expect(created.status, await created.clone().text()).toBe(200);
  const p = (await created.json()) as AiStudioProject,
    url = '/projects/' + p.id;
  expect((await call('GET', url, undefined, other)).status).toBe(404);
  const initial = (await (await call('GET', url)).json()) as AiStudioDetail;
  const broken = encodeGeneratorDraft({
    ...decodeGeneratorDraft(initial.current.source),
    manifest: '{',
  });
  expect((await call('PATCH', url, { expectedRevision: 1, source: broken })).status).toBe(200);
  expect(
    (
      await call('POST', url + '/runs', {
        id: randomUUID(),
        expectedRevision: 2,
        settings: {
          seed: 19,
          params: { width: 7, height: 7, teams: 4, workers: 4 },
          candidates: 1,
          startingUnitLevel: 0,
        },
      })
    ).status,
  ).toBe(400);
  expect((await call('PATCH', url, { expectedRevision: 2, restoreRevision: 1 })).status).toBe(200);
  const settings = {
      seed: 19,
      params: { width: 7, height: 7, teams: 4, workers: 4 },
      candidates: 1,
      startingUnitLevel: 0,
    },
    runId = randomUUID();
  const run = await call('POST', url + '/runs', { id: runId, expectedRevision: 3, settings });
  expect(run.status, await run.clone().text()).toBe(200);
  const payload = (await run.json()) as { source: string; revision: number };
  expect(JSON.parse(payload.source).manifest.entry).toBe('generator.js');
  expect(payload.revision).toBe(3);
  // JSONB order differs from client order; idempotency compares objects, not serialized key order.
  expect(
    (await call('POST', url + '/runs', { id: runId, expectedRevision: 3, settings })).status,
  ).toBe(200);
  expect(
    (
      await call('POST', url + '/runs', {
        id: runId,
        expectedRevision: 3,
        settings: { ...settings, seed: 20 },
      })
    ).status,
  ).toBe(409);
  expect(
    (await call('POST', url + '/run-result', { runId, summary: 'client-only result' }, other))
      .status,
  ).toBe(404);
  expect(
    (await call('POST', url + '/run-result', { runId, summary: 'client-only result' })).status,
  ).toBe(200);
  const check = await call('POST', url + '/check', { revision: 3, settings });
  expect(check.status, await check.clone().text()).toBe(200);
  const report = (await (await call('GET', url + '/checks')).json()) as {
    items: { revision: number; draft_hash: string; status: string }[];
  };
  expect(report.items[0]?.revision).toBe(3);
  expect(report.items[0]?.draft_hash).toBe(initial.current.hash);
  expect(report.items[0]?.status).toBe('pending');
  expect((await call('POST', url + '/check', { revision: 3, settings }, other)).status).toBe(404);
  await harness.database.db
    .insertInto('generator_studio_requests')
    .values([
      {
        id: randomUUID(),
        project_id: p.id,
        base_revision: 3,
        prompt: 'first',
        budget: 1,
        status: 'completed',
        created_at: new Date('2026-01-01'),
      },
      {
        id: randomUUID(),
        project_id: p.id,
        base_revision: 3,
        prompt: 'second',
        budget: 1,
        status: 'completed',
        created_at: new Date('2026-01-02'),
      },
    ])
    .execute();
  const conversation = (await (await call('GET', url)).json()) as AiStudioDetail;
  expect(conversation.requests.map((r) => r.prompt)).toEqual(['second', 'first']);
  const exported = await fetch(app.url + '/api/v1/accounts/me/export', {
    headers: { authorization: 'Bearer ' + owner.accessToken },
  });
  expect(exported.status).toBe(200);
  expect(
    ((await exported.json()) as { generatorStudio: { revisions: unknown[] } }).generatorStudio
      .revisions,
  ).toHaveLength(3);
});
