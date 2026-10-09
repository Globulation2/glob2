import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test } from '@playwright/test';
import type { SeededHistory } from '../../api/test/historySeed.ts';
import { decodeGeneratorDraft, generatorPackage, type AiStudioDetail } from '@glob2/protocol';
test('authors both files, recovers invalid JSON, freezes previews and exports on desktop and phone', async ({
  page,
  request,
}, info) => {
  test.setTimeout(480000);
  page.setDefaultTimeout(15000);
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  await page.context().addCookies([
    {
      name: 'glob2_session',
      value: seed.userSession,
      url: `http://127.0.0.1:${process.env['PORT'] ?? 4280}`,
    },
  ]);
  // Enable free manual authoring in this test; no paid provider request is dispatched.
  await page.route('**/api/v1/generator-studio/account', async (route) => {
    const response = await route.fetch();
    await route.fulfill({ json: { ...(await response.json()), enabled: true } });
  });
  await page.goto('/generator-studio');
  await page.getByText('Settings & references', { exact: true }).click();
  await page.getByRole('button', { name: 'Use working starter' }).click();
  await expect(page).toHaveURL(/\/generator-studio\/[0-9a-f-]+$/);
  const projectId = page.url().split('/').at(-1);
  const url = `/api/v1/generator-studio/projects/${projectId}`;
  if (info.project.name === 'phone')
    await page.getByRole('tab', { name: 'Preview', exact: true }).first().click();
  await expect(page.getByLabel('Project', { exact: true })).toBeVisible();
  const workspace = page.locator('.studio-artifact');
  await workspace.getByRole('combobox', { name: 'File', exact: true }).selectOption('manifest');
  await expect(
    workspace.getByRole('textbox', { name: 'Generator manifest JSON', exact: true }),
  ).toBeAttached();
  const editor =
    info.project.name === 'phone'
      ? page.getByLabel('Generator manifest JSON')
      : workspace.locator('.as-editor textarea');
  if (info.project.name === 'phone') await editor.focus();
  else
    await workspace
      .locator('.monaco-editor .view-lines')
      .first()
      .click({ position: { x: 30, y: 10 } });
  await page.keyboard.press('Control+A');
  await page.keyboard.press('Backspace');
  await page.keyboard.insertText('{');
  // Monaco pairs the opening brace; leave a deliberately incomplete manifest.
  if (info.project.name !== 'phone') await page.keyboard.press('Delete');
  await expect(page.getByRole('status').filter({ hasText: 'Manifest JSON:' })).toBeVisible();
  await expect
    .poll(
      async () =>
        decodeGeneratorDraft(
          ((await (await page.request.get(url)).json()) as AiStudioDetail).current.source,
        ).manifest,
    )
    .toBe('{');
  page.once('dialog', (d) => d.accept());
  await workspace.getByRole('button', { name: 'Undo revision', exact: true }).click();
  await expect(page.getByRole('status').filter({ hasText: 'Manifest JSON:' })).toHaveCount(0);
  if (info.project.name === 'phone')
    await page.getByRole('tab', { name: 'Preview', exact: true }).first().click();
  const detail = (await (await page.request.get(url)).json()) as AiStudioDetail;
  expect(JSON.parse(decodeGeneratorDraft(detail.current.source).manifest).entry).toBe(
    'generator.js',
  );
  await workspace.getByRole('tab', { name: 'Preview', exact: true }).click();
  await expect(page.getByRole('combobox', { name: 'Width', exact: true })).toHaveValue('7');
  await workspace.getByRole('button', { name: 'Generate', exact: true }).last().click();
  const watch = workspace.getByRole('button', { name: 'Watch AI play', exact: true });
  await expect(watch).toBeEnabled({ timeout: 150000 });
  const first = page.locator('iframe[title^="Generator preview"]');
  const firstSrc = await first.getAttribute('src');
  await page.getByLabel('Seed', { exact: true }).fill('20');
  expect(await first.getAttribute('src')).toBe(firstSrc);
  await workspace.getByRole('button', { name: 'Generate', exact: true }).last().click();
  await expect(watch).toBeEnabled({ timeout: 150000 });
  await expect(first).not.toHaveAttribute('src', firstSrc ?? '');
  await watch.click();
  await expect(page.getByRole('status').filter({ hasText: /tick \d+ · live/ })).toBeVisible({
    timeout: 60000,
  });
  if (process.env['GENERATOR_E2E_BINARY']) {
    await workspace.getByRole('tab', { name: 'Code', exact: true }).click();
    await workspace.getByRole('combobox', { name: 'File', exact: true }).selectOption('script');
    await expect(
      workspace.getByRole('textbox', { name: 'Generator JavaScript source', exact: true }),
    ).toBeAttached();
    const scriptEditor =
      info.project.name === 'phone'
        ? page.getByLabel('Generator JavaScript source')
        : workspace.locator('.as-editor textarea');
    const broken = 'export function generate(c){throw new Error("Studio repair example");}';
    if (info.project.name === 'phone') await scriptEditor.focus();
    else
      await workspace
        .locator('.monaco-editor .view-lines')
        .first()
        .click({ position: { x: 30, y: 10 } });
    await page.keyboard.press('Control+A');
    await page.keyboard.press('Backspace');
    await page.keyboard.insertText(broken);
    await expect
      .poll(
        async () =>
          decodeGeneratorDraft(
            ((await (await page.request.get(url)).json()) as AiStudioDetail).current.source,
          ).script,
      )
      .toBe(broken);
    await workspace.getByRole('button', { name: 'Run checks', exact: true }).click();
    await expect(workspace.locator('.as-checks summary')).toContainText('invalid', {
      timeout: 180000,
    });
    await workspace.getByRole('button', { name: 'Send diagnostics to chat', exact: true }).click();
    if (info.project.name === 'phone')
      await page.getByRole('tab', { name: 'Preview', exact: true }).first().click();
    page.once('dialog', (d) => d.accept());
    await workspace.getByRole('button', { name: 'Undo revision', exact: true }).click();
    await expect
      .poll(
        async () =>
          decodeGeneratorDraft(
            ((await (await page.request.get(url)).json()) as AiStudioDetail).current.source,
          ).script,
      )
      .toBe(decodeGeneratorDraft(detail.current.source).script);
    if (info.project.name === 'phone')
      await page.getByRole('tab', { name: 'Preview', exact: true }).first().click();
    await workspace.getByRole('button', { name: 'Run checks', exact: true }).click();
    await expect(workspace.locator('.as-checks summary')).toHaveText(/checks: valid$/, {
      timeout: 180000,
    });
    await expect(workspace.getByRole('button', { name: 'Publish', exact: true })).toBeEnabled();
    await workspace.getByRole('button', { name: 'Publish', exact: true }).click();
    const dialog = page.getByRole('dialog');
    await expect(dialog).toBeVisible();
    await dialog.getByRole('button', { name: 'Publish', exact: true }).click();
    await expect(page.locator('.notice').filter({ hasText: 'Published' })).toBeVisible({
      timeout: 30000,
    });
  }
  const download = page.waitForEvent('download');
  await workspace.getByRole('button', { name: 'Download', exact: true }).click();
  const exported = await download;
  expect(exported.suggestedFilename()).toBe('generator.json');
  const directory = resolve('../../../artifacts/generator-studio/ui-' + info.project.name);
  mkdirSync(directory, { recursive: true });
  await exported.saveAs(resolve(directory, 'export.json'));
  expect(JSON.parse(generatorPackage(detail.current.source)).modules['generator.js']).toBeTruthy();
  await page.screenshot({ path: resolve(directory, 'workspace.png') });
  const stop = workspace.getByRole('button', { name: 'Stop preview', exact: true });
  if (await stop.isVisible()) await stop.click();
  await expect(page.locator('iframe[title^="Generator preview"]')).toHaveCount(0);
});
