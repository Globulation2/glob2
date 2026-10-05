import { beforeAll, afterAll, describe, it, expect } from 'vitest';
import { sql } from 'kysely';
import {
  pendingAiReport,
  simVersionKey,
  checkDocument,
  type AiUpload,
  type AiInfo,
  type AiList,
  type AiDetail,
} from '@glob2/protocol';
import { handleEngineJobResult } from '@glob2/play';
import { maintainAiLibrary } from '@glob2/core';
import { createHarness, SIM, type Harness, type Instance } from './support.ts';
import { registeredPlayer, guestPlayer, serveSim, type Player } from './playSupport.ts';
let harness: Harness, app: Instance, owner: Player, other: Player, guest: Player;
beforeAll(async () => {
  harness = await createHarness();
  await serveSim(harness.database.db);
  await harness.database.db
    .updateTable('engine_agents')
    .set({ kinds: ['validate-ai'] })
    .execute();
  app = await harness.start({
    origin: 'http://ai.test',
    instance: {
      auth: { providers: [], local: { enabled: true } },
      limits: { authPerMinute: 1000, guestsPerHour: 1000 },
    },
  });
  owner = await registeredPlayer(app, 'AIAuthor');
  other = await registeredPlayer(app, 'AIFan');
  guest = await guestPlayer(app);
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  guest?.client.close();
  await harness?.close();
});
async function call(method: string, path: string, p?: Player, data?: unknown) {
  return fetch(app.url + path, {
    method,
    headers: {
      ...(p ? { authorization: 'Bearer ' + p.accessToken } : {}),
      ...(data === undefined
        ? {}
        : {
            'content-type':
              data instanceof Uint8Array ? 'application/octet-stream' : 'application/json',
          }),
    },
    ...(data === undefined
      ? {}
      : { body: data instanceof Uint8Array ? Buffer.from(data) : JSON.stringify(data) }),
  });
}
let next = 0;
async function upload(valid = true) {
  const bytes = Buffer.from(`let n=${next++}; function step(){n++;}`),
    response = await call('POST', '/api/v1/ai-uploads', owner, bytes);
  expect(response.status, await response.clone().text()).toBe(201);
  const u = (await response.json()) as AiUpload;
  const row = await harness.database.db
    .selectFrom('ai_validations')
    .selectAll()
    .where('hash', '=', u.sourceHash)
    .executeTakeFirstOrThrow();
  const report = pendingAiReport(u.sourceHash, simVersionKey(SIM));
  report.checks.forEach((c) => (c.status = 'passed'));
  report.valid = valid;
  report.metadata = {
    apiVersion: 1,
    name: 'Colony Mind',
    description: 'A patient builder',
    version: '1.0',
    author: 'author',
  };
  if (!valid) report.checks[4]!.status = 'failed';
  await handleEngineJobResult(harness.database.db, {
    jobId: row.job_id!,
    kind: 'validate-ai',
    agent: 'fake',
    ok: true,
    result: report,
  });
  return { u, bytes };
}
const publication = (id: string, extra = {}) => ({
  uploadId: id,
  name: 'Colony Mind',
  description: 'A patient economic builder',
  tags: ['Economy'],
  visibility: 'public',
  version: '1.0',
  notes: 'First release',
  ...extra,
});
async function publish(extra = {}) {
  const { u, bytes } = await upload();
  const r = await call('POST', '/api/v1/ais', owner, publication(u.id, extra));
  expect(r.status, await r.clone().text()).toBe(200);
  const ai = (await r.json()) as AiInfo;
  expect(checkDocument('AiInfo', ai).stage).toBe('ok');
  return { ai, u, bytes };
}
describe('AI library', () => {
  it('rejects anonymous, oversized, invalid encoding and NUL uploads', async () => {
    expect((await call('POST', '/api/v1/ai-uploads', undefined, Buffer.from('x'))).status).toBe(
      401,
    );
    expect((await call('POST', '/api/v1/ai-uploads', owner, Buffer.from([255]))).status).toBe(400);
    expect((await call('POST', '/api/v1/ai-uploads', owner, Buffer.from([0]))).status).toBe(400);
    expect((await call('POST', '/api/v1/ai-uploads', owner, Buffer.alloc(131073, 65))).status).toBe(
      413,
    );
  });
  it('requires all checks and keeps staged files private', async () => {
    const { u } = await upload(false);
    expect((await call('POST', '/api/v1/ais', owner, publication(u.id))).status).toBe(409);
    expect((await call('GET', '/api/v1/ai-uploads/' + u.id, other)).status).toBe(404);
  });
  it('atomically publishes once even with concurrent retries', async () => {
    const { u } = await upload();
    const results = await Promise.all([
      call('POST', '/api/v1/ais', owner, publication(u.id)),
      call('POST', '/api/v1/ais', owner, publication(u.id)),
    ]);
    expect(results.map((r) => r.status)).toEqual([200, 200]);
    expect(((await results[0]!.json()) as AiInfo).id).toBe(
      ((await results[1]!.json()) as AiInfo).id,
    );
  });
  it('preserves exact bytes, immutable versions, AI-wide likes and private favourites', async () => {
    const { ai, bytes } = await publish();
    const file = '/api/v1/ais/' + ai.id + '/versions/' + ai.latestVersion.id + '/file';
    const downloaded = await call('GET', file, other);
    expect(Buffer.from(await downloaded.arrayBuffer())).toEqual(bytes);
    expect(downloaded.headers.get('content-disposition')).toContain('.js');
    await call('GET', file, other);
    await Promise.all([
      call('PUT', '/api/v1/ais/' + ai.id + '/like', other),
      call('PUT', '/api/v1/ais/' + ai.id + '/like', other),
    ]);
    await call('PUT', '/api/v1/ais/' + ai.id + '/favourite', other);
    const { u } = await upload();
    expect(
      (
        await call(
          'POST',
          '/api/v1/ais/' + ai.id + '/versions',
          owner,
          publication(u.id, { version: '1.1' }),
        )
      ).status,
    ).toBe(200);
    const detail = (await (await call('GET', '/api/v1/ais/' + ai.id, other)).json()) as AiDetail;
    expect(detail.ai.likes).toBe(1);
    expect(detail.ai.favourited).toBe(true);
    expect(detail.versions.map((v) => v.downloads)).toEqual([0, 1]);
    expect(detail.versions[1]?.hash).toBe(ai.latestVersion.hash);
    expect(
      ((await (await call('GET', '/api/v1/ais/' + ai.id)).json()) as AiDetail).ai.favourited,
    ).toBe(false);
    expect((await call('PATCH', file, owner, {})).status).toBe(404);
    const next = await upload();
    expect(
      (
        await call(
          'POST',
          '/api/v1/ais/' + ai.id + '/versions',
          owner,
          publication(next.u.id, { version: '1.1' }),
        )
      ).status,
    ).toBe(409);
    expect(
      (
        await call(
          'POST',
          '/api/v1/ais/' + ai.id + '/versions',
          other,
          publication(next.u.id, { version: '2' }),
        )
      ).status,
    ).toBe(404);
  });
  it('enforces visibility, owner edits, guest restrictions and moderation', async () => {
    const { ai } = await publish({ visibility: 'private' });
    expect((await call('GET', '/api/v1/ais/' + ai.id, other)).status).toBe(404);
    expect((await call('PATCH', '/api/v1/ais/' + ai.id, other, { name: 'Hijacked' })).status).toBe(
      404,
    );
    await call('PATCH', '/api/v1/ais/' + ai.id, owner, { visibility: 'unlisted' });
    expect((await call('GET', '/api/v1/ais/' + ai.id)).status).toBe(200);
    const list = (await (await call('GET', '/api/v1/ais')).json()) as AiList;
    expect(list.items.some((x: AiInfo) => x.id === ai.id)).toBe(false);
    expect((await call('PUT', '/api/v1/ais/' + ai.id + '/like', guest)).status).toBe(403);
    await harness.database.db
      .updateTable('accounts')
      .set({ role: 'moderator' })
      .where('id', '=', other.accountId)
      .execute();
    expect(
      (await call('POST', '/api/v1/admin/ais/' + ai.id + '/hide', other, { reason: 'Broken' }))
        .status,
    ).toBe(200);
    expect((await call('GET', '/api/v1/ais/' + ai.id)).status).toBe(404);
    expect((await call('GET', '/api/v1/ais/' + ai.id, owner)).status).toBe(200);
  });
  it('filters, sorts and paginates without leaking favourites', async () => {
    const first = (await (
      await call('GET', '/api/v1/ais?limit=1&sort=likes&tags=Economy&q=patient')
    ).json()) as AiList;
    expect(first.items).toHaveLength(1);
    expect(first.nextCursor).toBeDefined();
    const second = (await (
      await call(
        'GET',
        '/api/v1/ais?limit=1&sort=likes&tags=Economy&q=patient&cursor=' + first.nextCursor,
      )
    ).json()) as AiList;
    expect(second.items[0]!.id).not.toBe(first.items[0]!.id);
    expect((await call('GET', '/api/v1/ais?favourites=true')).status).toBe(401);
    expect((await call('GET', '/api/v1/ais?tags=Unknown')).status).toBe(400);
  });
  it('cleans abandoned staging while retaining published evidence', async () => {
    const { ai } = await publish();
    await harness.database.db
      .updateTable('ai_uploads')
      .set({ expires_at: sql<Date>`now()-interval '1 day'` })
      .execute();
    await maintainAiLibrary(harness.database.db);
    expect((await harness.database.db.selectFrom('ai_uploads').select('id').execute()).length).toBe(
      0,
    );
    expect((await call('GET', '/api/v1/ais/' + ai.id)).status).toBe(200);
  });
});

it('rejects substituted validation evidence and permits infrastructure retries', async () => {
  const { u } = await upload();
  const row = await harness.database.db
    .selectFrom('ai_validations')
    .selectAll()
    .where('hash', '=', u.sourceHash)
    .executeTakeFirstOrThrow();
  const substituted = { ...row.report, sourceHash: 'f'.repeat(64) };
  await harness.database.db
    .updateTable('ai_validations')
    .set({ report: JSON.stringify(substituted) })
    .where('id', '=', row.id)
    .execute();
  expect((await call('POST', '/api/v1/ais', owner, publication(u.id))).status).toBe(409);
  await harness.database.db
    .updateTable('ai_validations')
    .set({ status: 'error' })
    .where('id', '=', row.id)
    .execute();
  const { ensureAiValidation } = await import('@glob2/core');
  await harness.database.db
    .transaction()
    .execute((tx) => ensureAiValidation(tx, u.sourceHash, SIM, true));
  const retried = await harness.database.db
    .selectFrom('ai_validations')
    .selectAll()
    .where('id', '=', row.id)
    .executeTakeFirstOrThrow();
  expect(retried.status).toBe('pending');
  expect(retried.job_id).not.toBe(row.job_id);
  expect(retried.report.checks.every((c) => c.status === 'pending')).toBe(true);
});
it('rejects repeated source under another label and database mutation of published bytes', async () => {
  const { ai, bytes } = await publish();
  const staged = (await (
    await call('POST', '/api/v1/ai-uploads', owner, bytes)
  ).json()) as AiUpload;
  expect(
    (
      await call(
        'POST',
        '/api/v1/ais/' + ai.id + '/versions',
        owner,
        publication(staged.id, { version: '2' }),
      )
    ).status,
  ).toBe(409);
  await expect(
    harness.database.db
      .updateTable('ai_versions')
      .set({ label: 'replaced' })
      .where('id', '=', ai.latestVersion.id)
      .execute(),
  ).rejects.toThrow('immutable');
  const detail = (await (await call('GET', '/api/v1/ais/' + ai.id)).json()) as AiDetail;
  expect(detail.versions).toHaveLength(1);
  expect(detail.versions[0]!.label).toBe('1.0');
});

it('keeps bookmarked unlisted releases visible only to their authorised viewer', async () => {
  const { ai } = await publish({ visibility: 'unlisted' });
  await call('PUT', '/api/v1/ais/' + ai.id + '/favourite', owner);
  const list = (await (await call('GET', '/api/v1/ais?favourites=true', owner)).json()) as AiList;
  expect(list.items.some((x) => x.id === ai.id)).toBe(true);
  const publicList = (await (await call('GET', '/api/v1/ais')).json()) as AiList;
  expect(publicList.items.some((x) => x.id === ai.id)).toBe(false);
});
