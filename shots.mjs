// Extra before/after screenshots for the icon change: node shots.mjs <outdir> [port]
import { chromium, devices } from '@playwright/test';

const out = process.argv[2];
const base = `http://127.0.0.1:${process.argv[3] ?? 4291}`;
const seed = await (await fetch(`${base}/__seed`)).json();
const browser = await chromium.launch();

for (const scheme of ['light', 'dark']) {
  const desktop = await browser.newContext({ viewport: { width: 1280, height: 860 }, colorScheme: scheme });
  await desktop.addCookies([{ name: 'glob2_session', value: seed.adminSession, url: base }]);
  const page = await desktop.newPage();
  const shot = async (name, full = false) => {
    await page.waitForLoadState('networkidle');
    await page.waitForTimeout(400);
    await page.screenshot({ path: `${out}/desktop-${scheme}-${name}.png`, fullPage: full });
  };
  await page.goto(`${base}/players`);
  await shot('players');
  await page.goto(`${base}/players/ai/nicowar`);
  await shot('ai-profile');
  await page.goto(`${base}/ais`);
  await shot('ais');
  await page.goto(`${base}/admin/reports`);
  await shot('admin-reports');
  await page.goto(`${base}/matches/${seed.featuredMatch}`);
  await shot('match-head');
  await page.goto(`${base}/leaderboard`);
  await page.getByRole('button', { name: 'Collapse sidebar' }).click();
  await page.getByRole('link', { name: 'Matches' }).first().hover();
  await shot('rail');
  await desktop.close();

  const phone = await browser.newContext({ ...devices['Pixel 7'], colorScheme: scheme });
  await phone.addCookies([{ name: 'glob2_session', value: seed.adminSession, url: base }]);
  const mobile = await phone.newPage();
  await mobile.goto(`${base}/leaderboard`);
  await mobile.getByRole('button', { name: 'Open navigation' }).click();
  await mobile.waitForTimeout(500);
  await mobile.screenshot({ path: `${out}/phone-${scheme}-drawer.png` });
  await phone.close();
}
await browser.close();
