const {test} = require('node:test');
const assert = require('node:assert/strict');
const {PNG} = require('pngjs');
const pixels = require('../tests/pixels');

function png(colors, width = colors.length) {
  const image = new PNG({width, height: colors.length / width});
  colors.forEach((color, index) => image.data.set([...color, 255], index * 4));
  return PNG.sync.write(image);
}
function page(image) {
  return {
    screenshot: async () => image,
    locator: selector => {
      assert.equal(selector, '#canvas');
      return {screenshot: async () => image};
    },
    evaluate: () => { throw new Error('The application thread is unavailable'); },
  };
}
const repeated = (color, count) => Array.from({length: count}, () => color);

test('presented pixels ignore alpha and retain the strict channel floor', async () => {
  assert.equal(await pixels.hasRenderedPixels(page(png([[16, 16, 16]]))), false);
  assert.equal(await pixels.hasRenderedPixels(page(png([[16, 17, 16]]))), true);
});

test('light glyphs require 51 pixels strictly above the threshold in every channel', async () => {
  assert.equal(await pixels.hasLightText(page(png(repeated([201, 201, 201], 50)))), false);
  assert.equal(await pixels.hasLightText(page(png(repeated([201, 201, 201], 51)))), true);
  assert.equal(await pixels.hasLightText(page(png(repeated([255, 200, 255], 51)))), false);
  assert.equal(await pixels.hasLightText(page(png(repeated([151, 151, 151], 51))), null, 150), true);
});

test('dark glyphs retain the 51-pixel and per-channel upper bounds', async () => {
  assert.equal(await pixels.hasDarkText(page(png(repeated([119, 119, 119], 50)))), false);
  assert.equal(await pixels.hasDarkText(page(png(repeated([119, 119, 119], 51)))), true);
  assert.equal(await pixels.hasDarkText(page(png(repeated([0, 120, 0], 51)))), false);
  assert.equal(await pixels.hasDarkText(page(png(repeated([179, 179, 179], 51))), null, 180), true);
});

test('dark share counts pixels rather than matching channels', async () => {
  const image = png([[129, 129, 129], [130, 129, 129], [1, 1, 1], [255, 255, 255]]);
  assert.equal(await pixels.darkShare(page(image)), 0.5);
});

test('classic buttons retain all gold-channel and sample-count limits', async () => {
  assert.equal(await pixels.hasClassicButton(page(png(repeated([191, 150, 160], 50)))), false);
  assert.equal(await pixels.hasClassicButton(page(png(repeated([191, 150, 160], 51)))), true);
  for (const color of [[170, 150, 0], [200, 140, 0], [255, 200, 190], [190, 150, 160]])
    assert.equal(await pixels.hasClassicButton(page(png(repeated(color, 51)))), false);
});

test('RGB comparison counts each changed pixel once and preserves the two-level tolerance', async () => {
  const before = png([[0, 0, 0], [0, 0, 0], [0, 0, 0]]);
  const after = png([[2, 0, 0], [3, 4, 5], [0, 0, 0]]);
  assert.deepEqual(await pixels.comparePresentedRGB(page(before), before, after),
    {maximumChannelDifference: 5, affectedPixels: 1});
});

test('RGB comparison rejects dimension changes even when pixel counts match', async () => {
  const colors = repeated([0, 0, 0], 4);
  await assert.rejects(pixels.comparePresentedRGB(page(png(colors)), png(colors), png(colors, 2)),
    /Presented screenshot dimensions changed/);
});
