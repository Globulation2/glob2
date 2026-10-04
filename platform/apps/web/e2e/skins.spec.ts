import { expect, test } from '@playwright/test';
import type { SeededHistory } from '../../api/test/historySeed.ts';

test('account drafts restore across devices and refuse stale saves', async ({
  browser,
  page,
  request,
  baseURL,
}) => {
  if (!baseURL) throw new Error('A test server URL is required.');
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  const cookie = { name: 'glob2_session', value: seed.userSession, url: baseURL };
  await page.context().addCookies([cookie]);
  await page.goto('/skins');
  await page.getByRole('button', { name: 'Restore from account' }).click();
  await expect(page.getByRole('status', { name: 'Designer status' })).toContainText(
    /No saved draft|Account draft restored/,
  );
  await page.getByLabel('Skin name').fill('Across devices');
  await page.getByRole('button', { name: 'Try stripes' }).click();
  const paint = await page
    .getByLabel('Paint texture')
    .evaluate((c: { toDataURL(): string }) => c.toDataURL());
  await page.getByRole('button', { name: 'Save to account' }).click();
  await expect(page.getByRole('status', { name: 'Designer status' })).toContainText(
    'Draft saved to your account.',
  );
  const second = await browser.newContext({ baseURL });
  try {
    await second.addCookies([cookie]);
    const device = await second.newPage();
    await device.goto('/skins');
    await device.getByRole('button', { name: 'Restore from account' }).click();
    await expect(device.getByRole('status', { name: 'Designer status' })).toContainText(
      'Account draft restored.',
    );
    await expect(device.getByLabel('Skin name')).toHaveValue('Across devices');
    expect(
      await device
        .getByLabel('Paint texture')
        .evaluate((c: { toDataURL(): string }) => c.toDataURL()),
    ).toBe(paint);
    await device.getByLabel('Skin name').fill('Newer draft');
    await device.getByRole('button', { name: 'Save to account' }).click();
    await expect(device.getByRole('status', { name: 'Designer status' })).toContainText(
      'Draft saved to your account.',
    );
    await page.getByRole('button', { name: 'Save to account' }).click();
    await expect(page.getByRole('status', { name: 'Designer status' })).toContainText(
      'Your account draft changed.',
    );
    await page.getByRole('button', { name: 'Save on this device' }).click();
    await expect(page.getByRole('status', { name: 'Designer status' })).toContainText(
      'Draft saved on this device.',
    );
    await page.getByRole('button', { name: 'Restore from account' }).click();
    await expect(page.getByLabel('Skin name')).toHaveValue('Newer draft');
    await page.getByRole('button', { name: 'Restore from this device' }).click();
    await expect(page.getByLabel('Skin name')).toHaveValue('Across devices');
    await page.screenshot({ path: test.info().outputPath('account-draft.png'), fullPage: true });
  } finally {
    await second.close();
  }
});

test('reopens published paint, resumes an edit and publishes immutable versions', async ({
  page,
  request,
  baseURL,
}) => {
  if (!baseURL) throw new Error('A test server URL is required.');
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  await page
    .context()
    .addCookies([{ name: 'glob2_session', value: seed.userSession, url: baseURL }]);
  await page.goto('/skins');
  await page.getByRole('button', { name: 'Restore from account' }).click();
  await expect(page.getByRole('status', { name: 'Designer status' })).toContainText(
    /No saved draft|Account draft restored/,
  );
  await page.getByRole('button', { name: 'Use as a starting point' }).first().click();
  const name = `Browser design ${test.info().project.name}`;
  await page.getByLabel('Skin name').fill(name);
  const swarmModel = page.waitForResponse((r) => r.url().endsWith('/skins/models/swarm-skep.gsk'));
  await page.getByRole('radio', { name: /^Skep/ }).check();
  expect((await swarmModel).status()).toBe(200);
  await expect(page.getByRole('tab', { name: 'Swarm' })).toHaveAttribute('aria-selected', 'true');
  const firstResponse = page.waitForResponse(
    (r) => r.url().endsWith('/skins/publish') && r.request().method() === 'POST',
  );
  await page.getByRole('button', { name: 'Publish skin', exact: true }).click();
  const first = await firstResponse;
  expect(first.status()).toBe(200);
  const original = (await first.json()) as {
    id: string;
    skinId: string;
    textureSha256: string;
    swarmMesh: string;
  };
  expect(original.swarmMesh).toBe('skep');
  const originalPaint = await (
    await page.request.get(`/api/v1/skins/versions/${original.id}/texture`)
  ).body();
  const card = page.locator(`[data-version-id="${original.id}"]`);
  await card.getByRole('button', { name: 'Equip', exact: true }).click();
  await expect(card.getByRole('button', { name: 'Equipped', exact: true })).toBeVisible();
  await expect(card).toContainText('skep swarm');
  await page.getByRole('radio', { name: /^Classic/ }).check();
  await card.getByRole('button', { name: 'Edit this version' }).click();
  await expect(page.getByLabel('Skin name')).toHaveValue(name);
  await expect(page.getByRole('radio', { name: /^Skep/ })).toBeChecked();
  await page.getByLabel('Paint', { exact: true }).fill('#aabbcc');
  await page.getByRole('button', { name: 'Fill', exact: true }).click();
  await page.getByLabel('Skin name').fill(`${name} revised`);
  await page.getByRole('button', { name: 'Save to account' }).click();
  await expect(page.getByRole('status', { name: 'Designer status' })).toContainText(
    'Draft saved to your account.',
  );
  await page.reload();
  await page.getByRole('button', { name: 'Restore from account' }).click();
  await expect(page.getByLabel('Skin name')).toHaveValue(`${name} revised`);
  const nextResponse = page.waitForResponse(
    (r) => r.url().endsWith('/skins/publish') && r.request().method() === 'POST',
  );
  await page.getByRole('button', { name: 'Publish new version', exact: true }).click();
  const next = await nextResponse;
  expect(next.status()).toBe(200);
  const revised = (await next.json()) as { id: string; skinId: string; textureSha256: string };
  expect(revised.id).not.toBe(original.id);
  expect(revised.skinId).toBe(original.skinId);
  expect(revised.textureSha256).not.toBe(original.textureSha256);
  expect(
    await (await page.request.get(`/api/v1/skins/versions/${original.id}/texture`)).body(),
  ).toEqual(originalPaint);
  await expect(
    page
      .locator(`[data-version-id="${original.id}"]`)
      .getByRole('button', { name: 'Equipped', exact: true }),
  ).toBeVisible();
  await expect(
    page
      .locator(`[data-version-id="${revised.id}"]`)
      .getByRole('button', { name: 'Equip', exact: true }),
  ).toBeVisible();
  await page.getByRole('button', { name: 'Make a separate design' }).click();
  const copyResponse = page.waitForResponse(
    (r) => r.url().endsWith('/skins/publish') && r.request().method() === 'POST',
  );
  await page.getByRole('button', { name: 'Publish skin', exact: true }).click();
  const copy = await copyResponse;
  expect(copy.status()).toBe(200);
  expect(((await copy.json()) as { skinId: string }).skinId).not.toBe(original.skinId);
  await page.screenshot({ path: test.info().outputPath('published-edit.png'), fullPage: true });
});

test('publishes each model with its own paintable material map', async ({
  page,
  request,
  baseURL,
}) => {
  if (!baseURL) throw new Error('A test server URL is required.');
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  await page
    .context()
    .addCookies([{ name: 'glob2_session', value: seed.userSession, url: baseURL }]);
  await page.goto('/skins');
  await page.getByRole('button', { name: 'Use as a starting point' }).first().click();
  await page.getByLabel('Skin name').fill(`Materials ${test.info().project.name}`);
  const publish = async (button: string) => {
    const response = page.waitForResponse(
      (r) => r.url().endsWith('/skins/publish') && r.request().method() === 'POST',
    );
    await page.getByRole('button', { name: button, exact: true }).click();
    const result = await response;
    expect(result.status()).toBe(200);
    return (await result.json()) as { id: string; textureSha256: string; materialSha256: string };
  };
  const plain = await publish('Publish skin');
  await page.getByRole('tab', { name: 'Warrior' }).click();
  await page.getByLabel('Material', { exact: true }).check();
  await page.getByLabel('Matte').check();
  await page.getByRole('button', { name: 'Fill', exact: true }).click();
  const matte = await publish('Publish new version');
  // Only the warrior's material changed: same colour atlas, new material map.
  expect(matte.textureSha256).toBe(plain.textureSha256);
  expect(matte.materialSha256).not.toBe(plain.materialSha256);
  const map = await page.request.get(`/api/v1/skins/versions/${matte.id}/material`);
  expect(map.status()).toBe(200);
  expect(map.headers()['content-type']).toContain('image/png');
  await page.screenshot({ path: test.info().outputPath('skin-materials.png'), fullPage: true });
});

test('reports match paint and moderates it without rewriting the original', async ({
  page,
  browser,
  request,
  baseURL,
}, info) => {
  if (!baseURL) throw new Error('A test server URL is required.');
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  await page.context().addCookies([
    {
      name: 'glob2_session',
      value: info.project.name === 'phone' ? seed.adminSession : seed.userSession,
      url: baseURL,
    },
  ]);
  const original = (await (
    await request.get(`/api/v1/matches/${seed.featuredMatch}/skins`)
  ).json()) as { colonySkins: Array<{ version: { id: string } }> };
  const version = original.colonySkins[0]?.version;
  if (!version) throw new Error('Expected a frozen fixture appearance');
  const texture = `/api/v1/skins/versions/${version.id}/texture`;
  const originalPaint = await (await request.get(texture)).body();
  await page.goto(`/matches/${seed.featuredMatch}`);
  const look = page.getByRole('region', { name: 'Match colony skins' });
  await look.getByRole('button', { name: 'Report skin', exact: true }).click();
  const reason = `Browser moderation ${info.project.name}`;
  await look.getByLabel('Report reason').fill(reason);
  await look.getByRole('button', { name: 'Send skin report' }).click();
  await expect(look.getByRole('status')).toContainText('Report received.');
  const context = await browser.newContext({
    baseURL,
    viewport:
      info.project.name === 'phone' ? { width: 393, height: 851 } : { width: 1280, height: 860 },
  });
  try {
    await context.addCookies([{ name: 'glob2_session', value: seed.adminSession, url: baseURL }]);
    const admin = await context.newPage();
    await admin.goto('/admin/skins');
    const report = admin.getByRole('article').filter({ hasText: reason });
    await report.getByLabel('Moderation reason').fill('Removed after review');
    await report.getByRole('button', { name: 'Disable skin and resolve' }).click();
    await expect(report).toHaveCount(0);
    expect((await request.get(texture)).status()).toBe(404);
    expect(
      (await (await request.get(`/api/v1/matches/${seed.featuredMatch}/skins`)).json()).colonySkins,
    ).toEqual([]);
    await admin.getByLabel('Skin report status').selectOption('closed');
    await expect(report).toContainText('Skin disabled');
    await report.getByText('View reported paint').click();
    await expect(report.getByRole('img')).toBeVisible();
    await admin.screenshot({ path: info.outputPath('skin-moderation.png'), fullPage: true });
    await report.getByLabel('Moderation reason').fill('Restored after review');
    await report.getByRole('button', { name: 'Restore skin' }).click();
    await expect(report).not.toContainText('Skin disabled');
    expect(await (await request.get(texture)).body()).toEqual(originalPaint);
  } finally {
    await context.close();
  }
});

test('inspects every action and paints a paused model with undo and erase', async ({ page }) => {
  const requests: string[] = [];
  page.on('request', (r) => {
    if (r.url().includes('/skins/models/')) requests.push(new URL(r.url()).pathname);
  });
  await page.goto('/skins');
  await expect(page.getByText('Glob paint repeats automatically', { exact: false })).toBeVisible();
  const preview = page.getByLabel(/^Paint directly on the 3D \w+ model$/);
  const texture = page.getByLabel('Paint texture');
  const image = () => texture.evaluate((c: { toDataURL(): string }) => c.toDataURL());
  const setFrame = async (direction: number, phase: number) => {
    await page.getByLabel('Animate', { exact: true }).uncheck();
    for (const [label, value] of [
      ['Direction', direction],
      ['Frame', phase],
    ] as const) {
      await page.getByLabel(label, { exact: true }).fill(String(value));
    }
    await expect(preview).toHaveAttribute('data-frame', String(direction * 32 + phase));
    await expect(page.getByLabel('Animate', { exact: true })).not.toBeChecked();
  };
  await setFrame(0, 0);
  const downloads = requests.length;
  await setFrame(7, 31);
  await setFrame(0, 0);
  expect(requests.length).toBe(downloads);
  const original = await image();
  await texture.click({ position: { x: 128, y: 32 } });
  await expect.poll(image).not.toBe(original);
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  expect(await image()).toBe(original);
  const box = await preview.boundingBox();
  if (!box) throw new Error('Preview is not visible');
  const position = { x: box.width / 2, y: box.height / 2 };
  await preview.click({ position });
  await expect.poll(image).not.toBe(original);
  const painted = await image();
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  expect(await image()).toBe(original);
  await page.getByRole('button', { name: 'Redo', exact: true }).click();
  expect(await image()).toBe(painted);
  await page.getByLabel('Brush size', { exact: true }).fill('16');
  await page.getByLabel('Erase to white', { exact: true }).check();
  await preview.click({ position });
  await expect.poll(image).toBe(original);
  await page.getByLabel('Erase to white', { exact: true }).uncheck();
  await page.getByRole('button', { name: 'Try stripes' }).click();
  for (const [model, tab, actions] of [
    ['worker', 'Worker', ['walk', 'swim', 'harvest']],
    ['warrior', 'Warrior', ['walk', 'swim', 'fight']],
    ['explorer', 'Explorer', ['fly']],
  ] as const) {
    await page.getByRole('tab', { name: tab }).click();
    for (const action of actions) {
      await page.getByLabel('Action', { exact: true }).selectOption(action);
      await expect.poll(() => requests.includes(`/skins/models/${model}-${action}.gsk`)).toBe(true);
      await setFrame(4, 15);
      await expect(preview).toHaveAttribute('data-model', `${model}-${action}`);
      await preview.screenshot({ path: test.info().outputPath(`${model}-${action}-frame.png`) });
    }
  }
  await page.getByRole('tab', { name: 'Swarm' }).click();
  await expect(page.getByLabel('Action', { exact: true })).toHaveCount(0);
  await expect(page.getByLabel('Frame', { exact: true })).toBeDisabled();
  await expect(page.getByLabel('Direction', { exact: true })).toBeDisabled();
  await expect(preview).toHaveAttribute('data-frame', '0');
});
