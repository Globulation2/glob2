import { chromium, firefox, webkit } from '../../platform/node_modules/playwright-core/index.mjs';
import assert from 'node:assert/strict';
for (const [name, engine] of Object.entries({ chromium, firefox, webkit })) {
  const browser = await engine.launch();
  const page = await browser.newPage();
  await page.route('**/api/**', route => route.fulfill({ status: 401, contentType: 'application/json', body: '{}' }));
  await page.goto('http://127.0.0.1:5178/skins');
  await page.getByLabel('Skin name').waitFor();
  for (const [theme, width, height] of [['dark', 1440, 900], ['light', 1440, 900], ['dark', 412, 839], ['dark', 320, 568]]) {
    await page.setViewportSize({ width, height });
    await page.evaluate(theme => document.documentElement.dataset.theme = theme, theme);
    const expand = page.getByRole('button', { name: 'Expand toolbox' });
    if (await expand.isVisible()) await expand.click();
    const summary = page.getByLabel('Choose paint color', { exact: true });
    if (!(await summary.evaluate(el => el.parentElement.open))) await summary.click();
    const plane = page.getByRole('slider', { name: 'Paint color saturation and brightness' });
    await plane.scrollIntoViewIfNeeded();
    const bounds = await plane.boundingBox();
    await page.mouse.move(bounds.x + bounds.width * .2, bounds.y + bounds.height * .2);
    await page.mouse.down();
    await page.mouse.move(bounds.x + bounds.width * .75, bounds.y + bounds.height * .35, { steps: 5 });
    await page.mouse.up();
    const hex = page.getByLabel('Paint color', { exact: true });
    assert.match(await hex.inputValue(), /^#[a-f0-9]{6}$/);
    await hex.fill('#8e52cc');
    await hex.press('Tab');
    assert.equal(await hex.inputValue(), '#8e52cc');
    await plane.focus();
    await plane.press('ArrowDown');
    assert.notEqual(await hex.inputValue(), '#8e52cc');
    assert.equal(await page.locator('input[type=color]').count(), 0);
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await plane.scrollIntoViewIfNeeded();
    await page.screenshot({ path: `artifacts/color-picker/${name}-${theme}-${width}.png` });
    console.log(`${name}: ${theme} ${width}x${height} pointer, keyboard and hex passed`);
  }
  await browser.close();
}
