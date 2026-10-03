// The online hub's "open a web page" actions (Full leaderboard, Sign in) in the
// browser game, on the threaded and the serial runtime. The threaded runtime runs
// the game on a worker without window or document; those actions used to throw
// there and freeze the game. They now open from the page's thread while the click
// still counts as a user gesture, or offer a link to tap when a browser refuses.
//
// The game is served as https://glob2.test (a secure, cross-origin isolated
// context, so its instance is https and its socket wss) with the files from the
// test server; the platform's REST API, realtime socket and web pages are stood
// in here (context.route, context.routeWebSocket).
const {test, expect} = require('@playwright/test');
const {gameURL, clickMainMenu, clickControl} = require('./main-menu');

const ORIGIN = 'https://glob2.test';
const base64url = value => Buffer.from(JSON.stringify(value)).toString('base64url');
const token = name => `${base64url({alg:'none'})}.${base64url({sub:name, exp:Math.floor(Date.now() / 1000) + 3600})}.x`;
const guest = {id:'11111111-1111-4111-8111-111111111111', displayName:'Guest-4242', kind:'guest'};
const registered = {id:guest.id, displayName:'Ana', kind:'registered'};
const tokens = name => ({accessToken:token(name), refreshToken:'refresh-' + name,
  accessTokenExpiresAt:new Date(Date.now() + 3600e3).toISOString(), refreshTokenExpiresAt:new Date(Date.now() + 864e5).toISOString()});

async function fakePlatform(page, served) {
  const platform = {socket:null, attempt:null, requests:[]};
  const context = page.context();
  await context.route(ORIGIN + '/**', async route => {
    const url = new URL(route.request().url());
    const json = body => route.fulfill({status:200, contentType:'application/json', body:JSON.stringify(body)});
    if (url.pathname === '/api/v1/auth/guest') return json({deviceCredential:'device-1', account:guest, tokens:tokens('guest')});
    if (url.pathname === '/api/v1/instance') return json({name:'Test instance', queues:[], authProviders:[], guestsAllowed:true});
    if (url.pathname === '/api/v1/accounts/me') return json(platform.attempt?.done ? registered : guest);
    if (url.pathname.startsWith('/api/v1/leaderboards/')) return json({entries:[]});
    if (url.pathname.startsWith('/api/v1/')) return json({items:[]});
    if (url.pathname === '/signin' || url.pathname === '/leaderboard')
      return route.fulfill({status:200, contentType:'text/html', body:'<!doctype html><title>' + url.pathname + '</title>'});
    const response = await route.fetch({url:served + url.pathname + url.search});
    return route.fulfill({response});
  });
  await context.routeWebSocket('wss://glob2.test/realtime', socket => {
    platform.socket = socket;
    socket.onMessage(message => {
      const request = JSON.parse(String(message));
      platform.requests.push(request.method);
      const reply = result => socket.send(JSON.stringify({type:'response', id:request.id, ok:true, result}));
      switch (request.method) {
      case 'session.hello':
        return reply({sessionId:'session-1', simSupported:true, ...(request.params.accessToken ? {account:guest} : {})});
      case 'session.authenticate':
        return reply({account:platform.attempt?.done ? registered : guest});
      case 'auth.handoff.begin':
        platform.attempt = {id:'attempt-1'};
        return reply({attemptId:'attempt-1', signInUrl:ORIGIN + '/signin?attempt=attempt-1',
          confirmationCode:'ABC123', expiresAt:new Date(Date.now() + 600e3).toISOString(), resumeToken:'resume-1'});
      default:
        return reply({});
      }
    });
  });
  return platform;
}

const snapshot = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await snapshot(page)).screen, {timeout:60000}).toContain(name);

// What a click opened: a tab, or (a browser that refuses the tab) the page's link,
// which the player taps.
async function expectOpened(page, path) {
  await expect.poll(async () => (await snapshot(page)).opened.length, {message:'nothing opened'}).toBeGreaterThan(0);
  const opened = (await snapshot(page)).opened.at(-1);
  expect(new URL(opened.url).pathname).toBe(path);
  test.info().annotations.push({type:'opened', description:`${path} via ${opened.via}`});
  if (opened.via === 'tab') return opened;
  expect(opened.via).toBe('offered');
  await expect(page.locator('#open-link')).toBeVisible();
  const popup = page.context().waitForEvent('page');
  await page.locator('#open-link-anchor').click();
  expect(new URL((await popup).url()).pathname).toBe(path);
  await expect(page.locator('#open-link')).toBeHidden();
  return opened;
}

// Firefox starts the threaded runtime slowly on shared CI-class machines.
test.describe.configure({timeout:240000});

for (const runtime of ['threaded', 'serial']) {
  test.describe(`online web links (${runtime} runtime)`, () => {
    const url = () => {
      const target = new URL(gameURL(), 'http://localhost');
      if (runtime === 'serial') target.searchParams.set('threads', 'serial');
      return ORIGIN + target.pathname + target.search;
    };
    const online = async (page, served) => {
      const platform = await fakePlatform(page, served);
      await page.goto(url()); await screen(page, 'MainMenuScreen');
      const started = await snapshot(page);
      // Playwright's WebKit gives a routed origin no shared memory; the plain-origin
      // test below covers its threaded runtime.
      test.skip(runtime === 'threaded' && test.info().project.name === 'webkit' && started.threadFallback === 'shared memory unavailable',
        'WebKit under Playwright routing has no shared memory for a routed origin');
      expect(started.executionMode, 'thread fallback: ' + JSON.stringify(started.threadFallback) +
        ' isolated: ' + await page.evaluate(() => self.crossOriginIsolated)).toBe(runtime);
      await clickMainMenu(page, 'yog'); await screen(page, 'OnlineHubScreen');
      // Online as a guest: the hub offers Sign in.
      await expect.poll(async () => Boolean((await snapshot(page)).controls['account/signin']), {timeout:30000}).toBe(true);
      return platform;
    };

    test('Full leaderboard opens the page and the game keeps running', async ({page, baseURL}) => {
      const errors = [];
      page.on('pageerror', error => errors.push(String(error)));
      page.on('console', message => { if (/is not defined/.test(message.text())) errors.push(message.text()); });
      await online(page, baseURL);
      const popups = [];
      page.context().on('page', p => popups.push(p));
      await clickControl(page, 'leaderboard/full');
      const opened = await expectOpened(page, '/leaderboard');
      if (opened.via === 'tab') await expect.poll(() => popups.length).toBeGreaterThan(0);
      await page.bringToFront();
      // Still running: frames go on and the next click is handled.
      const loop = (await snapshot(page)).loop;
      await expect.poll(async () => (await snapshot(page)).loop).toBeGreaterThan(loop + 5);
      await clickControl(page, 'back'); await screen(page, 'MainMenuScreen');
      expect(errors).toEqual([]);
    });

    // From the test server itself (http, so the hub stays offline: no platform), which
    // every browser runs threaded; the leaderboard link is there offline too.
    test('Full leaderboard opens from the offline hub on the test server', async ({page}) => {
      const errors = [];
      page.on('pageerror', error => errors.push(String(error)));
      page.on('console', message => { if (/is not defined/.test(message.text())) errors.push(message.text()); });
      await page.goto(url().slice(ORIGIN.length)); await screen(page, 'MainMenuScreen');
      expect((await snapshot(page)).executionMode).toBe(runtime);
      await clickMainMenu(page, 'yog'); await screen(page, 'OnlineHubScreen');
      await clickControl(page, 'leaderboard/full');
      await expectOpened(page, '/leaderboard');
      await page.bringToFront();
      const loop = (await snapshot(page)).loop;
      await expect.poll(async () => (await snapshot(page)).loop).toBeGreaterThan(loop + 5);
      await clickControl(page, 'back'); await screen(page, 'MainMenuScreen');
      expect(errors).toEqual([]);
    });

    // A browser that refuses the new tab (a popup blocker, or a gesture it no longer
    // counts): the page offers the link instead, and tapping it opens the page.
    test('a refused tab is offered as a link to tap', async ({page}) => {
      await page.addInitScript(() => { window.open = () => null; });
      await page.goto(url().slice(ORIGIN.length)); await screen(page, 'MainMenuScreen');
      await clickMainMenu(page, 'yog'); await screen(page, 'OnlineHubScreen');
      await clickControl(page, 'leaderboard/full');
      const opened = await expectOpened(page, '/leaderboard');
      expect(opened.via).toBe('offered');
      await expect.poll(async () => (await snapshot(page)).opened.at(-1).via).toBe('link');
      await page.bringToFront();
      await clickControl(page, 'back'); await screen(page, 'MainMenuScreen');
    });

    test('a guest signs in through the browser and the hub takes the account', async ({page, baseURL}) => {
      const platform = await online(page, baseURL);
      await clickControl(page, 'account/signin');
      await clickControl(page, 'signin/page');
      await expectOpened(page, '/signin');
      expect(platform.requests).toContain('auth.handoff.begin');
      // The player finishes in the other tab and comes back (a hidden game pauses).
      await page.bringToFront();
      // The browser page finishes: the platform tells the game.
      platform.attempt.done = true;
      platform.socket.send(JSON.stringify({type:'event', event:'auth.handoff.completed',
        data:{attemptId:'attempt-1', linked:true, session:{account:registered, tokens:tokens('ana')}}}));
      // A registered account has no Sign in button, and the sign-in panel closed.
      await expect.poll(async () => {
        const controls = (await snapshot(page)).controls;
        return !controls['account/signin'] && !controls['signin/cancel'] && Boolean(controls['leaderboard/full']);
      }, {timeout:30000}).toBe(true);
      await clickControl(page, 'back'); await screen(page, 'MainMenuScreen');
    });
  });
}
