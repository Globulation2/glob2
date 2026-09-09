const {expect} = require('@playwright/test');

// Canvas dimensions are published before the queued resize batch is consumed.
// Wait for presentation before sending fresh input: that batch intentionally
// cancels stale input, including Escape, to preserve the shared lifecycle policy.
async function resizeAndWait(page, size) {
  await page.setViewportSize(size);
  await expect.poll(() => page.evaluate(() => ({width:glob2Diagnostics.snapshot().width, height:glob2Diagnostics.snapshot().height}))).toEqual(size);
  const frames = await page.evaluate(() => glob2Diagnostics.snapshot().hostFrames);
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().hostFrames)).toBeGreaterThan(frames + 1);
}
module.exports = {resizeAndWait};
