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
async function hasLightText(page, clip) {
  const png = await page.screenshot({clip});
  return page.evaluate(async base64 => {
    const blob = await (await fetch('data:image/png;base64,' + base64)).blob();
    const bitmap = await createImageBitmap(blob);
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    const context = canvas.getContext('2d'); context.drawImage(bitmap, 0, 0); bitmap.close();
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let light = 0;
    for (let i = 0; i < pixels.length; i += 4)
      if (pixels[i] > 200 && pixels[i+1] > 200 && pixels[i+2] > 200) ++light;
    return light > 50;
  }, png.toString('base64'));
}
module.exports.hasLightText = hasLightText;

// Same idea for the redesigned Settings/CustomGame panels: dark text on a
// pale paper background rather than light text on a dark overlay.
async function hasDarkText(page, clip) {
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
      if (pixels[i] < 120 && pixels[i+1] < 120 && pixels[i+2] < 120) ++dark;
    return dark > 50;
  }, png.toString('base64'));
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
