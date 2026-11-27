// The release check (src/app/ReleaseCheck.h, docs/releases/release-check.md)
// in the browser: the same scripted run of the main menu, a custom game, a
// loaded save, a lesson, the tutorial and a saved game played to its victory
// screen, started with ?release-check=all.
const {test, expect} = require('@playwright/test');
const {gameURL} = require('./main-menu');

test('the release check passes', async ({page}) => {
  // Its own CI entry runs it (.github/scripts/ci_browser_matrix.json): a whole
  // scripted run would unbalance the shards.
  test.skip(!process.env.GLOB2_RELEASE_CHECK_SPEC, 'Set GLOB2_RELEASE_CHECK_SPEC=1 to run the release check');
  test.setTimeout(30 * 60 * 1000);
  const log = [];
  const errors = [];
  page.on('console', message => log.push(message.text()));
  page.on('pageerror', error => errors.push(String(error)));
  const url = new URL(gameURL(), 'http://localhost');
  url.searchParams.set('release-check', 'all');
  await page.goto(url.pathname + url.search);
  await expect.poll(() => log.find(line => /Glob2 release check (passed|failed)/.test(line)) || '',
    {timeout: 25 * 60 * 1000, intervals: [2000]}).toMatch(/Glob2 release check passed/);
  const steps = log.filter(line => line.includes('Glob2 release check:')).map(line => line.replace(/^.*Glob2 release check: /, ''));
  for (const step of ['menu', 'custom', 'load', 'lesson', 'tutorial', 'victory'])
    expect(steps).toContain(step + ' ok');
  expect(errors).toEqual([]);
});
