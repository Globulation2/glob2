// Screenshots capture the presented frame for both 2D and WebGL canvases;
// WebGL's non-preserved drawing buffer may already be cleared between frames.
async function hasRenderedPixels(page) {
  const png = await page.locator('#canvas').screenshot();
  return page.evaluate(async base64 => {
    const blob = await (await fetch('data:image/png;base64,' + base64)).blob();
    const bitmap = await createImageBitmap(blob);
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    const context = canvas.getContext('2d');
    context.drawImage(bitmap,0,0);
    bitmap.close();
    return context.getImageData(0,0,canvas.width,canvas.height).data.some((value,index) => index % 4 !== 3 && value > 16);
  }, png.toString('base64'));
}
module.exports = {hasRenderedPixels};

// Restrict to the inside of a text box, excluding its border and controls.
async function hasLightText(page, clip, minimumChannel = 200) {
  const png = await page.screenshot({clip});
  return page.evaluate(async ({base64, minimumChannel}) => {
    const blob = await (await fetch('data:image/png;base64,' + base64)).blob();
    const bitmap = await createImageBitmap(blob);
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    const context = canvas.getContext('2d'); context.drawImage(bitmap, 0, 0); bitmap.close();
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let light = 0;
    for (let i = 0; i < pixels.length; i += 4)
      if (pixels[i] > minimumChannel && pixels[i+1] > minimumChannel && pixels[i+2] > minimumChannel) ++light;
    return light > 50;
  }, {base64: png.toString('base64'), minimumChannel});
}
module.exports.hasLightText = hasLightText;

// Same idea for the redesigned Settings/CustomGame panels: dark text on a
// pale paper background rather than light text on a dark overlay.
async function hasDarkText(page, clip, maximumChannel = 120) {
  const png = await page.screenshot({clip});
  return page.evaluate(async ({base64, maximumChannel}) => {
    const blob = await (await fetch('data:image/png;base64,' + base64)).blob();
    const bitmap = await createImageBitmap(blob);
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    const context = canvas.getContext('2d'); context.drawImage(bitmap, 0, 0); bitmap.close();
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let dark = 0;
    for (let i = 0; i < pixels.length; i += 4)
      if (pixels[i] < maximumChannel && pixels[i+1] < maximumChannel && pixels[i+2] < maximumChannel) ++dark;
    return dark > 50;
  }, {base64: png.toString('base64'), maximumChannel});
}
module.exports.hasDarkText = hasDarkText;

// Share of a clip darker than those pale panels in every channel.
async function darkShare(page, clip) {
  const png = await page.screenshot({clip});
  return page.evaluate(async base64 => {
    const blob = await (await fetch('data:image/png;base64,' + base64)).blob();
    const bitmap = await createImageBitmap(blob);
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    const context = canvas.getContext('2d'); context.drawImage(bitmap, 0, 0); bitmap.close();
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let dark = 0;
    for (let i = 0; i < pixels.length; i += 4)
      if (pixels[i] < 130 && pixels[i+1] < 130 && pixels[i+2] < 130) ++dark;
    return dark / (pixels.length / 4);
  }, png.toString('base64'));
}
module.exports.darkShare = darkShare;

// The classic in-game dialog buttons: dark text on the gold button sprite.
async function hasClassicButton(page, clip) {
  const png = await page.screenshot({clip});
  return page.evaluate(async base64 => {
    const blob = await (await fetch('data:image/png;base64,' + base64)).blob();
    const bitmap = await createImageBitmap(blob);
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    const context = canvas.getContext('2d'); context.drawImage(bitmap, 0, 0); bitmap.close();
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let gold = 0;
    for (let i = 0; i < pixels.length; i += 4)
      if (pixels[i] > 170 && pixels[i+1] > 140 && pixels[i+2] < 190 && pixels[i] > pixels[i+2] + 30) ++gold;
    return gold > 50;
  }, png.toString('base64'));
}
module.exports.hasClassicButton = hasClassicButton;

// Compare screenshots after presentation; reading a WebGL drawing buffer can
// race its non-preserved contents, especially in the application worker.
async function comparePresentedRGB(page, firstPNG, secondPNG) {
  return page.evaluate(async images => {
    const read = async base64 => {
      const bitmap = await createImageBitmap(await (await fetch('data:image/png;base64,' + base64)).blob());
      const canvas = document.createElement('canvas');
      canvas.width = bitmap.width; canvas.height = bitmap.height;
      const context = canvas.getContext('2d');
      context.drawImage(bitmap, 0, 0); bitmap.close();
      return context.getImageData(0, 0, canvas.width, canvas.height);
    };
    const [first, second] = await Promise.all(images.map(read));
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
  }, [firstPNG.toString('base64'), secondPNG.toString('base64')]);
}
module.exports.comparePresentedRGB = comparePresentedRGB;
