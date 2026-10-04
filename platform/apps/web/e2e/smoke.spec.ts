// Smoke test of every web app page against the seeded history: deep links
// the game uses, leaderboards, profile, match page with charts and replay,
// map catalog with sign-in state, moderation, the server-rendered sign-in and
// invite pages, and Watch in browser when a browser game build is present.
//
// Every page is also checked with axe (WCAG 2.2 A/AA rules) in the light and
// the dark theme, and must not scroll sideways on phones (320 and 430 px).
// SCREENSHOT_DIR=<dir> saves desktop and phone screenshots of each page in
// both themes; AXE_REPORT=<file> writes the axe results as JSON.
import { appendFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { AxeBuilder } from '@axe-core/playwright';
import { expect, test, type Page, type TestInfo } from '@playwright/test';
import type { SeededHistory } from '../../api/test/historySeed.ts';
import { INVITE_CODE, SHOWCASE_MAP_COUNT } from './showcase.ts';

const BASE = `http://127.0.0.1:${process.env['PORT'] ?? 4280}`;
const THEMES = ['light', 'dark'] as const;
let seed: SeededHistory;

test.beforeAll(async ({ request }) => {
  seed = (await (await request.get('/__seed')).json()) as SeededHistory;
});

// Screenshots and axe runs see the globs parked (reduced motion), so the
// pictures are stable; a separate test covers the animation itself.
test.use({ reducedMotion: 'reduce' });

// The platform typecheck has no DOM library, so axe's result types and code
// that runs in the page are typed here or passed as strings.
interface AxeRule {
  id: string;
  impact?: string | null;
  nodes: { target: unknown[] }[];
}
interface AxeOutcome {
  passes: AxeRule[];
  incomplete: AxeRule[];
  violations: AxeRule[];
}

/** Evaluates `expression` in the page. */
function inPage<T>(page: Page, expression: string): Promise<T> {
  return page.evaluate(expression) as Promise<T>;
}

async function axe(page: Page, info: TestInfo, name: string, theme: string) {
  const results: AxeOutcome = await new AxeBuilder({ page })
    .withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22a', 'wcag22aa', 'best-practice'])
    .analyze();
  const report = process.env['AXE_REPORT'];
  if (report) {
    mkdirSync(dirname(report), { recursive: true });
    appendFileSync(
      report,
      `${JSON.stringify({
        page: name,
        project: info.project.name,
        theme,
        url: page.url(),
        passes: results.passes.length,
        incomplete: results.incomplete.map((r) => r.id),
        violations: results.violations.map((v) => ({
          id: v.id,
          impact: v.impact,
          nodes: v.nodes.map((n) => n.target.join(' ')),
        })),
      })}\n`,
    );
  }
  expect(
    results.violations.map(
      (v) => `${v.id} (${v.impact}): ${v.nodes.map((n) => n.target).join(', ')}`,
    ),
    `axe violations on ${name} (${theme})`,
  ).toEqual([]);
}

/** Checks the page in both themes (axe) and saves screenshots when asked. */
async function check(page: Page, info: TestInfo, name: string) {
  for (const theme of THEMES) {
    await page.emulateMedia({ colorScheme: theme, reducedMotion: 'reduce' });
    await inPage(page, 'document.fonts.ready.then(() => true)');
    await axe(page, info, name, theme);
    const dir = process.env['SCREENSHOT_DIR'];
    if (dir) {
      mkdirSync(dir, { recursive: true });
      await page.screenshot({
        path: join(dir, `${info.project.name}-${theme}-${name}.png`),
        fullPage: true,
      });
    }
  }
  await page.emulateMedia({ colorScheme: 'light', reducedMotion: 'reduce' });
}

async function signIn(page: Page, secret: string) {
  await page.context().addCookies([{ name: 'glob2_session', value: secret, url: BASE }]);
}

test('home shows the colony, ways in, live stats, ladders, maps and matches', async ({
  page,
}, info) => {
  await page.goto('/');
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Glob2 Online (test)');
  await expect(page.getByTestId('ladder-teaser').first()).toContainText('Kestrel');
  await expect(page.getByTestId('match-row').first()).toBeVisible();
  await expect(page.getByRole('link', { name: 'Play in browser' }).first()).toHaveAttribute(
    'href',
    '/play/',
  );
  await expect(page.getByRole('link', { name: 'Sign in' })).toHaveAttribute('href', '/signin');
  // Live numbers come from GET /api/v1/stats: the seeded running match is live.
  const stats = page.getByTestId('live-stats');
  await expect(stats).toContainText(/players? online/);
  await expect(stats).toContainText(/match(es)? on now/);
  await expect(stats).not.toContainText('–');
  await expect(page.getByTestId('featured-maps').getByRole('link')).toHaveCount(4);
  await expect(page.getByTestId('featured-maps')).toContainText('Isles of Plenty');
  await check(page, info, 'home');
});

test('home: nothing covers the header over the hero', async ({ page }) => {
  await page.goto('/');
  // Each header control must be the topmost element at its own centre.
  for (const target of [
    page.locator('.site-header .brand'),
    page.getByRole('navigation', { name: 'Main' }).getByRole('link').first(),
    page.getByRole('link', { name: 'Sign in' }),
  ]) {
    await expect(target).toBeVisible();
    const box = await target.boundingBox();
    expect(box).not.toBeNull();
    const x = (box?.x ?? 0) + (box?.width ?? 0) / 2;
    const y = (box?.y ?? 0) + (box?.height ?? 0) / 2;
    await target.evaluate((el) => el.setAttribute('data-hit-probe', ''));
    const topmost = await inPage<boolean>(
      page,
      `(() => { const el = document.querySelector('[data-hit-probe]'); const hit = document.elementFromPoint(${x}, ${y}); el.removeAttribute('data-hit-probe'); return Boolean(hit && el.contains(hit)); })()`,
    );
    expect(topmost).toBe(true);
  }
  // The hero card starts below the header.
  const card = await page.locator('.hero-card').boundingBox();
  const header = await page.locator('.site-header').boundingBox();
  expect(card?.y ?? 0).toBeGreaterThanOrEqual((header?.y ?? 0) + (header?.height ?? 0));
});

test('missing pages and items have a heading and a title', async ({ page }) => {
  await page.goto('/no-such-page');
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Page not found');
  await expect(page).toHaveTitle(/^Page not found · /);
  await page.goto('/matches/00000000-0000-0000-0000-000000000000');
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Match not found');
});

test('join with code opens the invite page; bad codes explain themselves', async ({ page }) => {
  await page.goto('/');
  const input = page.getByLabel('Got an invite code?');
  await input.fill('no!');
  await page.getByRole('button', { name: 'Join' }).click();
  await expect(input).toHaveAttribute('aria-invalid', 'true');
  await expect(page.locator('#join-hint')).toContainText('6 to 16 letters');
  await input.fill(INVITE_CODE);
  await input.press('Enter');
  await expect(page).toHaveURL(new RegExp(`/j/${INVITE_CODE}$`));
  await expect(page.locator('.room')).toHaveText('Saturday night colony');
});

test('server-rendered invite and sign-in pages share the look', async ({ page }, info) => {
  // An invite tries to open the installed game; stop that so the page stays.
  await page.route('glob2://**', (route) => route.abort());
  await page.goto(`/j/${INVITE_CODE}`);
  await expect(page.locator('.room')).toHaveText('Saturday night colony');
  await expect(page.getByRole('link', { name: 'Open in the Globulation 2 app' })).toBeVisible();
  await expect(page.locator('meta[property="og:image"]')).toHaveAttribute(
    'content',
    `${BASE}/signin/assets/og-colony.jpg`,
  );
  const art = await page.request.get('/signin/assets/colony.webp');
  expect(art.headers()['content-type']).toBe('image/webp');
  await check(page, info, 'invite');
  await page.goto('/j/NOSUCHCODE');
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Invite not found');
  await check(page, info, 'invite-missing');
  await page.goto('/signin');
  await expect(page.locator('#signin-username')).toBeVisible();
  await check(page, info, 'signin');
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
  await check(page, info, 'leaderboard');
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
  await check(page, info, 'player');
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
  // Team swatches use the game's colours: red and cyan on a two-team map.
  const swatches = page.getByTestId('participant').locator('.sw');
  await expect(swatches.first()).toHaveCSS('background-color', 'rgb(230, 46, 46)');
  await expect(swatches.nth(1)).toHaveCSS('background-color', 'rgb(46, 230, 230)');
  await check(page, info, 'match');
  // Hovering a chart shows its values.
  const chart = page.getByTestId('timelines').getByRole('img').first();
  await chart.scrollIntoViewIfNeeded();
  const box = await chart.boundingBox();
  if (box) await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await expect(page.locator('.tip').first()).toBeVisible();
  await page.goto(`/matches/${seed.pendingMatch}`);
  await expect(page.locator('main')).toContainText('checking this match');
});

test('recent matches list filters by queue', async ({ page }, info) => {
  await page.goto('/matches');
  await expect(page.getByTestId('match-row').first()).toBeVisible();
  await check(page, info, 'matches');
});

test('map catalog, map page and signed-in like', async ({ page }, info) => {
  await page.goto('/maps');
  await expect(page.getByTestId('map-card')).toHaveCount(1 + SHOWCASE_MAP_COUNT);
  await check(page, info, 'maps');
  await page.getByTestId('map-card').filter({ hasText: 'Even Ground Classic' }).click();
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
  await check(page, info, 'map');
  await page.goto('/maps/mine');
  await expect(page.locator('main')).toContainText('You have not shared any maps yet.');
  await check(page, info, 'maps-mine');
  await page.goto('/maps/new');
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Upload a map');
  await check(page, info, 'map-upload');
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
  await check(page, info, 'admin-accounts');
  await page.goto('/admin/reports');
  await expect(page.getByTestId('admin-report')).toContainText('north colony');
  await check(page, info, 'admin-reports');
  await page.goto('/admin/matches');
  await page.getByLabel('Status').selectOption('running');
  await expect(page.getByTestId('match-row')).toHaveCount(1);
  await check(page, info, 'admin-matches');
});

test('account page signed out and signed in', async ({ page }, info) => {
  await page.goto('/account');
  await expect(page.getByRole('link', { name: 'Sign in' }).last()).toHaveAttribute(
    'href',
    '/signin',
  );
  await signIn(page, seed.userSession);
  await page.goto('/account');
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Your account');
  await expect(page.locator('main')).toContainText('Kestrel');
  await check(page, info, 'account');
});

test('the theme toggle cycles system, light and dark and is remembered', async ({ page }) => {
  await page.emulateMedia({ colorScheme: 'light' });
  await page.goto('/leaderboard');
  const toggle = page.getByTestId('theme-toggle');
  const bg = () => inPage<string>(page, 'getComputedStyle(document.body).backgroundColor');
  await expect(toggle).toHaveAccessibleName(/same as this device/);
  await expect.poll(bg).toBe('rgb(241, 241, 225)');
  await toggle.click();
  await expect(toggle).toHaveAccessibleName(/light/);
  await toggle.click();
  await expect(toggle).toHaveAccessibleName(/dark/);
  await expect.poll(bg).toBe('rgb(27, 18, 41)');
  await page.reload();
  expect(await inPage(page, 'document.documentElement.dataset.theme')).toBe('dark');
  await expect.poll(bg).toBe('rgb(27, 18, 41)');
  await page.getByTestId('theme-toggle').click();
  await expect(page.getByTestId('theme-toggle')).toHaveAccessibleName(/same as this device/);
});

test('keyboard: skip link first, visible focus, focus moves to new pages', async ({
  page,
}, info) => {
  test.skip(info.project.name !== 'desktop', 'keyboard navigation is a desktop check');
  await page.goto('/');
  await page.keyboard.press('Tab');
  const skip = page.getByRole('link', { name: 'Skip to content' });
  await expect(skip).toBeFocused();
  await expect(skip).toBeInViewport();
  const outline = await inPage(page, 'getComputedStyle(document.activeElement).outlineStyle');
  expect(outline).toBe('solid');
  await page.keyboard.press('Enter');
  await expect(page.locator('#main')).toBeFocused();
  await page.getByRole('navigation', { name: 'Main' }).getByRole('link', { name: 'Maps' }).click();
  await expect(page.locator('#main')).toBeFocused();
});

test('the colony moves, can be paused, and keeps still for reduced motion', async ({ page }) => {
  await page.emulateMedia({ reducedMotion: 'no-preference' });
  await page.goto('/');
  const walker = "getComputedStyle(document.querySelector('.walker'))";
  const animation = () => inPage<string>(page, `${walker}.animationName`);
  const state = () => inPage<string>(page, `${walker}.animationPlayState`);
  expect(await animation()).toBe('cross');
  expect(await state()).toBe('running');
  await page.getByRole('button', { name: /Pause the globs/ }).click();
  expect(await state()).toBe('paused');
  await page.getByRole('button', { name: /Let the globs roam/ }).click();
  expect(await state()).toBe('running');
  await page.emulateMedia({ reducedMotion: 'reduce' });
  expect(await animation()).toBe('none');
  await expect(page.getByRole('button', { name: /Pause the globs/ })).toBeHidden();
});

test('phones: no sideways scrolling at 320 and 430 px, 44 px touch targets', async ({
  page,
}, info) => {
  test.skip(info.project.name !== 'phone', 'phone layout check');
  await signIn(page, seed.adminSession);
  const paths = [
    '/',
    '/leaderboard',
    `/players/${seed.accounts.bradley}`,
    `/matches/${seed.featuredMatch}`,
    '/matches',
    '/maps',
    `/maps/${seed.mapId}`,
    '/maps/new',
    '/account',
    '/admin/accounts',
    '/admin/reports',
    `/j/${INVITE_CODE}`,
    '/signin',
  ];
  await page.route('glob2://**', (route) => route.abort());
  for (const width of [320, 430]) {
    await page.setViewportSize({ width, height: 800 });
    for (const path of paths) {
      await page.goto(path);
      await page.waitForLoadState('networkidle');
      const overflow = await inPage<number>(
        page,
        'document.documentElement.scrollWidth - window.innerWidth',
      );
      expect(overflow, `${path} at ${width} px scrolls sideways`).toBeLessThanOrEqual(0);
      // Buttons, nav links and form fields: at least 44 x 44 CSS px.
      const small = await inPage<string[]>(
        page,
        `[...document.querySelectorAll(
          'button, .btn, a.button, .nav a, .seg > *, input:not([type=checkbox]), select')]
          .filter((el) => el.offsetParent !== null)
          .map((el) => ({ el, r: el.getBoundingClientRect() }))
          .filter(({ r }) => r.height < 43.5 || r.width < 43.5)
          .map(({ el, r }) => el.tagName + ' "' + el.textContent.trim() + '" ' + r.width + 'x' + r.height)`,
      );
      expect(small, `${path} at ${width} px has small touch targets`).toEqual([]);
    }
  }
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
});
