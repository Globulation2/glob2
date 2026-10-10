# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: artwork-memory.spec.js >> threaded high-resolution startup fits the browser heap on a many-core device
- Location: tests/artwork-memory.spec.js:7:3

# Error details

```
Error: expect(received).toBe(expected) // Object.is equality

Expected: true
Received: false

Call Log:
- Timeout 60000ms exceeded while waiting on the predicate
```

# Page snapshot

```yaml
- generic [ref=e1]:
  - generic [aria-hidden]:
    - status: Starting the game…
    - generic:
      - progressbar
      - generic: 42 of 42 MB
    - combobox
  - generic "Globulation 2" [active] [ref=e2]
```

# Test source

```ts
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
  122 |   let key;
  123 |   await expect.poll(async () => {
  124 |     const rows = await page.evaluate(list => {
  125 |       const controls = glob2Diagnostics.snapshot().controls;
  126 |       return Object.entries(controls).filter(([key]) => key.startsWith(list + '/')).map(([key, value]) => ({key, label: value.label}));
  127 |     }, list);
  128 |     const row = typeof match === 'number' ? rows.find(row => row.key === list + '/' + match)
  129 |       : rows.find(row => match instanceof RegExp ? match.test(row.label) : row.label === match);
  130 |     key = row?.key;
  131 |     return Boolean(key);
  132 |   }, {timeout: options.timeout || 30000, message: `row ${match} of list "${list}" is not available`}).toBe(true);
  133 |   return exports.clickControl(page, key, options);
  134 | };
  135 | 
  136 | exports.gameURL = () => {
  137 |   const entry = process.env.GLOB2_TEST_ENTRY_PATH || '/';
  138 |   const renderer = process.env.GLOB2_TEST_RENDERER;
  139 |   if (!renderer) return entry;
  140 |   if (!['software', 'webgl2'].includes(renderer)) {
  141 |     throw new Error('Unknown test renderer: ' + renderer);
  142 |   }
  143 |   const url = new URL(entry, 'http://localhost');
  144 |   url.searchParams.set('renderer', renderer);
  145 |   return url.pathname + url.search;
  146 | };
  147 | 
  148 | const mainMenuCodes = {campaign:0, tutorial:1, load:2, custom:3, yog:4, lan:5, settings:6, editor:7, credits:8, quit:9};
  149 | exports.clickMainMenu = async (page, action) => {
  150 |   const code = mainMenuCodes[action];
  151 |   if (code === undefined) throw new Error('Unknown main menu action: ' + action);
  152 |   const keys = action === 'settings' ? ['menu/6', 'menu/settings'] : ['menu/' + code];
  153 |   const present = () => page.evaluate(keys => {
  154 |     const controls = glob2Diagnostics.snapshot().controls;
  155 |     return keys.find(key => controls[key]) || (controls['menu/more'] ? 'menu/more' : null);
  156 |   }, keys);
  157 |   await expect.poll(present, {timeout: 30000, message: 'main menu is not showing'}).not.toBeNull();
  158 |   let key = await present();
  159 |   // Phones keep the utilities one level away behind More.
  160 |   if (key === 'menu/more' && !keys.includes('menu/more')) {
  161 |     await exports.clickControl(page, 'menu/more');
  162 |     key = keys[0];
  163 |   }
  164 |   return exports.clickControl(page, key);
  165 | };
  166 | 
  167 | exports.clickSettingsDone = page => exports.clickControl(page, 'done');
  168 | // Leaves Settings without claiming a durable save. Changes save as they are made,
  169 | // so the footer has only Done; a failed save adds "continue" (SettingsScreen::
  170 | // abandon()), which closes at once. Done after a failed restore fails the same
  171 | // way and offers it.
  172 | exports.clickSettingsCancel = async page => {
  173 |   await control(page, 'done', {enabled: false});
  174 |   // A save that has just failed shows "continue" at the next layout.
  175 |   let offered = false;
  176 |   for (const end = Date.now() + 2000; !offered && Date.now() < end; await page.waitForTimeout(100))
  177 |     offered = await page.evaluate(() => Boolean(glob2Diagnostics.snapshot().controls.cancel));
  178 |   if (!offered) await exports.clickControl(page, 'done');
  179 |   return exports.clickControl(page, 'cancel');
  180 | };
  181 | 
  182 | // The lobby's Start ignores input while its preview is pending; wait for the
  183 | // same ready state the lobby publishes.
  184 | exports.clickCustomGameStart = async page => {
  185 |   await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().customGameReady),
> 186 |     {timeout: 60000}).toBe(true);
      |                       ^ Error: expect(received).toBe(expected) // Object.is equality
  187 |   return exports.clickControl(page, 'start');
  188 | };
  189 | // The AI profile picker behind a colony's info button on the Players & Teams tab.
  190 | exports.clickCustomAIProfile = (page, colony) => exports.clickControl(page, `colony/${colony}/info`);
  191 | 
  192 | // Exercise the visible editing surface with actual mouse and keyboard events.
  193 | // Filling the DOM value directly would miss focus, deletion and key routing bugs.
  194 | // The browser input exists while a field is being edited, so start with the
  195 | // field's own control (by key) as a player would.
  196 | exports.editTextField = async (page, value, password = false, key = password ? 'password' : 'name') => {
  197 |   await exports.clickControl(page, key);
  198 |   const field = page.locator(password ? 'input[aria-label="Password"]' : 'input[aria-label="Game text field"]');
  199 |   await field.click();
  200 |   await expect(field).toBeFocused();
  201 |   // Use the platform's native Select All shortcut; Home/End caret behavior
  202 |   // differs between macOS Firefox and the other browser/platform pairs.
  203 |   await field.press('ControlOrMeta+A');
  204 |   await field.press('Delete');
  205 |   await expect(field).toHaveValue('');
  206 |   await field.pressSequentially(value);
  207 |   await expect(field).toHaveValue(value);
  208 |   if (password) await expect(field).toHaveAttribute('type', 'password');
  209 | };
  210 | 
  211 | exports.clickResultsSave = page => exports.clickControl(page, 'save-replay');
  212 | 
  213 | // Export binary evidence compactly: tracing a JS number per byte can consume
  214 | // minutes and hundreds of MB for a replay. This leaves the exact bytes intact.
  215 | exports.readBrowserFile = async (page,path) => Buffer.from(await page.evaluate(path=>{
  216 |   const data=FS.readFile(path),parts=[];
  217 |   for(let at=0;at<data.length;at+=32768)
  218 |     parts.push(String.fromCharCode(...data.subarray(at,at+32768)));
  219 |   return btoa(parts.join(''));
  220 | },path),'base64');
  221 | 
```