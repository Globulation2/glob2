# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: recording.spec.js >> threaded interrupted recording recovers committed fragments
- Location: tests/recording.spec.js:75:2

# Error details

```
Error: control "nav.8" is not available

control "nav.8" is not available

expect(received).toBe(expected) // Object.is equality

Expected: true
Received: false

Call Log:
- Timeout 30000ms exceeded while waiting on the predicate
```

# Page snapshot

```yaml
- generic [ref=e1]:
  - generic [aria-hidden]:
    - strong: Globulation 2
    - status: Starting the game…
    - generic:
      - progressbar
      - generic: 26 of 26 MB
  - generic "Globulation 2" [active] [ref=e2]
```

# Test source

```ts
  1   | // Screens and dialogs publish their interactive controls, keyed by the same
  2   | // stable keys the native harnesses use, with bounds in logical pixels (see
  3   | // ApplicationHost::controlsChanged and glob2Diagnostics.snapshot().controls).
  4   | // Tests drive those real controls rather than mirroring layout arithmetic.
  5   | const {expect} = require('@playwright/test');
  6   | // Layout runs inside the game's animation frame; two frames after a resize or
  7   | // rebuild the published bounds are current.
  8   | const settled = page => page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  9   | // The logical surface always fills the window at one uniform scale, so bounds
  10  | // published for a previous window size (their surface has another aspect
  11  | // ratio) are stale after a resize and must not be clicked.
  12  | function current(page, bounds) {
  13  |   const {width, height} = page.viewportSize();
  14  |   return Math.abs(width / bounds.surface.w - height / bounds.surface.h) < 0.02;
  15  | }
  16  | async function control(page, key, {timeout = 30000, enabled = true} = {}) {
  17  |   let found;
  18  |   await expect.poll(async () => {
  19  |     found = await page.evaluate(key => glob2Diagnostics.snapshot().controls[key] || null, key);
  20  |     return Boolean(found && (!enabled || found.enabled) && current(page, found));
> 21  |   }, {timeout, message: `control "${key}" is not available`}).toBe(true);
      |                                                               ^ Error: control "nav.8" is not available
  22  |   await settled(page);
  23  |   return (await page.evaluate(key => glob2Diagnostics.snapshot().controls[key] || null, key)) || found;
  24  | }
  25  | // The first control whose key matches and whose text is the label shown.
  26  | exports.clickByLabel = async (page, keyPattern, label, options = {}) => {
  27  |   const source = keyPattern.source;
  28  |   let key;
  29  |   await expect.poll(async () => {
  30  |     key = await page.evaluate(({source, label}) => {
  31  |       const pattern = new RegExp(source);
  32  |       for (const [key, value] of Object.entries(glob2Diagnostics.snapshot().controls))
  33  |         if (pattern.test(key) && value.label.toLowerCase() === label.toLowerCase()) return key;
  34  |       return null;
  35  |     }, {source, label});
  36  |     return Boolean(key);
  37  |   }, {timeout: options.timeout || 30000, message: `no control matching ${source} labelled "${label}"`}).toBe(true);
  38  |   return exports.clickControl(page, key, options);
  39  | };
  40  | // Logical → CSS pixels: the canvas fills the window at the logical surface size,
  41  | // scaled by any interface scale setting.
  42  | function css(page, bounds, point) {
  43  |   const {width, height} = page.viewportSize();
  44  |   const sx = width / bounds.surface.w, sy = height / bounds.surface.h;
  45  |   return {x: point.x * sx, y: point.y * sy};
  46  | }
  47  | function center(page, bounds) {
  48  |   return css(page, bounds, {x: bounds.x + bounds.w / 2, y: bounds.y + bounds.h / 2});
  49  | }
  50  | exports.control = control;
  51  | // The control's bounds in CSS pixels, for pixel checks near a known control.
  52  | exports.controlBox = async (page, key, options) => {
  53  |   const bounds = await control(page, key, {enabled: false, ...options});
  54  |   const origin = css(page, bounds, {x: bounds.x, y: bounds.y});
  55  |   const corner = css(page, bounds, {x: bounds.x + bounds.w, y: bounds.y + bounds.h});
  56  |   return {x: origin.x, y: origin.y, width: corner.x - origin.x, height: corner.y - origin.y};
  57  | };
  58  | // The panel of the host owning a control, in CSS pixels.
  59  | exports.rootBox = async (page, key) => {
  60  |   const bounds = await control(page, key, {enabled: false});
  61  |   const origin = css(page, bounds, {x: bounds.root.x, y: bounds.root.y});
  62  |   const corner = css(page, bounds, {x: bounds.root.x + bounds.root.w, y: bounds.root.y + bounds.root.h});
  63  |   return {x: origin.x, y: origin.y, width: corner.x - origin.x, height: corner.y - origin.y};
  64  | };
  65  | // Scroll regions publish every row and card, including ones scrolled out of
  66  | // the window; wheel over the owning panel until the control is on screen.
  67  | // A control's "visible" rect is what its scroll regions leave on screen.
  68  | // Game frames are scheduled by the host, not by requestAnimationFrame, so a
  69  | // scroll shows up in the published bounds only after the next game frame.
  70  | const same = (a, b) => a.x === b.x && a.y === b.y && a.w === b.w && a.h === b.h;
  71  | const shown = bounds => bounds.visible && bounds.visible.w >= Math.min(bounds.w, 8) && bounds.visible.h >= Math.min(bounds.h, 8);
  72  | async function controlOnScreen(page, key, options) {
  73  |   let bounds = await control(page, key, options);
  74  |   for (let attempt = 0; attempt < 60; ++attempt) {
  75  |     if (shown(bounds)) {
  76  |       // Wait until the bounds hold still before clicking.
  77  |       await page.waitForTimeout(150);
  78  |       const again = await control(page, key, options);
  79  |       if (same(again, bounds) && same(again.visible, bounds.visible)) return bounds;
  80  |       bounds = again;
  81  |       continue;
  82  |     }
  83  |     const root = css(page, bounds, {x: bounds.root.x + bounds.root.w / 2, y: bounds.root.y + bounds.root.h / 2});
  84  |     await page.mouse.move(root.x, root.y);
  85  |     await page.mouse.wheel(0, bounds.y + bounds.h / 2 < bounds.root.y + bounds.root.h / 2 ? -120 : 120);
  86  |     await page.waitForTimeout(150);
  87  |     bounds = await control(page, key, options);
  88  |   }
  89  |   throw new Error(`control "${key}" cannot be scrolled into view`);
  90  | }
  91  | // Click the on-screen part of the control.
  92  | function visibleCenter(page, bounds) {
  93  |   const v = shown(bounds) ? bounds.visible : bounds;
  94  |   return css(page, bounds, {x: v.x + v.w / 2, y: v.y + v.h / 2});
  95  | }
  96  | // Input is consumed at the game's next host frame; give that frame a moment
  97  | // to run so a test's next step (a resize, say) cannot overtake the click. A
  98  | // test that holds the loader pauses frames, so this never blocks for long.
  99  | async function consumed(page) {
  100 |   const loop = () => page.evaluate(() => glob2Diagnostics.snapshot().loop);
  101 |   const before = await loop();
  102 |   const deadline = Date.now() + 500;
  103 |   while (Date.now() < deadline) {
  104 |     if ((await loop()) > before) return;
  105 |     await page.waitForTimeout(20);
  106 |   }
  107 | }
  108 | exports.consumed = consumed;
  109 | exports.clickControl = async (page, key, options = {}) => {
  110 |   const bounds = await controlOnScreen(page, key, options);
  111 |   await page.locator('#canvas').click({position: visibleCenter(page, bounds), delay: 80, ...(options.click || {})});
  112 |   await consumed(page);
  113 | };
  114 | exports.tapControl = async (page, key, options = {}) => {
  115 |   const bounds = await control(page, key, options);
  116 |   const at = center(page, bounds);
  117 |   await page.touchscreen.tap(at.x, at.y);
  118 |   await consumed(page);
  119 | };
  120 | // List rows are published as "<list>/<index>" with the text they show.
  121 | exports.clickListRow = async (page, list, match, options = {}) => {
```