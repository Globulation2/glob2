import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { AxeBuilder } from '@axe-core/playwright';
import { expect, test } from '@playwright/test';

for (const width of [320, 390, 900, 1100, 1440, 1920]) {
  for (const scheme of ['light', 'dark'] as const) {
    test(`workspace ${width}px ${scheme}`, async ({ page }, info) => {
      test.skip(info.project.name !== 'desktop', 'Explicit viewport coverage runs once.');
      await page.setViewportSize({ width, height: 900 });
      await page.emulateMedia({ colorScheme: scheme, reducedMotion: 'reduce' });
      await page.goto('/maps');
      await expect(page.getByRole('heading', { name: 'Maps', exact: true })).toBeVisible();
      await expect(page.getByTestId('map-card').first()).toBeVisible();
      expect(
        await page.evaluate(() => {
          const root = (
            globalThis as unknown as {
              document: { documentElement: { scrollWidth: number; clientWidth: number } };
            }
          ).document.documentElement;
          return root.scrollWidth - root.clientWidth;
        }),
      ).toBeLessThanOrEqual(0);
      const directory = resolve('../artifacts/web-app/workspace');
      mkdirSync(directory, { recursive: true });
      await page.screenshot({
        path: resolve(directory, `maps-${width}-${scheme}.png`),
        fullPage: true,
      });
      const violations = (await new AxeBuilder({ page }).analyze()).violations;
      expect(violations.map((v) => ({ id: v.id, nodes: v.nodes.map((n) => n.target) }))).toEqual(
        [],
      );
      if (width < 1100) {
        const opener = page.getByRole('button', { name: 'Open navigation', exact: true });
        await opener.click();
        const dialog = page.getByRole('dialog', { name: 'Navigation' });
        await expect(dialog).toBeVisible();
        await expect(dialog.getByRole('link', { name: 'Maps', exact: true })).toHaveAttribute(
          'aria-current',
          'page',
        );
        await page.keyboard.press('Escape');
        await expect(dialog).not.toBeVisible();
        await expect(opener).toBeFocused();
        await opener.click();
        await dialog.getByRole('link', { name: 'Matches', exact: true }).click();
        await expect(
          page.getByRole('heading', { name: 'Recent matches', exact: true }),
        ).toBeVisible();
        await expect(dialog).not.toBeVisible();
      } else {
        await page.getByRole('button', { name: 'Collapse sidebar' }).click();
        await expect(page.locator('.app-sidebar')).toHaveCSS('width', '72px');
        await page.getByRole('button', { name: 'Open navigation', exact: true }).click();
        await expect(page.locator('.app-sidebar')).toHaveCSS('width', '240px');
      }
    });
  }
}

test('drawer closes across breakpoints without trapping focus', async ({ page }, info) => {
  test.skip(info.project.name !== 'desktop');
  await page.setViewportSize({ width: 390, height: 700 });
  await page.goto('/');
  await page.getByRole('button', { name: 'Open navigation', exact: true }).click();
  await expect(page.getByRole('dialog')).toBeVisible();
  await page.setViewportSize({ width: 1440, height: 900 });
  await expect(page.getByRole('dialog')).not.toBeVisible();
  await page
    .getByRole('navigation', { name: 'Main', exact: true })
    .getByRole('link', { name: 'Maps', exact: true })
    .click();
  await expect(page.getByRole('heading', { name: 'Maps', exact: true })).toBeVisible();
});
