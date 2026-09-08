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
