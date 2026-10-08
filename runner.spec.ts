import { test } from '../../platform/node_modules/@playwright/test/index.mjs';
test.beforeEach(async ({ page }) => {
  await page.route('**/api/**', route => route.fulfill({status:401, contentType:'application/json', body:'{}'}));
});
await import('../../platform/apps/web/e2e/skins-studio-ux.spec.ts');
