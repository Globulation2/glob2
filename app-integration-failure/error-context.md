# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: smoke.spec.ts >> phones: no sideways scrolling at 320 and 430 px, 44 px touch targets
- Location: e2e/smoke.spec.ts:404:1

# Error details

```
Error: /skins at 320 px has small touch targets

expect(received).toEqual(expected) // deep equality

- Expected  - 1
+ Received  + 3

- Array []
+ Array [
+   "INPUT \"\" 240x27",
+ ]
```

# Page snapshot

```yaml
- generic [ref=f6e2]:
  - banner [ref=f6e3]:
    - link "Back to Globulation 2" [ref=f6e4] [cursor=pointer]:
      - /url: /
    - generic [ref=f6e7]:
      - generic [ref=f6e8]: COLONY STUDIO
      - textbox "Skin name" [ref=f6e9]: My colony
      - status [ref=f6e10]: Ready
    - generic [ref=f6e11]:
      - button "Undo" [disabled] [ref=f6e12]
      - button "Redo" [disabled] [ref=f6e15]
    - navigation "Studio" [ref=f6e18]:
      - button "My skins" [ref=f6e19] [cursor=pointer]
      - button "Shop" [ref=f6e20] [cursor=pointer]
      - button "Skin settings" [ref=f6e21] [cursor=pointer]
      - button "Save" [ref=f6e25] [cursor=pointer]
      - button "Publish" [ref=f6e28] [cursor=pointer]
  - main "Skin designer" [ref=f6e29]:
    - generic "Paint directly on the 3D worker model" [ref=f6e31]
    - group "Model to paint" [ref=f6e32]:
      - button "Worker" [pressed] [ref=f6e33] [cursor=pointer]
      - button "Warrior" [ref=f6e35] [cursor=pointer]
      - button "Explorer" [ref=f6e37] [cursor=pointer]
      - button "Swarm" [ref=f6e39] [cursor=pointer]
    - complementary "Paint tools" [ref=f6e41]:
      - generic [ref=f6e42]:
        - generic [ref=f6e43]: ⠿ TOOLBOX
        - button "Collapse toolbox" [ref=f6e44] [cursor=pointer]: −
      - generic [ref=f6e45]:
        - button "Brush" [pressed] [ref=f6e46] [cursor=pointer]
        - button "Eraser" [ref=f6e51] [cursor=pointer]
        - button "Eyedropper" [ref=f6e56] [cursor=pointer]
        - button "Orbit" [ref=f6e60] [cursor=pointer]
        - button "Patterns" [ref=f6e65] [cursor=pointer]
      - generic [ref=f6e69]:
        - group "Brush paints" [ref=f6e70]:
          - button "Color" [pressed] [ref=f6e71] [cursor=pointer]
          - button "Material" [ref=f6e72] [cursor=pointer]
        - generic [ref=f6e73]:
          - textbox "Paint color" [ref=f6e74] [cursor=pointer]: "#ed9252"
          - strong [ref=f6e76]: Paint color
        - generic [ref=f6e77]:
          - text: Brush size
          - generic [ref=f6e78]: "36"
          - slider "Brush size" [ref=f6e79]: "36"
        - generic [ref=f6e80]:
          - text: Opacity
          - generic [ref=f6e81]: 100%
          - slider "Opacity" [ref=f6e82]: "1"
        - generic [ref=f6e83]:
          - text: Hardness
          - generic [ref=f6e84]: 80%
          - slider "Hardness" [ref=f6e85]: "0.8"
        - generic [ref=f6e86]:
          - checkbox "Pen pressure" [ref=f6e87]
          - text: Pen pressure
        - group [ref=f6e88]:
          - generic "Paint repeats on matching surfaces" [ref=f6e89] [cursor=pointer]
    - generic [ref=f6e90]:
      - combobox "Camera view" [ref=f6e91] [cursor=pointer]:
        - option "View…" [disabled] [selected]
        - option "Zoom in (+)"
        - option "Zoom out (−)"
        - option "Front"
        - option "Back"
        - option "Left"
        - option "Right"
        - option "Top"
        - option "Bottom"
      - button "Fit model" [ref=f6e92] [cursor=pointer]
      - button "Show game view" [ref=f6e96] [cursor=pointer]
    - generic [ref=f6e100]:
      - group "Pose" [ref=f6e101]:
        - button "Walk" [pressed] [ref=f6e102] [cursor=pointer]
        - button "Swim" [ref=f6e104] [cursor=pointer]
        - button "Harvest" [ref=f6e106] [cursor=pointer]
      - button "Play animation" [ref=f6e108] [cursor=pointer]
      - slider "Frame" [ref=f6e112]: "0"
    - generic: Drag to paint · Orbit tool to turn · scroll or pinch to zoom
```

# Test source

```ts
  348 |   if (mobile)
  349 |     await page.locator('.mobile-bar').getByRole('button', { name: 'Open navigation' }).click();
  350 |   const toggle = page
  351 |     .locator(mobile ? '.drawer-content' : '.app-sidebar')
  352 |     .getByTestId('theme-toggle');
  353 |   const bg = () => inPage<string>(page, 'getComputedStyle(document.body).backgroundColor');
  354 |   await expect(toggle).toHaveAccessibleName(/same as this device/);
  355 |   await expect.poll(bg).toBe('rgb(241, 241, 225)');
  356 |   await toggle.click();
  357 |   await expect(toggle).toHaveAccessibleName(/light/);
  358 |   await toggle.click();
  359 |   await expect(toggle).toHaveAccessibleName(/dark/);
  360 |   await expect.poll(bg).toBe('rgb(27, 18, 41)');
  361 |   await page.reload();
  362 |   expect(await inPage(page, 'document.documentElement.dataset.theme')).toBe('dark');
  363 |   await expect.poll(bg).toBe('rgb(27, 18, 41)');
  364 |   if (mobile)
  365 |     await page.locator('.mobile-bar').getByRole('button', { name: 'Open navigation' }).click();
  366 |   await toggle.click();
  367 |   await expect(toggle).toHaveAccessibleName(/same as this device/);
  368 | });
  369 | 
  370 | test('keyboard: skip link first, visible focus, focus moves to new pages', async ({
  371 |   page,
  372 | }, info) => {
  373 |   test.skip(info.project.name !== 'desktop', 'keyboard navigation is a desktop check');
  374 |   await page.goto('/');
  375 |   await page.keyboard.press('Tab');
  376 |   const skip = page.getByRole('link', { name: 'Skip to content' });
  377 |   await expect(skip).toBeFocused();
  378 |   await expect(skip).toBeInViewport();
  379 |   const outline = await inPage(page, 'getComputedStyle(document.activeElement).outlineStyle');
  380 |   expect(outline).toBe('solid');
  381 |   await page.keyboard.press('Enter');
  382 |   await expect(page.locator('#main')).toBeFocused();
  383 |   await page.getByRole('navigation', { name: 'Main' }).getByRole('link', { name: 'Maps' }).click();
  384 |   await expect(page.locator('#main')).toBeFocused();
  385 | });
  386 | 
  387 | test('the colony moves, can be paused, and keeps still for reduced motion', async ({ page }) => {
  388 |   await page.emulateMedia({ reducedMotion: 'no-preference' });
  389 |   await page.goto('/');
  390 |   const walker = "getComputedStyle(document.querySelector('.walker'))";
  391 |   const animation = () => inPage<string>(page, `${walker}.animationName`);
  392 |   const state = () => inPage<string>(page, `${walker}.animationPlayState`);
  393 |   expect(await animation()).toBe('cross');
  394 |   expect(await state()).toBe('running');
  395 |   await page.getByRole('button', { name: /Pause the globs/ }).click();
  396 |   expect(await state()).toBe('paused');
  397 |   await page.getByRole('button', { name: /Let the globs roam/ }).click();
  398 |   expect(await state()).toBe('running');
  399 |   await page.emulateMedia({ reducedMotion: 'reduce' });
  400 |   expect(await animation()).toBe('none');
  401 |   await expect(page.getByRole('button', { name: /Pause the globs/ })).toBeHidden();
  402 | });
  403 | 
  404 | test('phones: no sideways scrolling at 320 and 430 px, 44 px touch targets', async ({
  405 |   page,
  406 | }, info) => {
  407 |   test.skip(info.project.name !== 'phone', 'phone layout check');
  408 |   await signIn(page, seed.adminSession);
  409 |   const paths = [
  410 |     '/',
  411 |     '/leaderboard',
  412 |     `/players/${seed.accounts.bradley}`,
  413 |     `/matches/${seed.featuredMatch}`,
  414 |     '/matches',
  415 |     '/maps',
  416 |     '/skins',
  417 |     `/maps/${seed.mapId}`,
  418 |     '/maps/new',
  419 |     '/account',
  420 |     '/admin/accounts',
  421 |     '/admin/reports',
  422 |     `/j/${INVITE_CODE}`,
  423 |     '/signin',
  424 |   ];
  425 |   await page.route('glob2://**', (route) => route.abort());
  426 |   for (const width of [320, 430]) {
  427 |     await page.setViewportSize({ width, height: 800 });
  428 |     for (const path of paths) {
  429 |       await page.goto(path);
  430 |       await page.waitForLoadState('networkidle');
  431 |       const overflow = await inPage<number>(
  432 |         page,
  433 |         'document.documentElement.scrollWidth - window.innerWidth',
  434 |       );
  435 |       expect(overflow, `${path} at ${width} px scrolls sideways`).toBeLessThanOrEqual(0);
  436 |       // Buttons, nav links and form fields: at least 44 x 44 CSS px.
  437 |       // Radio/checkbox labels are the clickable target, including their indicators.
  438 |       const small = await inPage<string[]>(
  439 |         page,
  440 |         `[...new Set([...document.querySelectorAll(
  441 |           'button, .btn, a.button, .nav a, .seg > *, input, select')]
  442 |           .filter((el) => el.offsetParent !== null)
  443 |           .map((el) => el.matches('input[type=radio], input[type=checkbox]') ? el.closest('label') ?? el : el))]
  444 |           .map((el) => ({ el, r: el.getBoundingClientRect() }))
  445 |           .filter(({ r }) => r.height < 43.5 || r.width < 43.5)
  446 |           .map(({ el, r }) => el.tagName + ' "' + el.textContent.trim() + '" ' + r.width + 'x' + r.height)`,
  447 |       );
> 448 |       expect(small, `${path} at ${width} px has small touch targets`).toEqual([]);
      |                                                                       ^ Error: /skins at 320 px has small touch targets
  449 |     }
  450 |   }
  451 | });
  452 | 
  453 | test('watch in browser opens the replay in the browser game', async ({ page }, info) => {
  454 |   const game =
  455 |     process.env['GLOB2_WEB_CLIENT_DIR'] ??
  456 |     join(import.meta.dirname, '../../../../build/emscripten/client/release');
  457 |   test.skip(!existsSync(join(game, 'index.html')), 'no browser game build');
  458 |   test.skip(info.project.name !== 'desktop', 'one browser run is enough');
  459 |   test.setTimeout(180_000);
  460 |   await page.goto(`/matches/${seed.featuredMatch}`);
  461 |   await page.getByTestId('watch').click();
  462 |   await expect(page).toHaveURL(/\/play\/\?replay=/);
  463 |   // glob2Diagnostics is the browser shell's read-only diagnostics (browser/shell.html).
  464 |   const snapshot = () =>
  465 |     page.evaluate(() =>
  466 |       (
  467 |         globalThis as unknown as {
  468 |           glob2Diagnostics?: { snapshot(): { watchReplay: string; screen: string; tick: number } };
  469 |         }
  470 |       ).glob2Diagnostics?.snapshot(),
  471 |     );
  472 |   await expect.poll(async () => (await snapshot())?.watchReplay, { timeout: 90_000 }).toBe('ready');
  473 |   await expect.poll(async () => (await snapshot())?.screen, { timeout: 120_000 }).toBe('match');
  474 |   await expect
  475 |     .poll(async () => (await snapshot())?.tick ?? 0, { timeout: 120_000 })
  476 |     .toBeGreaterThan(25);
  477 | });
  478 | 
```