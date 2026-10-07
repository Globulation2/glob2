import { expect, test, type Locator } from '@playwright/test';

async function boxFor(locator: Locator) {
  const box = await locator.boundingBox();
  if (!box) throw new Error('Expected a visible studio control.');
  return box;
}

const sizes = [
  { width: 1440, height: 900 },
  { width: 768, height: 1024 },
  { width: 1024, height: 768 },
  { width: 320, height: 568 },
  { width: 412, height: 839 },
];

test('studio retains usable canvas and reachable controls at every supported layout', async ({
  page,
}, info) => {
  for (const size of sizes) {
    await page.setViewportSize(size);
    await page.goto('/skins');
    await expect(page.getByLabel('Skin name')).toBeEnabled();
    const toolbox = page.getByRole('complementary', { name: 'Paint tools' });
    const compact = size.width <= 599 && size.height < 700;
    await expect(
      page.getByRole('button', { name: compact ? 'Expand toolbox' : 'Collapse toolbox' }),
    ).toBeVisible();
    const canvas = await boxFor(page.locator('.skin-stage > .skin-viewport'));
    expect(canvas.height).toBeGreaterThan(120);
    if (size.width <= 599) {
      const navigation = await boxFor(page.locator('.skin-navigation'));
      expect(canvas.y).toBeGreaterThanOrEqual(navigation.y + navigation.height);
    }
    expect(await page.evaluate('document.documentElement.scrollWidth <= innerWidth')).toBe(true);
    const tray = await boxFor(toolbox);
    expect(tray).toBeTruthy();
    expect(tray.x).toBeGreaterThanOrEqual(0);
    expect(tray.y).toBeGreaterThanOrEqual(0);
    expect(tray.x + tray.width).toBeLessThanOrEqual(size.width);
    expect(tray.y + tray.height).toBeLessThanOrEqual(size.height);
    for (const button of await toolbox.locator('.skin-tools button').all()) {
      const target = await boxFor(button);
      const label = await boxFor(button.locator('span'));
      expect(target.height).toBeGreaterThanOrEqual(44);
      expect(label.x).toBeGreaterThanOrEqual(target.x);
      expect(label.x + label.width).toBeLessThanOrEqual(target.x + target.width);
      expect(label.y + label.height).toBeLessThanOrEqual(target.y + target.height);
    }
    await page.screenshot({ path: info.outputPath(`studio-${size.width}x${size.height}.png`) });
    if (compact) await page.getByRole('button', { name: 'Expand toolbox' }).click();
    await page.getByLabel('Pen pressure').check();
    await expect(page.getByLabel('Pen pressure')).toBeChecked();
    await toolbox.getByText('Paint repeats on matching surfaces', { exact: true }).click();
    await expect(
      toolbox.getByText(/Some front\/back and top\/bottom surfaces share paint/),
    ).toBeVisible();
    const help = await boxFor(
      toolbox.getByText('Paint repeats on matching surfaces', { exact: true }),
    );
    expect(help.y + help.height).toBeLessThanOrEqual(size.height);
    await page.getByRole('button', { name: 'Collapse toolbox' }).click();
    await expect(page.getByRole('button', { name: 'Patterns', exact: true })).toBeVisible();
    const collapsedTray = await boxFor(toolbox);
    for (const label of await toolbox.locator('.skin-tools button span').all()) {
      const text = await boxFor(label);
      expect(text.y + text.height).toBeLessThanOrEqual(collapsedTray.y + collapsedTray.height);
    }
    if (size.width <= 599) expect(collapsedTray.height).toBeLessThanOrEqual(100);
  }
});

test('pattern modal keeps Apply visible on short phones and exposes effective controls only', async ({
  page,
}, info) => {
  await page.setViewportSize({ width: 320, height: 568 });
  await page.goto('/skins');
  await page.getByRole('button', { name: 'Patterns', exact: true }).click();
  const dialog = page.getByRole('dialog', { name: 'Patterns & fills' });
  const apply = dialog.getByRole('button', { name: 'Apply to Worker' });
  await expect(apply).toBeEnabled();
  const footer = await boxFor(dialog.locator('footer'));
  expect(footer.y + footer.height).toBeLessThanOrEqual(568);
  await expect(dialog.getByRole('slider', { name: 'Band width' })).toBeAttached();
  await dialog.getByRole('button', { name: 'Checker', exact: true }).click();
  await expect(dialog.getByRole('slider', { name: /Density|Band width/ })).toHaveCount(0);
  await dialog.getByRole('button', { name: 'Spots', exact: true }).click();
  await expect(dialog.getByRole('slider', { name: 'Spot size' })).toBeAttached();
  await dialog.getByRole('button', { name: 'Whole model', exact: true }).click();
  await expect(dialog.getByLabel('Pattern size')).toHaveCount(0);
  await expect(dialog.getByLabel('Replace the gaps too')).toHaveCount(0);
  await dialog.getByRole('button', { name: 'Mirrored bands', exact: true }).click();
  await expect(dialog.getByRole('combobox', { name: 'Band width' })).toBeAttached();
  expect(
    await dialog.locator('.skin-pattern-controls').evaluate((e) => e.scrollWidth <= e.clientWidth),
  ).toBe(true);
  await page.screenshot({ path: info.outputPath('studio-short-phone-patterns.png') });
  await dialog.getByRole('button', { name: 'Cancel', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeDisabled();
});

test('studio follows the shared system and saved themes, including open dialogs', async ({
  page,
}, info) => {
  const palettes = {
    light: { bg: 'rgb(241, 241, 225)', surface: 'rgb(251, 251, 243)', ink: 'rgb(29, 69, 48)' },
    dark: { bg: 'rgb(27, 18, 41)', surface: 'rgb(43, 28, 66)', ink: 'rgb(249, 232, 187)' },
  };
  async function expectTheme(theme: 'light' | 'dark') {
    const palette = palettes[theme];
    await expect(page.locator('.skin-studio')).toHaveCSS('background-color', palette.bg);
    await expect(page.locator('.skin-studio')).toHaveCSS('color', palette.ink);
    await expect(page.locator('.skin-studio')).toHaveCSS('color-scheme', theme);
    await expect(page.locator('.skin-topbar')).toHaveCSS('background-color', palette.surface);
    await expect(page.locator('.skin-stage')).toHaveCSS(
      'background-image',
      new RegExp(palette.bg.replace(/[()]/g, '\\$&')),
    );
    await expect(page.getByLabel('Paint color')).toHaveValue('#ed9252');
    await expect(page.getByLabel('Skin name')).toHaveValue('Shared theme design');
    const dialog = page.getByRole('dialog');
    if (await dialog.count()) {
      await expect(dialog).toHaveCSS('background-color', palette.surface);
      await expect(dialog).toHaveCSS('color', palette.ink);
      await expect(dialog).toHaveCSS('color-scheme', theme);
    }
  }
  await page.emulateMedia({ colorScheme: 'light' });
  await page.goto('/skins');
  await expect(page.getByLabel('Skin name')).toBeEnabled();
  await page.getByLabel('Skin name').fill('Shared theme design');
  await expectTheme('light');
  await page.emulateMedia({ colorScheme: 'dark' });
  await expectTheme('dark');
  const toggle = page.getByTestId('theme-toggle');
  await toggle.click(); // System → light, overriding the dark device setting.
  await expectTheme('light');
  await page.screenshot({
    path: info.outputPath('studio-light-settings.png'),
    animations: 'disabled',
  });
  await toggle.click(); // Light → dark.
  await expectTheme('dark');
  await page.screenshot({
    path: info.outputPath('studio-dark-settings.png'),
    animations: 'disabled',
  });
  await page.reload();
  await expectTheme('dark');
  await toggle.click(); // Dark → system.
  await page.emulateMedia({ colorScheme: 'light' });
  await expectTheme('light');
  await page.getByRole('button', { name: 'Patterns', exact: true }).click();
  await expectTheme('light');
  await page.emulateMedia({ colorScheme: 'dark' });
  await expectTheme('dark');
  await page.screenshot({
    path: info.outputPath('studio-dark-patterns.png'),
    animations: 'disabled',
  });
});
