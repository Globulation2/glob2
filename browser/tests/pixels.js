const {PNG} = require('pngjs');

// Inspect the captured presentation in the runner. Sending the PNG back into
// the game page makes pixel assertions wait for its rendering/GPU work again.
// The normal screenshot calls below capture opaque, presented sRGB pixels.
const read = png => PNG.sync.read(png);

// WebGL's non-preserved drawing buffer may be cleared between frames.
async function hasRenderedPixels(page) {
  const {data} = read(await page.locator('#canvas').screenshot());
  return data.some((value, index) => index % 4 !== 3 && value > 16);
}

// Restrict to the inside of a text box, excluding its border and controls.
async function hasLightText(page, clip, minimumChannel = 200) {
  const {data} = read(await page.screenshot({clip}));
  let light = 0;
  for (let i = 0; i < data.length; i += 4)
    if (data[i] > minimumChannel && data[i+1] > minimumChannel && data[i+2] > minimumChannel) ++light;
  return light > 50;
}

// Dark text on the redesigned Settings/CustomGame paper panels.
async function hasDarkText(page, clip, maximumChannel = 120) {
  const {data} = read(await page.screenshot({clip}));
  let dark = 0;
  for (let i = 0; i < data.length; i += 4)
    if (data[i] < maximumChannel && data[i+1] < maximumChannel && data[i+2] < maximumChannel) ++dark;
  return dark > 50;
}

async function darkShare(page, clip) {
  const {data} = read(await page.screenshot({clip}));
  let dark = 0;
  for (let i = 0; i < data.length; i += 4)
    if (data[i] < 130 && data[i+1] < 130 && data[i+2] < 130) ++dark;
  return dark / (data.length / 4);
}

// The classic in-game dialog buttons: dark text on the gold button sprite.
async function hasClassicButton(page, clip) {
  const {data} = read(await page.screenshot({clip}));
  let gold = 0;
  for (let i = 0; i < data.length; i += 4)
    if (data[i] > 170 && data[i+1] > 140 && data[i+2] < 190 && data[i] > data[i+2] + 30) ++gold;
  return gold > 50;
}

// Keep the page argument for callers sharing the screenshot helpers' API.
async function comparePresentedRGB(page, firstPNG, secondPNG) {
  const first = read(firstPNG), second = read(secondPNG);
  if (first.width !== second.width || first.height !== second.height)
    throw new Error('Presented screenshot dimensions changed');
  let maximumChannelDifference = 0, affectedPixels = 0;
  for (let i = 0; i < first.data.length; i += 4) {
    let affected = false;
    for (let channel = 0; channel < 3; ++channel) {
      const difference = Math.abs(first.data[i + channel] - second.data[i + channel]);
      maximumChannelDifference = Math.max(maximumChannelDifference, difference);
      affected ||= difference > 2;
    }
    if (affected) ++affectedPixels;
  }
  return {maximumChannelDifference, affectedPixels};
}

module.exports = {hasRenderedPixels, hasLightText, hasDarkText, darkShare, hasClassicButton, comparePresentedRGB};
