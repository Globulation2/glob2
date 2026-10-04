import { writeFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import type { SeededHistory } from '../../api/test/historySeed.ts';
test.use({ video: 'on' });

async function saveDialog(page: Page) {
  await page.getByRole('button', { name: 'Save', exact: true }).click();
  return page.getByRole('dialog', { name: 'Save your skin' });
}
async function close(page: Page) {
  await page
    .getByRole('dialog')
    .getByRole('button', { name: /^Close / })
    .click();
}
async function saved(page: Page) {
  const dialog = await saveDialog(page);
  await dialog.getByRole('button', { name: 'Save on this device' }).click();
  await close(page);
  return page.evaluate(() => {
    const key = Object.keys(localStorage).find(
      (k) => k.startsWith('glob2-skin-draft-v2:') && !k.endsWith(':recovery'),
    );
    return key
      ? (JSON.parse(localStorage.getItem(key) ?? 'null') as {
          image: string;
          material: string;
          swarmViewAngle: number;
        })
      : null;
  });
}
async function fill(page: Page, name = 'Solid') {
  await page.getByRole('button', { name: 'Patterns', exact: true }).click();
  const dialog = page.getByRole('dialog', { name: 'Patterns & fills' });
  await dialog.getByRole('button', { name: 'Whole model', exact: true }).click();
  await dialog.getByRole('button', { name, exact: true }).click();
  await dialog.getByRole('button', { name: /^Apply to / }).click();
}
async function publish(page: Page) {
  const response = page.waitForResponse(
    (r) => r.url().endsWith('/skins/publish') && r.request().method() === 'POST',
  );
  await page.getByRole('button', { name: 'Publish', exact: true }).click();
  const result = await response;
  expect(result.status()).toBe(200);
  return (await result.json()) as {
    id: string;
    skinId: string;
    textureSha256: string;
    materialSha256: string;
    swarmViewAngle: number;
  };
}

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
  let dialog = await saveDialog(page);
  await dialog.getByRole('button', { name: 'Restore from account' }).click();
  await expect(dialog.getByRole('status')).toContainText(/No account draft|Account draft restored/);
  await close(page);
  await page.getByLabel('Skin name').fill('Across devices');
  await fill(page);
  const paint = await saved(page);
  await page.getByRole('button', { name: 'Shop', exact: true }).click();
  await close(page);
  expect(await saved(page)).toEqual(paint);
  dialog = await saveDialog(page);
  await dialog.getByRole('button', { name: 'Save to account' }).click();
  await expect(dialog.getByRole('status')).toContainText('Draft saved to your account.');
  const second = await browser.newContext({ baseURL });
  try {
    await second.addCookies([cookie]);
    const device = await second.newPage();
    await device.goto('/skins');
    const other = await saveDialog(device);
    await other.getByRole('button', { name: 'Restore from account' }).click();
    await expect(other.getByRole('status')).toContainText('Account draft restored.');
    await close(device);
    await expect(device.getByLabel('Skin name')).toHaveValue('Across devices');
    expect(await saved(device)).toEqual(paint);
    await device.getByLabel('Skin name').fill('Newer draft');
    await saveDialog(device);
    await other.getByRole('button', { name: 'Save to account' }).click();
    await expect(other.getByRole('status')).toContainText('Draft saved to your account.');
    await dialog.getByRole('button', { name: 'Save to account' }).click();
    await expect(dialog.getByRole('status')).toContainText('Your account draft changed.');
    await dialog.getByRole('button', { name: 'Save on this device' }).click();
    await dialog.getByRole('button', { name: 'Restore from account' }).click();
    await expect(page.getByLabel('Skin name')).toHaveValue('Newer draft');
    await page.waitForTimeout(900); // Recovery must not overwrite the manual checkpoint.
    await dialog.getByRole('button', { name: 'Restore from this device' }).click();
    await expect(page.getByLabel('Skin name')).toHaveValue('Across devices');
  } finally {
    await second.close();
  }
});

test.describe('paint-to-publish demonstration', () => {
  test('publishes immutable angled skins, reopens and equips them independently', async ({
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
    await page.getByLabel('Skin name').fill(`Studio ${test.info().project.name}`);
    const worker = page.getByLabel('Paint directly on the 3D worker model');
    await expect(worker).toHaveAttribute('data-frame', '0');
    const bounds = await worker.boundingBox();
    if (!bounds) throw new Error('Missing paint viewport');
    await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2 - 25);
    await page.mouse.down();
    await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2 + 25, {
      steps: 8,
    });
    await page.mouse.up();
    await page.getByRole('button', { name: 'Harvest', exact: true }).click();
    await page.getByLabel('Frame', { exact: true }).fill('15');
    await saved(page);
    await page.getByRole('button', { name: 'Swarm' }).click();
    await page.getByRole('button', { name: 'Choose final view' }).click();
    await page.getByLabel('Camera angle').fill('127');
    await page.getByRole('button', { name: 'Use this view' }).click();
    await fill(page, 'Mirrored bands');
    const original = await publish(page);
    expect(original.swarmViewAngle).toBe(127);
    const texture = await (
      await page.request.get(`/api/v1/skins/versions/${original.id}/texture`)
    ).body();
    await page.getByRole('button', { name: 'My skins', exact: true }).click();
    let card = page.locator(`[data-version-id="${original.id}"]`);
    await card.getByRole('button', { name: 'Equip', exact: true }).click();
    await expect(card.getByRole('button', { name: 'Equipped', exact: true })).toBeVisible();
    await card.getByRole('button', { name: 'Edit this version' }).click();
    await expect(page.getByText('Final game view · 127°', { exact: true })).toBeVisible();
    await page.getByRole('button', { name: 'Material', exact: true }).click();
    await page.getByRole('radio', { name: 'Matte', exact: true }).click();
    await fill(page);
    const revised = await publish(page);
    expect(revised.id).not.toBe(original.id);
    expect(revised.skinId).toBe(original.skinId);
    expect(revised.textureSha256).toBe(original.textureSha256);
    expect(revised.materialSha256).not.toBe(original.materialSha256);
    expect(
      await (await page.request.get(`/api/v1/skins/versions/${original.id}/texture`)).body(),
    ).toEqual(texture);
    await page.getByRole('button', { name: 'My skins', exact: true }).click();
    card = page.locator(`[data-version-id="${original.id}"]`);
    await expect(card.getByRole('button', { name: 'Equipped', exact: true })).toBeVisible();
    await close(page);
    await page.screenshot({ path: test.info().outputPath('studio-published.png') });
  });
});

test('direct strokes, orbit, animation, pattern cancel and undo preserve transactions', async ({
  page,
}) => {
  await page.goto('/skins');
  const preview = page.getByLabel('Paint directly on the 3D worker model');
  await expect(preview).toHaveAttribute('data-frame', '0');
  expect(await page.getByLabel('Paint texture').count()).toBe(0);
  const original = await saved(page);
  const box = await preview.boundingBox();
  if (!box) throw new Error('No model viewport');
  const x = box.x + box.width / 2,
    y = box.y + box.height / 2;
  await page.mouse.move(x, y - 25);
  await page.mouse.down();
  await page.mouse.move(x, y + 25, { steps: 8 });
  await page.mouse.up();
  const painted = await saved(page);
  expect(painted?.image).not.toBe(original?.image);
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  expect((await saved(page))?.image).toBe(original?.image);
  await page.getByRole('button', { name: 'Redo', exact: true }).click();
  expect((await saved(page))?.image).toBe(painted?.image);
  await page.getByRole('button', { name: 'Patterns', exact: true }).click();
  await page.getByRole('dialog').getByRole('button', { name: 'Waves', exact: true }).click();
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click();
  expect((await saved(page))?.image).toBe(painted?.image);
  await fill(page, 'Mirrored spots');
  const pattern = await saved(page);
  expect(pattern?.image).not.toBe(painted?.image);
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  expect((await saved(page))?.image).toBe(painted?.image);
  await page.getByRole('button', { name: 'Orbit', exact: true }).click();
  await page.mouse.move(x, y);
  await page.mouse.down();
  await page.mouse.move(x + 60, y + 40);
  await page.mouse.up();
  expect((await saved(page))?.image).toBe(painted?.image);
  await page.getByRole('button', { name: 'Brush', exact: true }).click();
  await page.getByRole('button', { name: 'Play animation' }).click();
  await page.waitForTimeout(150);
  await preview.click({ position: { x: box.width / 2, y: box.height / 2 } });
  await expect(page.getByRole('button', { name: 'Play animation' })).toBeVisible();
  for (const [model, actions] of [
    ['Worker', ['Walk', 'Swim', 'Harvest']],
    ['Warrior', ['Walk', 'Swim', 'Fight']],
    ['Explorer', ['Fly']],
  ] as const) {
    await page.getByRole('button', { name: model }).click();
    for (const action of actions) {
      await page.getByRole('button', { name: action, exact: true }).click();
      await page.getByLabel('Frame', { exact: true }).fill('15');
      await expect(
        page.getByLabel(`Paint directly on the 3D ${model.toLowerCase()} model`),
      ).toHaveAttribute('data-model', `${model.toLowerCase()}-${action.toLowerCase()}`);
    }
  }
  await page.screenshot({ path: test.info().outputPath('studio-paint.png') });
});

test('responsive studio keeps dialogs, tools and the document in the viewport', async ({
  page,
}) => {
  for (const [width, height] of [
    [320, 740],
    [768, 1024],
    [1024, 768],
    [1440, 900],
  ] as const) {
    await page.setViewportSize({ width, height });
    await page.goto('/skins');
    await expect(page.getByRole('button', { name: 'Swarm' })).toBeVisible();
    await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeVisible();
    expect(
      await page
        .locator('html')
        .evaluate((e) => [e.scrollWidth <= e.clientWidth, e.scrollHeight <= e.clientHeight]),
    ).toEqual([true, true]);
    await page.getByRole('button', { name: 'Patterns', exact: true }).click();
    const apply = page.getByRole('button', { name: 'Apply to Worker' });
    await expect(apply).toBeInViewport();
    await page.screenshot({ path: test.info().outputPath(`studio-pattern-${width}.png`) });
    await page.keyboard.press('Escape');
    await expect(page.getByRole('dialog')).toHaveCount(0);
    await expect(page.getByRole('button', { name: 'Patterns', exact: true })).toBeFocused();
  }
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

test('painting degrades safely without WebGL2', async ({ page }) => {
  await page.addInitScript({
    content: `
    const get = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function (...args) {
      return args[0] === 'webgl2' ? null : get.apply(this, args);
    };
  `,
  });
  await page.goto('/skins');
  await expect(page.getByRole('alert')).toContainText('Painting needs WebGL 2');
  await page.getByRole('button', { name: 'My skins', exact: true }).click();
  await expect(page.getByRole('dialog', { name: 'My skins' })).toBeVisible();
  await close(page);
  await page.getByRole('button', { name: 'Shop', exact: true }).click();
  await expect(page.getByRole('dialog')).toBeVisible();
  await close(page);
  await expect(page.getByRole('button', { name: 'Retry 3D canvas' })).toBeVisible();
  expect(await page.getByLabel('Paint texture').count()).toBe(0);
});

test('idle viewport stops drawing and records stroke feedback latency', async ({ page }, info) => {
  await page.addInitScript({
    content: `
    const state = { draws: 0, input: 0, samples: [] };
    window.skinMetrics = state;
    for (const type of ['pointerdown', 'pointermove']) window.addEventListener(type, e => {
      if (e.target instanceof HTMLCanvasElement) state.input = performance.now();
    }, true);
    const draw = WebGL2RenderingContext.prototype.drawElements;
    WebGL2RenderingContext.prototype.drawElements = function (...args) {
      draw.apply(this, args);
      state.draws++;
      if (state.input) { state.samples.push(performance.now() - state.input); state.input = 0; }
    };
  `,
  });
  await page.goto('/skins');
  const preview = page.getByLabel('Paint directly on the 3D worker model');
  await expect(preview).toHaveAttribute('data-frame', '0');
  await page.waitForTimeout(350);
  const metrics = () =>
    page.evaluate(
      () =>
        (globalThis as unknown as { skinMetrics: { draws: number; samples: number[] } })
          .skinMetrics,
    );
  const idle = (await metrics()).draws;
  await page.waitForTimeout(350);
  expect((await metrics()).draws).toBe(idle);
  const box = await preview.boundingBox();
  if (!box) throw new Error('No model viewport');
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 - 40);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 + 40, { steps: 20 });
  await page.mouse.up();
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeEnabled();
  const result = await metrics();
  const samples = result.samples.sort((a, b) => a - b);
  const measurement = {
    project: info.project.name,
    viewport: page.viewportSize(),
    userAgent: await page.evaluate(() => navigator.userAgent),
    medianMs: samples[Math.floor(samples.length / 2)],
    p95Ms: samples[Math.floor(samples.length * 0.95)],
    samples,
  };
  const orbit = async () => {
    await page.getByRole('button', { name: 'Orbit', exact: true }).click();
    const bounds = await preview.boundingBox();
    if (!bounds) throw new Error('Missing orbit viewport');
    await page.mouse.move(bounds.x + bounds.width / 2 - 40, bounds.y + bounds.height / 2);
    await page.mouse.down();
    const before = (await metrics()).draws,
      started = Date.now();
    await page.mouse.move(bounds.x + bounds.width / 2 + 40, bounds.y + bounds.height / 2 + 35, {
      steps: 60,
    });
    await page.mouse.up();
    const elapsedMs = Date.now() - started,
      frames = (await metrics()).draws - before;
    return {
      viewport: page.viewportSize(),
      frames,
      elapsedMs,
      inputDrivenFramesPerSecond: (frames * 1000) / elapsedMs,
    };
  };
  const navigation = [await orbit()];
  await page.setViewportSize({ width: 768, height: 1024 });
  await page.waitForTimeout(250);
  navigation.push(await orbit());
  const renderer = await preview.evaluate((canvas) => {
    const gl = (
      canvas as unknown as {
        getContext(type: string): {
          getExtension(name: string): { UNMASKED_RENDERER_WEBGL: number } | null;
          getParameter(key: number): string;
        };
      }
    ).getContext('webgl2');
    const extension = gl.getExtension('WEBGL_debug_renderer_info');
    return extension ? gl.getParameter(extension.UNMASKED_RENDERER_WEBGL) : 'unavailable';
  });
  const report = {
    ...measurement,
    navigation,
    renderer,
    note: 'Headless Chromium with emulated viewports; physical tablet/pen testing is separate.',
  };
  const path = info.outputPath('paint-feedback.json');
  writeFileSync(path, JSON.stringify(report, null, 2));
  await info.attach('paint-feedback.json', { contentType: 'application/json', path });
  await page.screenshot({ path: info.outputPath('studio-workspace.png') });
});

test('two-finger gestures cancel paint and palm touches do not end a pen stroke', async ({
  page,
}) => {
  // Synthetic multi-pointer events exercise the transaction state machine. Real
  // capture is covered by the mouse/drag tests; synthetic IDs cannot be captured.
  await page.addInitScript({
    content: 'HTMLCanvasElement.prototype.setPointerCapture = function () {};',
  });
  await page.goto('/skins');
  const preview = page.getByLabel('Paint directly on the 3D worker model');
  await expect(preview).toHaveAttribute('data-frame', '0');
  const box = await preview.boundingBox();
  if (!box) throw new Error('No model viewport');
  const at = {
    clientX: box.x + box.width / 2,
    clientY: box.y + box.height / 2,
    buttons: 1,
    pressure: 1,
    isPrimary: true,
  };
  const pointer = async (type: string, id: number, kind: string, offset = 0) =>
    preview.dispatchEvent(type, {
      ...at,
      clientY: at.clientY + offset,
      pointerId: id,
      pointerType: kind,
    });
  await pointer('pointerdown', 21, 'touch');
  await pointer('pointerdown', 22, 'touch', 30);
  await pointer('pointermove', 22, 'touch', 50);
  await pointer('pointerup', 22, 'touch', 50);
  await pointer('pointerup', 21, 'touch');
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeDisabled();
  await page.getByRole('button', { name: 'Fit model' }).click();
  await pointer('pointerdown', 31, 'pen');
  await pointer('pointerdown', 32, 'touch', 30);
  await pointer('pointerup', 32, 'touch', 30);
  await pointer('lostpointercapture', 32, 'touch', 30);
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeDisabled();
  await pointer('pointermove', 31, 'pen', 20);
  await pointer('pointerup', 31, 'pen', 20);
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeEnabled();
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeDisabled();
});

test('capture ownership and auxiliary buttons cannot create or split paint strokes', async ({
  page,
}) => {
  await page.addInitScript({
    content: 'HTMLCanvasElement.prototype.setPointerCapture = function () {};',
  });
  await page.goto('/skins');
  const preview = page.getByLabel('Paint directly on the 3D worker model');
  await expect(preview).toHaveAttribute('data-frame', '0');
  const blank = await saved(page);
  const box = await preview.boundingBox();
  if (!box) throw new Error('No model viewport');
  const event = (id: number, pointerType: string, button = 0) => ({
    clientX: box.x + box.width / 2,
    clientY: box.y + box.height / 2,
    pointerId: id,
    pointerType,
    button,
    buttons: button === 1 ? 4 : 1,
    pressure: 1,
    isPrimary: true,
  });
  const undo = page.getByRole('button', { name: 'Undo', exact: true });
  await preview.dispatchEvent('pointerdown', event(41, 'mouse', 1));
  await preview.dispatchEvent('pointerup', event(41, 'mouse', 1));
  await expect(undo).toBeDisabled();

  // A palm already holding capture must neither leave a dab nor terminate the
  // pen when the browser subsequently releases the palm's capture.
  await preview.dispatchEvent('pointerdown', event(42, 'touch'));
  await preview.dispatchEvent('pointerdown', event(43, 'pen'));
  await preview.dispatchEvent('pointerup', event(42, 'touch'));
  await preview.dispatchEvent('lostpointercapture', event(42, 'touch'));
  await expect(undo).toBeDisabled();
  await preview.dispatchEvent('pointerup', event(43, 'pen'));
  await expect(undo).toBeEnabled();
  expect((await saved(page))?.image).not.toBe(blank?.image);
  await undo.click();
  expect((await saved(page))?.image).toBe(blank?.image);
  await expect(undo).toBeDisabled();

  // Unexpected loss cancels the active transaction and clears its pointer ID,
  // so the next independent stroke is accepted normally.
  await preview.dispatchEvent('pointerdown', event(44, 'pen'));
  await preview.dispatchEvent('lostpointercapture', event(44, 'pen'));
  await expect(undo).toBeDisabled();
  await preview.dispatchEvent('pointerdown', event(45, 'pen'));
  await preview.dispatchEvent('pointerup', event(45, 'pen'));
  await expect(undo).toBeEnabled();
  await undo.click();
  await expect(undo).toBeDisabled();

  await preview.dispatchEvent('pointerdown', event(46, 'pen'));
  await preview.dispatchEvent('webglcontextlost', { cancelable: true });
  await preview.dispatchEvent('pointerup', event(46, 'pen'));
  await expect(undo).toBeDisabled();
  await expect(page.getByRole('alert')).toContainText('The 3D canvas was interrupted');
  await page.getByRole('button', { name: 'Retry 3D canvas' }).click();
  await expect(page.getByRole('alert')).toBeHidden();
  await expect(page.getByText('Preparing your model…')).toBeHidden();
  await preview.dispatchEvent('pointerdown', event(47, 'pen'));
  await preview.dispatchEvent('pointerup', event(47, 'pen'));
  await expect(undo).toBeEnabled();
});

test('trackpad pinch zooms only the inspection view and preserves final framing', async ({
  page,
}) => {
  await page.goto('/skins');
  let preview = page.getByLabel('Paint directly on the 3D worker model');
  await expect(preview).toHaveAttribute('data-frame', '0');
  const image = () =>
    preview.evaluate((canvas) => (canvas as unknown as { toDataURL(): string }).toDataURL());
  const pinch = () =>
    preview.evaluate((canvas) => {
      const { WheelEvent } = globalThis as unknown as {
        WheelEvent: new (type: string, options: Record<string, unknown>) => Event;
      };
      const event = new WheelEvent('wheel', {
        deltaY: -200,
        ctrlKey: true,
        bubbles: true,
        cancelable: true,
      });
      canvas.dispatchEvent(event);
      return event.defaultPrevented;
    });
  const before = await image();
  expect(await pinch()).toBe(true);
  await expect.poll(image).not.toBe(before);
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeDisabled();
  await page.getByRole('button', { name: 'Swarm', exact: true }).click();
  preview = page.getByLabel('Paint directly on the 3D swarm model');
  await expect(preview).toHaveAttribute('data-frame', '0');
  await page.getByRole('button', { name: 'Choose final view', exact: true }).click();
  await expect(page.getByLabel('Camera angle')).toBeVisible();
  // Let the fixed game projection draw before comparing its pixels.
  await page.evaluate(
    'new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))',
  );
  const fixed = await image();
  expect(await pinch()).toBe(true);
  await page.evaluate('new Promise(resolve => requestAnimationFrame(resolve))');
  expect(await image()).toBe(fixed);
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeDisabled();
});
