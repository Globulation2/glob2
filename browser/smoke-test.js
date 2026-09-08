// Evaluate this function with a Playwright Page while the local server is running.
async (page) => {
  const result = {};
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  await page.setViewportSize({width:1200, height:900});
  const open = async () => {
    await page.goto('http://127.0.0.1:8765/');
    await page.waitForFunction(() => window.Module?.glob2Screen?.includes('MainMenuScreen'));
  };
  const click = async (x, y) => {
    await page.locator('#canvas').click({position:{x,y}, delay:80});
  };
  // Native menus use a centered 640x480 coordinate system.
  const menu = async (x, y) => click(x + 280, y + 210);
  const screen = async name => page.waitForFunction(name => Module.glob2Screen?.includes(name), name);
  await open();
  result.page = await page.evaluate(() => ({
    buttons:document.querySelectorAll('button').length,
    visibleText:document.body.innerText,
    canvas:[Module.canvas.width,Module.canvas.height],
    viewport:[innerWidth,innerHeight],
  }));
  if (result.page.buttons || result.page.visibleText.trim()) throw Error('Page contains wrapper UI');
  if (result.page.canvas.join() !== result.page.viewport.join()) throw Error('Game does not fill viewport');
  await menu(480,200);
  await screen('CustomGameScreen');
  await menu(100,70);
  await menu(530,380);
  await page.waitForFunction(() => Module.glob2Tick > 25);
  const before = await page.evaluate(() => ({tick:Module.glob2Tick, time:performance.now()}));
  await page.waitForFunction(t => Module.glob2Tick >= t+125, before.tick, {timeout:15000});
  const after = await page.evaluate(() => ({tick:Module.glob2Tick, time:performance.now()}));
  result.ticksPerSecond = (after.tick-before.tick)*1000/(after.time-before.time);
  await page.locator('#canvas').press('p', {delay:80});
  await page.waitForTimeout(150);
  const paused = await page.evaluate(() => Module.glob2Tick);
  await page.waitForTimeout(250);
  if (await page.evaluate(() => Module.glob2Tick) !== paused) throw Error('Pause did not stop simulation');
  result.pauseTick = paused;
  await page.locator('#canvas').press('Escape', {delay:80});
  // In-game overlay: 320x260, centered in the viewport.
  await click(600,400);
  // Save overlay: 300x275, text field 190px from its top.
  await click(600,515);
  await page.locator('#canvas').press('Home');
  for (let i=0;i<40;i++) await page.locator('#canvas').press('Delete');
  await page.locator('#canvas').pressSequentially('Browser smoke', {delay:20});
  await click(520,555);
  const save = '/home/web_user/.glob2/games/Browser_smoke.game';
  await page.waitForFunction(path => FS.analyzePath(path).exists, save);
  await page.waitForFunction(() => !Module.saveMount.idbPersistState);
  const hash = async () => page.evaluate(async path => {
    const data = FS.readFile(path);
    const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', data));
    return {size:data.length,sha256:Array.from(digest).map(n=>n.toString(16).padStart(2,'0')).join('')};
  }, save);
  result.saved = await hash();
  await open();
  result.restored = await hash();
  if (result.saved.sha256 !== result.restored.sha256) throw Error('Persistent save changed after reload');
  // Derive the smoke save row without removing other experiment saves.
  const row = await page.evaluate(() => FS.readdir('/home/web_user/.glob2/games')
    .filter(n=>n.endsWith('.game')).sort((a,b)=>a.replaceAll('_',' ').localeCompare(b.replaceAll('_',' '),undefined,{numeric:true,sensitivity:'base'})).indexOf('Browser_smoke.game'));
  await menu(160,200);
  await screen('ChooseMapScreen');
  // The bundled standard font renders list rows at 16px.
  await menu(100,70 + row*16);
  await menu(530,380);
  await page.waitForFunction(t => Module.glob2Tick >= t, paused);
  result.loadedTick = await page.evaluate(() => Module.glob2Tick);
  result.audio = await page.evaluate(() => Module.SDL2?.audioContext?.state);
  result.errors = errors;
  if (errors.length) throw Error(errors.join('\n'));
  return result;
}
