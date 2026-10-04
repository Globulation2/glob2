// Accessibility and phone layout of every page, in the light and dark themes:
// axe-core finds no violations (all rules axe runs by default, as in the UX
// review), nothing scrolls sideways at phone width, and the forms that used to
// end in bare error pages (sign-in, map upload) explain problems inline.
import { AxeBuilder } from '@axe-core/playwright';
import { expect, test, type Page } from '@playwright/test';
import type { SeededHistory } from '../../api/test/historySeed.ts';

const BASE = `http://127.0.0.1:${process.env['PORT'] ?? 4280}`;
let seed: SeededHistory;

test.beforeAll(async ({ request }) => {
  seed = (await (await request.get('/__seed')).json()) as SeededHistory;
});

async function signIn(page: Page, secret: string) {
  await page.context().addCookies([{ name: 'glob2_session', value: secret, url: BASE }]);
}

async function axe(page: Page) {
  const results = await new AxeBuilder({ page }).analyze();
  return results.violations.map((v) => ({
    id: v.id,
    impact: v.impact,
    nodes: v.nodes.slice(0, 3).map((n) => n.target.join(' ')),
  }));
}

async function sidewaysScroll(page: Page) {
  // Runs in the page (this file is type-checked without the DOM library).
  return page.evaluate(() => {
    const root = (
      globalThis as unknown as {
        document: { documentElement: { scrollWidth: number; clientWidth: number } };
      }
    ).document.documentElement;
    return root.scrollWidth - root.clientWidth;
  });
}

const pages = (): { name: string; path: string; signedIn?: boolean }[] => [
  { name: 'home', path: '/' },
  { name: 'leaderboard', path: '/leaderboard/ranked-1v1' },
  { name: 'matches', path: '/matches' },
  { name: 'match', path: `/matches/${seed.featuredMatch}` },
  { name: 'player', path: `/players/${seed.accounts.bradley}` },
  { name: 'skins-signed-out', path: '/skins' },
  { name: 'skins', path: '/skins', signedIn: true },
  { name: 'maps', path: '/maps' },
  { name: 'map', path: `/maps/${seed.mapId}` },
  { name: 'upload-signed-out', path: '/maps/new' },
  { name: 'upload', path: '/maps/new', signedIn: true },
  { name: 'not-found', path: '/no/such/page' },
  { name: 'signin', path: '/signin' },
  { name: 'invite-unknown', path: '/j/NOSUCHCODE' },
];

for (const scheme of ['light', 'dark'] as const) {
  test(`every page passes axe and fits the screen (${scheme})`, async ({ page }) => {
    await page.emulateMedia({ colorScheme: scheme });
    const found: Record<string, unknown> = {};
    for (const entry of pages()) {
      await page.context().clearCookies();
      if (entry.signedIn) await signIn(page, seed.userSession);
      await page.goto(entry.path);
      await expect(page.getByRole('heading', { level: 1 }).first()).toBeVisible();
      await page.waitForLoadState('networkidle');
      const violations = await axe(page);
      const overflow = await sidewaysScroll(page);
      if (violations.length > 0 || overflow > 0) found[entry.name] = { violations, overflow };
    }
    expect(found).toEqual({});
  });
}

test('the match page’s connection table fits phones as cards', async ({ page }, info) => {
  await page.goto(`/matches/${seed.featuredMatch}`);
  await page.getByText('Verification and connection details', { exact: true }).click();
  const region = page.getByRole('region', { name: 'Connection quality per player' });
  await expect(region).toBeVisible();
  await expect(region).toHaveAttribute('tabindex', '0');
  const rows = page.getByTestId('network-row');
  await expect(rows).toHaveCount(2);
  const regionBox = await region.boundingBox();
  const scroll = await region.evaluate((el) => el.scrollWidth - el.clientWidth);
  expect(scroll).toBeLessThanOrEqual(0);
  if (info.project.name === 'phone') {
    // Cards: each cell is a "label  value" line, every one inside the screen.
    const cells = rows.first().getByRole('cell');
    for (const cell of await cells.all()) {
      const box = (await cell.boundingBox()) ?? { x: Infinity, width: 0 };
      expect(box.x + box.width).toBeLessThanOrEqual(
        (regionBox?.x ?? 0) + (regionBox?.width ?? 0) + 1,
      );
    }
    const labels = await cells.evaluateAll((all) =>
      all.map(
        (cell) =>
          (
            globalThis as unknown as {
              getComputedStyle(el: unknown, pseudo: string): { content: string };
            }
          ).getComputedStyle(cell, '::before').content,
      ),
    );
    expect(labels).toContain('"Delayed orders"');
  }
});

test('sign-up problems stay on the form with the username kept', async ({ page }, info) => {
  const username = `e2e_${info.project.name}`;
  await page.goto('/signin');
  const form = page.locator('form', { has: page.getByRole('button', { name: 'Create account' }) });
  await form.getByLabel('Choose a username').fill('e2e user');
  await form.getByLabel('Choose a password').fill('short');
  await form.getByRole('button', { name: 'Create account' }).click();
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Sign in');
  await expect(page.locator('#register-username-error')).toHaveText(
    'Usernames can only use letters, digits, dots, dashes and underscores (no spaces).',
  );
  await expect(page.getByLabel('Choose a username')).toHaveValue('e2e user');
  await expect(page.getByLabel('Choose a username')).toHaveAttribute('aria-invalid', 'true');
  await expect(page.getByLabel('Choose a password')).toHaveValue('');
  expect(await axe(page)).toEqual([]);
  await page.getByLabel('Choose a username').fill(username);
  await page.getByLabel('Choose a password').fill('short');
  await page.getByRole('button', { name: 'Create account' }).click();
  await expect(page.locator('#register-password-error')).toContainText(
    'This password is too short: it has 5 characters',
  );
  // The wrong password for an existing account.
  await page.getByLabel('Choose a password').fill('a long enough password');
  await page.getByRole('button', { name: 'Create account' }).click();
  await expect(page.getByText('You are signed in as')).toBeVisible();
  await page.context().clearCookies();
  await page.goto('/signin');
  await page.locator('#signin-username').fill(username);
  await page.locator('#signin-password').fill('not the password');
  await page.getByRole('button', { name: 'Sign in', exact: true }).click();
  await expect(page.locator('#signin-password-error')).toHaveText(
    'That password is not right for this username. Try again.',
  );
  await expect(page.locator('#signin-username')).toHaveValue(username);
});

test('a file that is not a map is refused on the form, before any map exists', async ({ page }) => {
  await signIn(page, seed.userSession);
  await page.goto('/maps/new');
  await page.getByLabel(/Map file/).setInputFiles({
    name: 'holiday.map.gz',
    mimeType: 'application/gzip',
    buffer: Buffer.from('not a map, just some text'),
  });
  await expect(page.getByLabel('Title')).toHaveValue('holiday');
  await page.getByRole('button', { name: 'Upload' }).click();
  await expect(page.locator('#map-file-error')).toContainText(
    "This file isn't a Globulation 2 map",
  );
  await expect(page).toHaveURL(/\/maps\/new$/);
  await page.goto('/maps/mine');
  await expect(page.locator('main')).toContainText('You have not shared any maps yet.');
});
