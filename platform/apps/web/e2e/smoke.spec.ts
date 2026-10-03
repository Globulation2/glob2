// Smoke test of every web app page against the seeded history: deep links
// the game uses, leaderboards, profile, match page with charts and replay,
// map catalog with sign-in state, moderation, and Watch in browser when a
// browser game build is present.
import { existsSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { expect, test, type Page, type TestInfo } from '@playwright/test';
import type { SeededHistory } from '../../api/test/historySeed.ts';

const BASE = `http://127.0.0.1:${process.env['PORT'] ?? 4280}`;
let seed: SeededHistory;

test.beforeAll(async ({ request }) => {
  seed = (await (await request.get('/__seed')).json()) as SeededHistory;
});

async function shot(page: Page, info: TestInfo, name: string) {
  const dir = process.env['SCREENSHOT_DIR'];
  if (!dir) return;
  mkdirSync(dir, { recursive: true });
  await page.screenshot({ path: join(dir, `${info.project.name}-${name}.png`), fullPage: true });
}

async function signIn(page: Page, secret: string) {
  await page.context().addCookies([{ name: 'glob2_session', value: secret, url: BASE }]);
}

test('home shows the instance, leaderboard teasers and recent matches', async ({ page }, info) => {
  await page.goto('/');
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Glob2 Online (test)');
  await expect(page.getByTestId('ladder-teaser').first()).toContainText('Kestrel');
  await expect(page.getByTestId('match-row').first()).toBeVisible();
  await expect(page.getByRole('link', { name: 'Play in browser' }).first()).toHaveAttribute(
    'href',
    '/play/',
  );
  await expect(page.getByRole('link', { name: 'Sign in' })).toHaveAttribute('href', '/signin');
  await shot(page, info, 'home');
});

test('leaderboard ranks players and lists AIs per version', async ({ page }, info) => {
  await page.goto('/leaderboard/ranked-1v1');
  const rows = page.getByTestId('leaderboard-row');
  await expect(rows.first()).toContainText('Kestrel');
  await expect(page.locator('main')).not.toContainText('Spammer');
  await expect(page.locator('main')).toContainText('Nicowar');
  await expect(page.locator('main')).toContainText('older version');
  await page.getByLabel('Hide provisional ratings').check();
  await expect(page.locator('main')).not.toContainText('Ana_M');
  await shot(page, info, 'leaderboard');
});

test('player profile shows ratings, graph, aggregates and matches', async ({ page }, info) => {
  await page.goto(`/players/${seed.accounts.bradley}`);
  await expect(page.getByTestId('player-name')).toHaveText('Bradley');
  await expect(page.getByTestId('rating-tile')).toHaveCount(2);
  await expect(page.getByRole('img', { name: 'Rating after each rated match' })).toBeVisible();
  await expect(page.locator('main')).toContainText('Even Ground Classic');
  await expect(page.getByTestId('match-row')).toHaveCount(9);
  await page.getByRole('button', { name: 'Rooms' }).click();
  await expect(page.locator('main')).toContainText('No matches yet.');
  await page.getByRole('button', { name: 'All' }).click();
  await expect(page.getByTestId('match-row')).toHaveCount(9);
  await shot(page, info, 'player');
  await page.goto(`/players/${seed.accounts.guest}`);
  await expect(page.locator('main')).toContainText('Guests are not ranked');
  await page.goto(`/players/${seed.accounts.banned}`);
  await expect(page.getByRole('alert')).toContainText('Not found');
});

test('match page shows players, charts, verification and the replay', async ({ page }, info) => {
  await page.goto(`/matches/${seed.featuredMatch}`);
  await expect(page.getByTestId('match-title')).toContainText(
    '1 vs 1 ranked · Even Ground Classic',
  );
  await expect(page.getByTestId('participant')).toHaveCount(2);
  await expect(page.getByTestId('timelines').getByRole('img')).toHaveCount(3);
  await expect(page.locator('main')).toContainText('refused');
  const watch = page.getByTestId('watch');
  const replayUrl = `${BASE}/api/v1/matches/${seed.featuredMatch}/artifacts/replay`;
  await expect(watch).toHaveAttribute('href', `/play/?replay=${encodeURIComponent(replayUrl)}`);
  const replay = await page.request.get(replayUrl);
  expect(replay.status()).toBe(200);
  // Hovering a chart shows its values.
  const chart = page.getByTestId('timelines').getByRole('img').first();
  await chart.scrollIntoViewIfNeeded();
  const box = await chart.boundingBox();
  if (box) await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await expect(page.locator('.tip').first()).toBeVisible();
  await shot(page, info, 'match');
  await page.goto(`/matches/${seed.pendingMatch}`);
  await expect(page.locator('main')).toContainText('checking this match');
});

test('map catalog, map page and signed-in like', async ({ page }, info) => {
  await page.goto('/maps');
  await expect(page.getByTestId('map-card')).toHaveCount(1);
  await shot(page, info, 'maps');
  await page.getByTestId('map-card').click();
  await expect(page.getByTestId('map-title')).toHaveText('Even Ground Classic');
  await expect(page.getByRole('link', { name: 'Sign in to like or report' })).toBeVisible();
  await signIn(page, seed.userSession);
  await page.reload();
  await expect(page.getByTestId('account-chip')).toContainText('Kestrel');
  // The seeded Kestrel already likes the map: unlike, then like again.
  await page.getByRole('button', { name: '♥ Liked' }).click();
  await expect(page.getByTestId('likes')).toHaveText('0');
  await page.getByRole('button', { name: '♡ Like' }).click();
  await expect(page.getByTestId('likes')).toHaveText('1');
  await shot(page, info, 'map');
  await page.goto('/maps/mine');
  await expect(page.locator('main')).toContainText('You have not shared any maps yet.');
});

test('moderation pages for administrators only', async ({ page }, info) => {
  await page.goto('/admin');
  await expect(page.locator('main')).toContainText('Sign in');
  await signIn(page, seed.userSession);
  await page.goto('/admin');
  await expect(page.locator('main')).toContainText('This page is for moderators.');
  await signIn(page, seed.adminSession);
  await page.goto('/admin/accounts');
  await expect(page.getByTestId('admin-account').first()).toBeVisible();
  await page.getByLabel('Search accounts').fill('Spammer');
  await page.getByRole('button', { name: 'Search' }).click();
  await expect(page.getByTestId('admin-account')).toHaveCount(1);
  await expect(page.getByTestId('admin-account')).toContainText('banned');
  await shot(page, info, 'admin-accounts');
  await page.goto('/admin/reports');
  await expect(page.getByTestId('admin-report')).toContainText('north colony');
  await shot(page, info, 'admin-reports');
  await page.goto('/admin/matches');
  await page.getByLabel('Status').selectOption('running');
  await expect(page.getByTestId('match-row')).toHaveCount(1);
});

test('watch in browser opens the replay in the browser game', async ({ page }, info) => {
  const game =
    process.env['GLOB2_WEB_CLIENT_DIR'] ??
    join(import.meta.dirname, '../../../../build/emscripten/client/release');
  test.skip(!existsSync(join(game, 'index.html')), 'no browser game build');
  test.skip(info.project.name !== 'desktop', 'one browser run is enough');
  test.setTimeout(180_000);
  await page.goto(`/matches/${seed.featuredMatch}`);
  await page.getByTestId('watch').click();
  await expect(page).toHaveURL(/\/play\/\?replay=/);
  // glob2Diagnostics is the browser shell's read-only diagnostics (browser/shell.html).
  const snapshot = () =>
    page.evaluate(() =>
      (
        globalThis as unknown as {
          glob2Diagnostics?: { snapshot(): { watchReplay: string; screen: string; tick: number } };
        }
      ).glob2Diagnostics?.snapshot(),
    );
  await expect.poll(async () => (await snapshot())?.watchReplay, { timeout: 90_000 }).toBe('ready');
  await expect.poll(async () => (await snapshot())?.screen, { timeout: 120_000 }).toBe('match');
  await expect
    .poll(async () => (await snapshot())?.tick ?? 0, { timeout: 120_000 })
    .toBeGreaterThan(25);
  await shot(page, info, 'watch-replay');
});
