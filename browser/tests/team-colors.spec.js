const {test, expect} = require('@playwright/test');
const {clickMainMenu,clickControl,clickListRow}=require('./main-menu');

// Inspect the presented worker sprite, excluding the adjacent colored text.
// The first tutorial uses the standard red team. This catches a hue shift
// caused by decoding BGRA sprite bytes with an RGBA display format.
async function workerColors(page, width) {
  const png = await page.screenshot({clip:{x:((width-640)>>2)+12,y:0,width:18,height:16}});
  return page.evaluate(async base64 => {
    const bitmap = await createImageBitmap(await (await fetch('data:image/png;base64,'+base64)).blob());
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    const context = canvas.getContext('2d');
    context.drawImage(bitmap,0,0); bitmap.close();
    const pixels = context.getImageData(0,0,canvas.width,canvas.height).data;
    let red=0, purple=0;
    for (let i=0;i<pixels.length;i+=4) {
      const [r,g,b] = pixels.slice(i,i+3);
      if (r>80 && r>g*1.5 && r>b*1.5) red++;
      if (r>80 && b>80 && r>g*1.5 && b>g*1.5) purple++;
    }
    return {red,purple};
  }, png.toString('base64'));
}

for (const renderer of ['software','webgl2']) {
  test(`${renderer} preserves the red tutorial team before and after resize`, async ({page}, info) => {
    if (renderer === 'webgl2') test.setTimeout(120000);
    const screen = name => expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen),
      {timeout: renderer === 'webgl2' ? 60000 : 30000}).toContain(name);
    const entry = new URL(process.env.GLOB2_TEST_ENTRY_PATH || '/', 'http://localhost');
    entry.searchParams.set('renderer',renderer);
    await page.goto(entry.pathname+entry.search);
    await screen('MainMenuScreen');
    expect(await page.evaluate(() => glob2Diagnostics.snapshot().renderer)).toBe(renderer);
    await clickMainMenu(page,'tutorial'); await screen('CampaignMenuScreen');
    await clickListRow(page,'missions',0); await clickControl(page,'start');
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
