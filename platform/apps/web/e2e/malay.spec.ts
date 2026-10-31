import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test } from '@playwright/test';
import malay from '../../../packages/i18n/locales/ms.json' with { type: 'json' };
import { INVITE_CODE } from './showcase.ts';

test('Malay persists across app navigation and server-rendered pages', async ({
  page,
  context,
}, info) => {
  const base = String(info.project.use.baseURL);
  await context.addCookies([{ name: 'glob2_locale', value: 'ms', url: base }]);
  await page.route('glob2://**', (route) => route.abort());
  const routes = [
    ['home', '/'],
    ['maps', '/maps'],
    ['matches', '/matches'],
    ['leaderboard', '/leaderboard/ranked-1v1'],
    ['account', '/account'],
    ['missing', '/no-such-page'],
    ['signin', '/signin'],
    ['invite', `/j/${INVITE_CODE}`],
  ] as const;
  for (const [name, path] of routes) {
    await page.goto(path);
    await expect(page.locator('html')).toHaveAttribute('lang', 'ms');
    await expect(page.locator('html')).toHaveAttribute('dir', 'ltr');
    if (name === 'home')
      await expect(page.locator('#hero-title')).toHaveText(malay['Welcome to the colony']);
    if (name === 'signin') await expect(page.locator('#signin-username')).toBeVisible();
    if (name === 'invite')
      await expect(
        page.getByRole('link', { name: malay['Open in the Globulation 2 app'] }),
      ).toBeVisible();
    await page.evaluate('document.fonts.ready');
    expect(
      await page.evaluate(
        'document.documentElement.scrollWidth - document.documentElement.clientWidth',
      ),
      `${name} should fit the viewport`,
    ).toBeLessThanOrEqual(1);
    const directory = process.env['SCREENSHOT_DIR'];
    if (directory) {
      mkdirSync(directory, { recursive: true });
      await page.screenshot({
        path: resolve(directory, `${info.project.name}-ms-${name}.png`),
        fullPage: true,
      });
    }
  }
  await page.goto('/');
  await page.reload();
  await expect(page.locator('#hero-title')).toHaveText(malay['Welcome to the colony']);
});
