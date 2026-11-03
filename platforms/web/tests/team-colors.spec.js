const {waitForMainMenu}=require('./main-menu');
const {test, expect} = require('@playwright/test');
const {clickMainMenu,clickControl,clickListRow}=require('./main-menu');

// Inspect the first colored icon in the top bar: the worker sprite. Its x
// position follows measured counters, so locate its ink instead of assuming the
// old fixed-width layout. The 18px sample excludes the adjacent counter text.
// The first tutorial uses the standard red team. This catches a hue shift
// caused by decoding BGRA sprite bytes with an RGBA display format.
async function workerColors(page, width) {
  const png = await page.screenshot({clip:{x:0,y:0,width,height:16}});
  return page.evaluate(async base64 => {
    const bitmap = await createImageBitmap(await (await fetch('data:image/png;base64,'+base64)).blob());
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    const context = canvas.getContext('2d');
    context.drawImage(bitmap,0,0); bitmap.close();
    const pixels = context.getImageData(0,0,canvas.width,canvas.height).data;
    let first=-1;
    for (let x=0;x<canvas.width && first<0;x++) {
      for (let y=0;y<canvas.height;y++) {
        const i=(y*canvas.width+x)*4, [r,g,b]=pixels.slice(i,i+3);
        // Locate any saturated ink, independent of the expected team hue.
        if (Math.max(r,g,b)>80 && Math.max(r,g,b)-Math.min(r,g,b)>30) {first=x;break;}
      }
    }
    let red=0, purple=0;
    if (first>=0) for (let x=first;x<Math.min(first+18,canvas.width);x++) {
      for (let y=0;y<canvas.height;y++) {
        const i=(y*canvas.width+x)*4, [r,g,b]=pixels.slice(i,i+3);
        if (r>80 && r>g*1.5 && r>b*1.5) red++;
        if (r>80 && b>80 && r>g*1.5 && b>g*1.5) purple++;
      }
    }
    return {red,purple,first};
  }, png.toString('base64'));
}

for (const renderer of ['software','webgl2']) {
  test(`${renderer} preserves the red tutorial team before and after resize`, async ({page}, info) => {
    if (renderer === 'webgl2') test.setTimeout(120000);
    const screen = name => name==='MainMenuScreen' ? waitForMainMenu(page) : expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen),
      {timeout: renderer === 'webgl2' ? 60000 : 30000}).toContain(name);
    const entry = new URL(process.env.GLOB2_TEST_ENTRY_PATH || '/', 'http://localhost');
    entry.searchParams.set('renderer',renderer);
    await page.goto(entry.pathname+entry.search);
    await screen('MainMenuScreen');
    expect(await page.evaluate(() => glob2Diagnostics.snapshot().renderer)).toBe(renderer);
    await clickMainMenu(page,'tutorial'); await screen('LessonCatalogScreen');
    await clickControl(page,'tutorial/lesson/0');
    await screen('match');
    for (const width of [1200,1280]) {
      await page.setViewportSize({width,height:900});
      await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().width)).toBe(width);
      let colors;
      await expect.poll(async () => {
        colors = await workerColors(page,width);
        return colors.red;
      }, {timeout:15000}).toBeGreaterThan(10);
      expect(colors.purple, JSON.stringify(colors)).toBe(0);
    }
    await page.screenshot({path:info.outputPath('red-tutorial-team.png')});
  });
}
