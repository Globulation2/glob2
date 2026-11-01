// Run against an actual ready desktop settings page. No source/runtime hooks.
const fs = require('node:fs/promises');
const path = require('node:path');
async function pixels(page, png) {
  return page.evaluate(async base64 => {
    const bitmap = await createImageBitmap(await (await fetch('data:image/png;base64,' + base64)).blob());
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    const context = canvas.getContext('2d');
    context.drawImage(bitmap, 0, 0); bitmap.close();
    return Array.from(context.getImageData(0, 0, canvas.width, canvas.height).data);
  }, png.toString('base64'));
}
async function checkOpaquePanel(page, outputDirectory) {
  await fs.mkdir(outputDirectory, {recursive: true});
  const state = await page.evaluate(() => glob2Diagnostics.snapshot());
  if (!state.screen.includes('SettingsScreen')) throw new Error('Open actual settings before probing canvas alpha');
  const navigation = Object.entries(state.controls).filter(([id]) => /^nav\.\d+$/.test(id)).map(([,v]) => v.visible);
  if (!navigation.length) throw new Error('Desktop navigation crop is unavailable; do not substitute a different scene');
  const x = Math.min(...navigation.map(r => r.x)), y = Math.min(...navigation.map(r => r.y));
  const right = Math.max(...navigation.map(r => r.x+r.w)), bottom = Math.max(...navigation.map(r => r.y+r.h));
  const clip = {x, y, width: right-x, height: bottom-y};
  const originalStyle = await page.locator('#canvas').getAttribute('style');
  const captures = [];
  try {
    await page.mouse.move(1, 1);
    for (const [label, color] of [['black', 'rgb(0,0,0)'], ['white', 'rgb(255,255,255)']]) {
      await page.evaluate(color => { document.querySelector('#canvas').style.backgroundColor = color; }, color);
      await page.waitForTimeout(250);
      const png = await page.screenshot({clip});
      await fs.writeFile(path.join(outputDirectory, label + '.png'), png);
      captures.push(await pixels(page, png));
    }
  } finally {
    await page.evaluate(style => {
      const canvas = document.querySelector('#canvas');
      if (style === null) canvas.removeAttribute('style'); else canvas.setAttribute('style', style);
    }, originalStyle);
  }
  if (captures[0].length !== captures[1].length) throw new Error('Capture dimensions changed during probe');
  let maximum = 0, affectedPixels = 0, sum = 0;
  for (let i=0; i<captures[0].length; i+=4) {
    let affected = false;
    for (let c=0;c<3;c++) {
      const difference = Math.abs(captures[0][i+c] - captures[1][i+c]);
      maximum = Math.max(maximum, difference); sum += difference;
      if (difference > 2) affected = true;
    }
    if (affected) affectedPixels++;
  }
  const report = {renderer:state.renderer, executionMode:state.executionMode, clip,
    maximumChannelDifference:maximum, affectedPixels, totalAbsoluteChannelDifference:sum,
    inferredMinimumOpaquePanelAlpha:1-maximum/255, passed:maximum<=2,
    reason:'CSS behind an opaque presented panel must not change its text/icon pixels'};
  await fs.writeFile(path.join(outputDirectory, 'result.json'), JSON.stringify(report,null,2)+'\n');
  if (!report.passed) { const error = new Error('Presented opaque panel leaks canvas background: '+JSON.stringify(report)); error.report=report; throw error; }
  return report;
}
module.exports = {checkOpaquePanel};
