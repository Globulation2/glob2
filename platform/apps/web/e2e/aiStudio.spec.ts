import { expect, test } from '@playwright/test';
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { pendingAiReport } from '@glob2/protocol';
const id = '11111111-1111-4111-8111-111111111111';
const initial =
  'export function metadata() { return { apiVersion: 2, name: "My Colony" }; }\nexport function step(ctx) {\n  const buildings = ctx.game.buildings({team: ctx.myTeam});\n  for (const building of buildings) building.workers = 2;\n}\n';
test('studio edits, restores, explicitly checks, watches a pinned revision and publishes', async ({
  page,
}, info) => {
  let revision = 1,
    source = initial,
    cursor = 0,
    checked = false,
    published = false,
    requests: Record<string, unknown>[] = [];
  const submissions: string[] = [];
  let acknowledgeSubmission!: () => void;
  const delayedSubmission = new Promise<void>((resolve) => {
    acknowledgeSubmission = resolve;
  });
  let enabled = true;
  let mapFetches = 0,
    detailReads = 0;
  const history = [
    {
      revision: 1,
      source: initial,
      hash: 'a'.repeat(64),
      reason: 'initial',
      created_at: new Date().toISOString(),
    },
  ];
  await page.route('**/api/v1/accounts/me', (r) =>
    r.fulfill({
      json: {
        id,
        displayName: 'Studio author',
        kind: 'registered',
        createdAt: new Date().toISOString(),
        role: 'user',
        status: 'active',
        identities: [],
        entitlements: [],
      },
    }),
  );
  await page.route('**/api/v1/ai-studio/**', async (r) => {
    const path = new URL(r.request().url()).pathname,
      method = r.request().method(),
      data = method === 'POST' || method === 'PATCH' ? r.request().postDataJSON() : {};
    if (path.endsWith('/account'))
      return r.fulfill({
        json: {
          enabled,
          model: 'Studio coding model',
          maxRequestCredits: 100,
          balance: 100,
          reserved: 0,
          available: 100,
          rate: { input: 10, cachedInput: 1, output: 20 },
          packs: [],
        },
      });
    if (path.endsWith('/events'))
      return r.fulfill({
        json: {
          events:
            Number(new URL(r.request().url()).searchParams.get('after')) < cursor
              ? [{ cursor: String(cursor) }]
              : [],
        },
      });
    if (path.endsWith('/checks')) {
      const report = pendingAiReport(
        history.at(-1)?.hash ?? 'a'.repeat(64),
        '133-54-' + 'b'.repeat(64),
      );
      report.valid = true;
      report.checks.forEach((c) => (c.status = 'passed'));
      return r.fulfill({
        json: {
          items: checked
            ? [
                {
                  report,
                  status: 'valid',
                  error: null,
                  upload_id: id,
                  expires_at: new Date(Date.now() + 86400000).toISOString(),
                },
              ]
            : [],
        },
      });
    }
    if (path.endsWith('/check')) {
      checked = true;
      return r.fulfill({ json: { uploadId: id, revision } });
    }
    if (path.endsWith('/requests')) {
      submissions.push(data.id);
      if (submissions.length === 1)
        return r.fulfill({ status: 503, json: { message: 'Submission response lost' } });
      if (submissions.length === 2) await delayedSubmission;
      source = source.replace('workers = 2', 'workers = 3');
      revision++;
      cursor++;
      history.push({
        revision,
        source,
        hash: 'b'.repeat(64),
        reason: 'assistant',
        created_at: new Date().toISOString(),
      });
      requests = [
        {
          id: data.id,
          base_revision: revision - 1,
          prompt: data.text,
          diagnostics: '',
          budget: 100,
          status: 'completed',
          response: 'Changed staffing to three workers.',
          error: null,
          charged: 1,
        },
        ...requests,
      ];
      return r.fulfill({ json: { accepted: true } });
    }
    if (path.endsWith('/runs'))
      return r.fulfill({
        json: {
          runId: data.id,
          revision: data.expectedRevision,
          source: history.find((x) => x.revision === data.expectedRevision)?.source ?? initial,
          seed: data.seed,
          opponent: data.opponent,
        },
      });
    if (path.endsWith('/test-map')) {
      mapFetches++;
      return r.fulfill({ body: Buffer.from('test-map') });
    }
    if (path.endsWith('/run-result')) return r.fulfill({ json: { saved: true } });
    if (path.endsWith('/revision'))
      return r.fulfill({
        json: history.find(
          (x) => x.revision === Number(new URL(r.request().url()).searchParams.get('revision')),
        ),
      });
    if (method === 'PATCH') {
      if (data.source === '')
        return r.fulfill({ status: 400, json: { message: 'Draft is temporarily empty' } });
      source = data.restoreRevision
        ? (history.find((x) => x.revision === data.restoreRevision)?.source ?? initial)
        : (data.source ?? source);
      revision++;
      cursor++;
      history.push({
        revision,
        source,
        hash: 'c'.repeat(64),
        reason: data.restoreRevision ? 'restore' : 'manual',
        created_at: new Date().toISOString(),
      });
      return r.fulfill({ json: { revision } });
    }
    detailReads++;
    return r.fulfill({
      json: {
        id,
        title: 'My Colony',
        revision,
        current: history.at(-1),
        revisions: [...history].reverse(),
        requests,
        cursor: String(cursor),
        runs: [],
      },
    });
  });
  await page.route('**/api/v1/ais', (r) => {
    published = true;
    return r.fulfill({ json: { id, name: 'My Colony' } });
  });
  await page.route('**/play/studio.html?*', (r) =>
    r.fulfill({
      contentType: 'text/html',
      headers: {
        'Cross-Origin-Embedder-Policy': 'require-corp',
        'Cross-Origin-Resource-Policy': 'same-origin',
      },
      body: `<html><body style="background:#213d29;color:white"><h2>Live colony test</h2><script>const q=new URLSearchParams(location.search),base={channel:'glob2-ai-studio',version:1,runId:q.get('run'),revision:Number(q.get('revision'))};addEventListener('message',e=>{if(e.data.type==='launch')parent.postMessage({...base,type:'progress',tick:100},location.origin);});parent.postMessage({...base,type:'ready'},location.origin);</script></body></html>`,
    }),
  );
  await page.goto('/ai-studio/' + id);
  await expect(page.getByRole('heading', { name: 'My Colony', exact: true })).toBeVisible();
  if (info.project.name === 'desktop')
    await expect(page.locator('.monaco-editor').first()).toBeVisible();
  else {
    await page
      .getByRole('tab', { name: /^Preview(?: ·.*)?$/ })
      .first()
      .click();
    await expect(
      page.getByRole('textbox', { name: 'AI JavaScript source', exact: true }),
    ).toBeVisible();
  }
  const projectName = page.getByRole('textbox', { name: 'Project', exact: true });
  await projectName.fill('A name still being typed');
  const readsBeforePoll = detailReads;
  cursor++;
  await expect.poll(() => detailReads).toBeGreaterThan(readsBeforePoll);
  await expect(projectName).toHaveValue('A name still being typed');
  await projectName.fill('My Colony');
  const sourceInput =
    info.project.name === 'desktop'
      ? page.locator('.monaco-editor textarea').first()
      : page.getByRole('textbox', { name: 'AI JavaScript source', exact: true });
  if (info.project.name === 'desktop')
    await page
      .locator('.monaco-editor .view-lines')
      .first()
      .click({ position: { x: 30, y: 10 } });
  else await sourceInput.focus();
  if (info.project.name === 'phone') await sourceInput.fill('');
  else {
    await page.keyboard.press('Control+A');
    await page.keyboard.press('Backspace');
  }
  await expect(page.getByText('Draft is temporarily empty', { exact: false })).toBeVisible();
  if (info.project.name === 'phone') await expect(sourceInput).toBeEditable();
  if (info.project.name === 'desktop')
    await page
      .locator('.monaco-editor .view-lines')
      .first()
      .click({ position: { x: 30, y: 10 } });
  else await sourceInput.focus();
  if (info.project.name === 'phone') await sourceInput.fill(initial);
  else await page.keyboard.insertText(initial);
  if (info.project.name === 'phone')
    await page.getByRole('tab', { name: 'Chat', exact: true }).click();
  await page
    .getByRole('textbox', { name: 'Describe a change or ask a question' })
    .fill('Give every building three workers.');
  await page.getByRole('button', { name: 'Send', exact: true }).click();
  await expect(page.getByText('Submission response lost')).toBeVisible();
  await page.getByRole('button', { name: 'Send', exact: true }).click();
  await expect.poll(() => submissions.length).toBe(2);
  if (info.project.name === 'phone')
    await page.getByRole('tab', { name: 'Chat', exact: true }).click();
  const composer = page.getByRole('textbox', { name: 'Describe a change or ask a question' });
  await composer.fill('My next question');
  acknowledgeSubmission();
  await expect(page.getByText('Changed staffing to three workers.')).toBeVisible();
  await expect(composer).toHaveValue('My next question');
  await composer.fill('');
  expect(submissions).toHaveLength(2);
  expect(submissions[0]).toBe(submissions[1]);
  if (info.project.name === 'phone')
    await expect(
      page.getByRole('tab', { name: 'Preview · Ready', exact: true }).first(),
    ).toBeVisible();
  if (info.project.name === 'phone')
    await page
      .getByRole('tab', { name: /^Preview(?: ·.*)?$/ })
      .first()
      .click();
  await expect(page.getByRole('button', { name: 'Publish', exact: true })).toBeDisabled();
  await page.getByRole('tab', { name: 'Changes', exact: true }).first().click();
  if (info.project.name === 'phone')
    await page
      .getByRole('tab', { name: /^Preview(?: ·.*)?$/ })
      .first()
      .click();
  await page.getByRole('button', { name: 'Run checks', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Publish', exact: true })).toBeEnabled();
  const testedRevision = revision;
  await page.getByRole('tab', { name: 'Playtest', exact: true }).first().first().click();
  await expect(page.getByText(`Revision ${testedRevision} · tick 100 · live`)).toBeVisible();
  if (info.project.name === 'desktop') {
    // Inspecting a diff and launching a match must preserve the assistant edit's undo step.
    await page.getByRole('tab', { name: 'Code', exact: true }).first().click();
    const lines = page.locator('.monaco-editor .view-lines').first();
    await lines.click({ position: { x: 30, y: 10 } });
    await page.keyboard.press('Control+Z');
    await expect(lines).toContainText(/workers\s*=\s*2/);
    await page.keyboard.press('Control+Shift+Z');
    await expect(lines).toContainText(/workers\s*=\s*3/);
    await page.getByRole('tab', { name: 'Playtest', exact: true }).first().last().click();
  }
  const previousFetches = mapFetches;
  await page.locator('iframe').evaluate((el) => {
    const frame = el as unknown as { src: string };
    frame.src += '&threads=serial';
  });
  await expect.poll(() => mapFetches).toBeGreaterThan(previousFetches);
  await expect(page.getByText(`Revision ${testedRevision} · tick 100 · live`)).toBeVisible();
  const firstRun = await page.locator('iframe').getAttribute('src');
  await page.getByRole('button', { name: 'Restart same setup', exact: true }).click();
  await expect(page.getByText(`Revision ${testedRevision} · tick 100 · live`)).toBeVisible();
  await expect.poll(() => page.locator('iframe').getAttribute('src')).not.toBe(firstRun);
  await page
    .frameLocator('iframe')
    .locator('body')
    .evaluate(() => {
      const browser = globalThis as unknown as {
        location: { search: string; origin: string };
        parent: { postMessage(message: unknown, origin: string): void };
      };
      const query = new URLSearchParams(browser.location.search);
      browser.parent.postMessage(
        {
          channel: 'glob2-ai-studio',
          version: 1,
          type: 'complete',
          runId: query.get('run'),
          revision: Number(query.get('revision')),
          result: 'controller disabled',
          diagnostic: 'Test controller error',
        },
        browser.location.origin,
      );
    });
  await expect(page.getByText('Finished: controller disabled')).toBeVisible();
  await expect(page.getByText('Test diagnostics attached.', { exact: false })).toHaveCount(0);
  await page.getByRole('button', { name: 'Fix this', exact: true }).first().click();
  await expect(
    page.getByRole('textbox', { name: 'Describe a change or ask a question' }),
  ).toHaveValue('Please fix the issues in the attached test diagnostics.');
  expect(submissions).toHaveLength(2);
  await page.getByRole('button', { name: 'Send', exact: true }).click();
  await expect.poll(() => submissions.length).toBe(3);
  if (info.project.name === 'phone')
    await page
      .getByRole('tab', { name: /^Preview(?: ·.*)?$/ })
      .first()
      .click();
  await page.getByRole('button', { name: 'Run checks', exact: true }).click();
  await page.getByRole('button', { name: 'Stop game', exact: true }).click();
  await expect(page.locator('iframe')).toHaveCount(0);
  await page.getByRole('tab', { name: 'Code', exact: true }).first().click();
  await page.getByRole('button', { name: 'Publish', exact: true }).click();
  await page.getByRole('dialog').getByRole('button', { name: 'Publish', exact: true }).click();
  await expect.poll(() => published).toBe(true);
  const restoredRevision = revision + 1;
  page.once('dialog', (d) => d.accept());
  await page.getByRole('button', { name: 'Undo revision', exact: true }).click();
  await expect(page.getByText(`Saved · revision ${restoredRevision}`).first()).toBeVisible();
  // Detail snapshots use the API's descending request order. An uncertain newest
  // request must not expose the older completed result as ready or send another call.
  requests.unshift({
    id: 'uncertain-latest',
    base_revision: revision,
    prompt: 'A pending provider request',
    diagnostics: '',
    budget: 100,
    status: 'uncertain',
    response: '',
    error: 'Provider delivery outcome is unknown.',
    charged: null,
  });
  const stoppedRequests: string[] = [];
  page.on('request', (request) => {
    if (new URL(request.url()).pathname.endsWith('/stop')) stoppedRequests.push(request.url());
  });
  await page.reload();
  await expect(
    page.getByText(/Cancellation is unavailable while the provider outcome/),
  ).toBeVisible();
  const uncertainPrompt = page.getByRole('textbox', {
    name: 'Describe a change or ask a question',
  });
  await uncertainPrompt.fill('A second paid request must wait.');
  await uncertainPrompt.press('Enter');
  const unavailableSend = page.getByRole('button', { name: 'Send', exact: true });
  await expect(unavailableSend).toHaveAttribute('aria-disabled', 'true');
  await unavailableSend.focus();
  await page.keyboard.press('Enter');
  await expect(page.getByRole('button', { name: 'Stop request' })).toHaveAttribute(
    'aria-disabled',
    'true',
  );
  await page.getByRole('button', { name: 'Stop request' }).focus();
  await page.keyboard.press('Enter');
  expect(submissions).toHaveLength(3);
  expect(stoppedRequests).toEqual([]);
  if (info.project.name === 'phone')
    await page
      .getByRole('tab', { name: /Preview/ })
      .first()
      .click();
  await expect(page.getByRole('button', { name: 'Download', exact: true })).toBeEnabled();
  if (info.project.name === 'phone')
    await expect(page.getByRole('textbox', { name: 'AI JavaScript source' })).toBeEditable();
  requests.shift();
  enabled = false;
  await page.reload();
  await expect(
    page.getByText(
      'Generation is unavailable. Saved code, export and local tools remain accessible.',
    ),
  ).toBeVisible();
  if (info.project.name === 'phone')
    await page
      .getByRole('tab', { name: /^Preview(?: ·.*)?$/ })
      .first()
      .click();
  await expect(page.getByText(`Saved · revision ${restoredRevision}`).first()).toBeVisible();
  if (info.project.name === 'desktop')
    await expect(page.locator('.monaco-editor').first()).toBeVisible();
  if (process.env['SCREENSHOT_DIR']) {
    mkdirSync(process.env['SCREENSHOT_DIR'], { recursive: true });
    await page.screenshot({
      path: resolve(process.env['SCREENSHOT_DIR'], `ai-studio-${info.project.name}.png`),
      fullPage: true,
    });
  }
});
