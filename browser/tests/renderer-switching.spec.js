// SPDX-License-Identifier: GPL-3.0-or-later
const {test,expect}=require('@playwright/test');
const {gameURL,clickMainMenu,clickControl,clickByLabel,clickSettingsDone,clickCustomGameStart,controlBox}=require('./main-menu');
const {clickCreateMap}=require('./editor-controls');
const {hasDarkText}=require('./pixels');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>{const s=await state(page);return s.screen+' '+s.screenClass;},{timeout:120000}).toContain(name);
const generation=page=>page.evaluate(()=>Module.glob2PresentationSurface.generation);
async function choose(page,label) {
  const before={generation:await generation(page),loop:(await state(page)).loop};
  await clickControl(page,'graphics.renderer');
  await clickByLabel(page,/popup/,label);
  // In threaded builds FS belongs to the application worker. The requested
  // choice alone is also insufficient: a failed switch retains that choice.
  // Surface generation advances only after successful transaction commit.
  await expect.poll(()=>{if(page.rendererSwitchErrors?.length)throw Error(page.rendererSwitchErrors.join('\n'));return generation(page);},{timeout:120000}).toBeGreaterThan(before.generation);
  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['graphics.renderer']?.label)).toContain(label);
  const active={'Software':'software','Programmable GL':'webgl2','WebGPU':'webgpu'}[label];
  if(!active)throw Error('Missing active renderer expectation for '+label);
  await expect.poll(()=>page.evaluate(()=>Module.renderer),{timeout:120000}).toBe(active);
  await expect.poll(async()=>(await state(page)).loop,{timeout:120000}).toBeGreaterThan(before.loop+1);
  await expect.poll(async()=>hasDarkText(page,await controlBox(page,'graphics.renderer')),{timeout:120000}).toBe(true);
  await expect(page.locator('#canvas')).toHaveCount(1);
  expect(await page.evaluate(()=>document.querySelector('#canvas').isConnected)).toBe(true);
}
async function start(page,mode) {
  const errors=[];page.rendererSwitchErrors=errors;page.on('pageerror',error=>errors.push(String(error)));
  const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads',mode);url.searchParams.set('renderer','software');
  await page.goto(url.pathname+url.search);await screen(page,'MainMenuScreen');
  const execution=(await state(page)).executionMode;
  test.skip(execution!==mode,'Browser does not support the requested execution mode');
  await page.evaluate(()=>{globalThis.rendererSwitchApplicationToken={module:Module,action:Module._glob2_context_action,starts:Module.glob2ApplicationStarts,thread:Module.glob2ApplicationThreadIdentity};globalThis.rendererSwitchToken=rendererSwitchApplicationToken;});
  return async()=>{
    expect(await page.evaluate(()=>rendererSwitchToken===rendererSwitchApplicationToken&&rendererSwitchToken.module===Module&&rendererSwitchToken.action===Module._glob2_context_action)).toBe(true);
    expect(await page.evaluate(()=>({starts:Module.glob2ApplicationStarts,thread:Module.glob2ApplicationThreadIdentity}))).toEqual(await page.evaluate(()=>({starts:rendererSwitchToken.starts,thread:rendererSwitchToken.thread})));
    expect(await page.evaluate(()=>Module.glob2ApplicationStarts)).toBe(1);
    expect((await state(page)).executionMode).toBe(execution);
    expect(errors).toEqual([]);
  };
}
async function openMenu(page) {
  if (!(await state(page)).controls.return) await page.locator('#canvas').press('Escape',{delay:80});
  await expect.poll(async()=>Boolean((await state(page)).controls.return)).toBe(true);
}
async function openOverlay(page) {
  await openMenu(page);
  await clickControl(page,'settings');await screen(page,'SettingsScreen');
}
async function resumeOverlay(page) {
  await clickSettingsDone(page);
  // Settings closes the menu before opening. Wait for the parent screen to
  // resume and clear its old input before sending a new physical key.
  await expect.poll(async()=>(await state(page)).screenClass).not.toContain('SettingsScreen');
  const loop=(await state(page)).loop;
  await expect.poll(async()=>(await state(page)).loop,{timeout:120000}).toBeGreaterThan(loop+2);
  if ((await state(page)).controls.return) await clickControl(page,'return');
}
async function match(page) {
  await clickMainMenu(page,'custom');
  await clickControl(page,'generator/colonies');await clickByLabel(page,/popup/,'2');
  for(const dimension of ['width','height']) {
    await clickControl(page,'generator/'+dimension);await clickByLabel(page,/popup/,'128');
  }
  await clickCustomGameStart(page);
  await expect.poll(async()=>(await state(page)).tick,{timeout:120000}).toBeGreaterThan(25);
}
async function available(page,label) {
  await clickControl(page,'graphics.renderer');
  const offered=await page.evaluate(label=>Object.entries(glob2Diagnostics.snapshot().controls).some(([key,value])=>/popup/.test(key)&&value.label===label),label);
  await page.locator('#canvas').press('Escape',{delay:80});
  if (label!=='WebGPU') return offered;
  // Compiled shaders alone do not establish an adapter. In threaded builds
  // inspect the application worker that owns the backend, not the DOM realm.
  let reason=offered?'Presentation-owner WebGPU realm could not be inspected':'Renderer selector reports WebGPU unavailable';
  // Idle Emscripten asset pthreads may be inside Atomics.wait forever. Probe
  // all realms with bounded waits before sending async work to the GPU owner.
  const threaded=(await state(page)).executionMode==='threaded';
  const identity=await page.evaluate(()=>Module.glob2ApplicationThreadIdentity);
  // The DOM page and idle asset workers load the same libraries. Only the
  // application pthread owns renderer initialization; Software deliberately
  // has no Module.canvas in that worker, so canvas presence is not a predicate.
  const realms=threaded?page.workers():[page];
  const owners=await Promise.all(realms.map(async realm=>{
    let timer;
    try{return await Promise.race([
      realm.evaluate(({threaded,identity})=>
        typeof Glob2WebGPU==='object'&&typeof Module==='object'&&
        (threaded
          ? typeof ENVIRONMENT_IS_PTHREAD!=='undefined'&&ENVIRONMENT_IS_PTHREAD&&
            typeof _pthread_self==='function'&&_pthread_self()===identity
          : typeof ENVIRONMENT_IS_PTHREAD==='undefined'||!ENVIRONMENT_IS_PTHREAD),
      {threaded,identity}).catch(()=>false),
      new Promise(resolve=>{timer=setTimeout(()=>resolve(false),5000);})
    ]);}finally{clearTimeout(timer);}
  }));
  for (let index=0;index<realms.length;++index) {
    if(!owners[index])continue;
    const realm=realms[index];
    let timer,result;
    try{result=await Promise.race([realm.evaluate(async()=>{
      if(!globalThis.navigator?.gpu)return {available:false,reason:'Presentation owner has no navigator.gpu'};
      try{return await navigator.gpu.requestAdapter() ? {available:true} : {available:false,reason:'Presentation owner returned no WebGPU adapter'};}
      catch(error){return {available:false,reason:String(error)};}
    }).catch(()=>null),new Promise(resolve=>{timer=setTimeout(()=>resolve({available:false,reason:'Presentation-owner adapter request timed out'}),15000);})]);}
    finally{clearTimeout(timer);}
    if(!result)continue;
    if(result.available){if(offered)return true;break;}
    reason=result.reason;break;
  }
  page.rendererBackendUnavailableReason=reason;
  test.info().annotations.push({type:'WebGPU coverage omitted',description:reason});
  return false;
}
for(const mode of ['serial','threaded']) {
  test(`${mode}: Automatic resolving to the active Software renderer completes without replacing the surface`,async({page},info)=>{
    test.setTimeout(360000);const verify=await start(page,mode);await match(page);await openOverlay(page);
    // direct0 keeps Software as its initial automatic policy. Capture the actual
    // presentation object as well as its generation to detect redundant teardown.
    expect(await page.evaluate(()=>Module.renderer)).toBe('software');
    const before={generation:await generation(page),loop:(await state(page)).loop};
    await page.evaluate(()=>{globalThis.rendererNoopCanvas=Module.canvas;});
    await clickControl(page,'graphics.renderer');await clickByLabel(page,/popup/,'Automatic');
    await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['graphics.renderer']?.label)).toContain('Automatic');
    await expect.poll(async()=>(await state(page)).loop,{timeout:120000}).toBeGreaterThan(before.loop+3);
    expect(await generation(page)).toBe(before.generation);
    expect(await page.evaluate(()=>Module.canvas===rendererNoopCanvas&&Module.renderer==='software')).toBe(true);
    await verify();
    // A subsequent real switch must be admitted: a no-op left in Preparing
    // would stall this request and all following capture admission.
    await choose(page,'Programmable GL');await resumeOverlay(page);
    const completed=()=>page.evaluate(async()=>{
      const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true});
      const paths=[];for await(const [name,handle]of root.entries())if(name.endsWith('.complete')&&(await handle.getFile()).size)paths.push(name);return paths;
    });
    const previousRecordings=new Set(await completed());
    await openMenu(page);
    await clickControl(page,'recording/toggle');
    await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Stop recording/i);
    await clickControl(page,'return');const tick=(await state(page)).tick;
    await expect.poll(async()=>(await state(page)).tick,{timeout:120000}).toBeGreaterThan(tick+10);
    await openMenu(page);
    await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Stop recording/i);
    await clickControl(page,'recording/toggle');
    await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).not.toMatch(/Stop recording/i);
    await expect.poll(async()=>(await completed()).filter(path=>!previousRecordings.has(path)).length,{timeout:60000}).toBe(1);
    const recording=(await completed()).find(path=>!previousRecordings.has(path));
    const metadata=await page.evaluate(async name=>{
      const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings');
      const file=await(await root.getFileHandle(name.slice(0,-9)+'.json')).getFile();
      return JSON.parse(await file.text());
    },recording);
    expect(metadata.complete).toBe(true);expect(metadata.width).toBeGreaterThan(0);expect(metadata.height).toBeGreaterThan(0);
    await info.attach('automatic-noop-recording-metadata',{body:JSON.stringify(metadata,null,2),contentType:'application/json'});
    await page.screenshot({path:info.outputPath('automatic-noop-following-capture.png')});await verify();
  });
  test(`${mode}: repeated menu switches preserve Wasm lifetime and input`,async({page},info)=>{
    test.setTimeout(360000);const verify=await start(page,mode);
    await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
    for(let cycle=0;cycle<3;++cycle) {
      await choose(page,'Programmable GL');
      await page.screenshot({path:info.outputPath('gl-'+cycle+'.png')});
      await choose(page,'Software');
    }
    await clickSettingsDone(page);await screen(page,'MainMenuScreen');
    await match(page);await page.setViewportSize({width:1280,height:720});
    // A single physical key opens exactly one menu after repeated replacement.
    await page.locator('#canvas').press('Escape',{delay:80});await clickControl(page,'return');
    const tick=(await state(page)).tick;
    await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(tick);
    await verify();
  });
  test(`${mode}: gameplay and torus survive settings overlay switches`,async({page},info)=>{
    test.setTimeout(360000);const verify=await start(page,mode);await match(page);
    const tick=(await state(page)).tick;
    await openOverlay(page);await choose(page,'Programmable GL');await resumeOverlay(page);
    await expect.poll(async()=>(await state(page)).tick,{timeout:120000}).toBeGreaterThan(tick);
    await page.locator('#canvas').press('g',{delay:80});
    await expect.poll(async()=>(await state(page)).torusSettled,{timeout:120000}).toBe(true);
    await page.screenshot({path:info.outputPath('torus-before-switch.png')});
    await openOverlay(page);await choose(page,'Software');await resumeOverlay(page);
    await expect.poll(async()=>(await state(page)).torus).toBe(false);
    await openOverlay(page);await choose(page,'Programmable GL');await resumeOverlay(page);
    await page.locator('#canvas').press('g',{delay:80});
    await expect.poll(async()=>(await state(page)).torusSettled,{timeout:120000}).toBe(true);
    await page.screenshot({path:info.outputPath('torus-after-switch.png')});await verify();
  });
  test(`${mode}: editor stays open across live renderer switches and resize`,async({page},info)=>{
    test.setTimeout(360000);const verify=await start(page,mode);
    await clickMainMenu(page,'editor');await screen(page,'EditorMainMenu');await clickControl(page,'new-map');
    await screen(page,'NewMapScreen');await clickCreateMap(page);await screen(page,'MapEditorScreen');
    for(const label of ['Programmable GL','Software']) {
      await openOverlay(page);await choose(page,label);await resumeOverlay(page);await screen(page,'MapEditorScreen');
      const viewport={width:label==='Software'?1200:1001,height:label==='Software'?900:701};
      await page.setViewportSize(viewport);
      // Diagnostics retain controls from suspended screen hosts. Inspect the
      // active editor menu rather than the retained EditorMainMenu's bounds.
      await openMenu(page);
      await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls.settings?.surface),
        {timeout:120000}).toEqual({w:viewport.width,h:viewport.height});
      await clickControl(page,'return');
      await page.screenshot({path:info.outputPath('editor-'+label+'.png')});
    }
    await openMenu(page);await clickControl(page,'quit');
    await clickControl(page,'choice/1');await screen(page,'EditorMainMenu');await verify();
  });
  test(`${mode}: recording retains one session while changing backend`,async({page},info)=>{
    test.setTimeout(360000);const verify=await start(page,mode);await match(page);
    const completed=()=>page.evaluate(async()=>{
      const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true});
      const paths=[];for await(const [name,handle]of root.entries())if(name.endsWith('.complete')&&(await handle.getFile()).size)paths.push(decodeURIComponent(name).slice(0,-9));return paths;
    });
    const existing=new Set(await completed());
    await openMenu(page);await clickControl(page,'recording/toggle');
    await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Stop recording/i);
    await clickControl(page,'return');const started=(await state(page)).tick;
    await expect.poll(async()=>(await state(page)).tick,{timeout:120000}).toBeGreaterThan(started+10);
    await openOverlay(page);
    await choose(page,'Programmable GL');await choose(page,'Software');await resumeOverlay(page);
    const resumed=(await state(page)).tick;
    await expect.poll(async()=>(await state(page)).tick,{timeout:120000}).toBeGreaterThan(resumed+10);
    await openMenu(page);
    await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Stop recording/i);
    await clickControl(page,'recording/toggle');
    await expect.poll(async()=>(await completed()).filter(path=>!existing.has(path)).length,{timeout:60000}).toBe(1);
    const path=(await completed()).find(path=>!existing.has(path));
    const metadata=await page.evaluate(async path=>{const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings');return JSON.parse(await(await(await root.getFileHandle(encodeURIComponent(path+'.json'))).getFile()).text());},path);
    expect(metadata.complete).toBe(true);expect(metadata.width).toBe(1200);expect(metadata.height).toBe(900);
    const chapters=metadata.chapters.filter(chapter=>chapter.screen==='game_session');
    expect(chapters.length).toBeGreaterThan(1);
    const identity=chapters[0];expect(identity.match).toBeGreaterThan(0);
    for(const chapter of chapters) {
      expect(chapter.match).toBe(identity.match);expect(chapter.mode).toBe(identity.mode);
      expect(chapter.map).toBe(identity.map);expect(chapter.team).toBe(identity.team);
      expect(chapter.tick).toBeGreaterThanOrEqual(identity.tick);
    }
    await info.attach('recording-switch-metadata',{body:JSON.stringify(metadata,null,2),contentType:'application/json'});await verify();
  });
  test(`${mode}: all available WebGPU pairs retain application ownership`,async({page},info)=>{
    test.setTimeout(480000);const verify=await start(page,mode);
    await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
    test.skip(!await available(page,'WebGPU'),'WebGPU assets or browser/device capability unavailable');
    for(const label of ['Programmable GL','WebGPU','Software','WebGPU','Programmable GL','Software'])await choose(page,label);
    await page.screenshot({path:info.outputPath('all-backend-pairs.png')});await verify();
  });
  test(`${mode}: current GL context restores after earlier canvas replacements`,async({page},info)=>{
    test.setTimeout(360000);const verify=await start(page,mode);await match(page);
    for(const label of ['Programmable GL','Software','Programmable GL']){await openOverlay(page);await choose(page,label);await resumeOverlay(page);}
    const before=await state(page);
    await page.evaluate(()=>Module._glob2_context_action(0));await expect.poll(async()=>(await state(page)).contextLost).toBe(true);
    await page.evaluate(()=>Module._glob2_context_action(1));
    await expect.poll(async()=>(await state(page)).contextRestores,{timeout:120000}).toBeGreaterThan(before.contextRestores);
    await expect.poll(async()=>(await state(page)).tick,{timeout:120000}).toBeGreaterThan(before.tick);
    await page.screenshot({path:info.outputPath('restored-current-context.png')});await verify();
  });
  test(`${mode}: real WebGPU device loss falls back during gameplay`,async({page},info)=>{
    test.setTimeout(360000);const verify=await start(page,mode);await match(page);await openOverlay(page);
    test.skip(!await available(page,'WebGPU'),'WebGPU assets or browser/device capability unavailable');
    await choose(page,'WebGPU');await resumeOverlay(page);
    const before={generation:await generation(page),tick:(await state(page)).tick};
    await page.evaluate(()=>Module._glob2_context_action(2));
    await expect.poll(()=>{if(page.rendererSwitchErrors?.length)throw Error(page.rendererSwitchErrors.join('\n'));return generation(page);},{timeout:120000}).toBeGreaterThan(before.generation);
    await expect.poll(async()=>(await state(page)).tick,{timeout:120000}).toBeGreaterThan(before.tick);
    await openOverlay(page);
    expect(await page.evaluate(()=>glob2Diagnostics.snapshot().controls['graphics.renderer']?.label)).toContain('WebGPU'); // Successful preference survives automatic fallback.
    await page.screenshot({path:info.outputPath('device-loss-fallback.png')});await resumeOverlay(page);await verify();
  });
  test(`${mode}: presentation surface creation failure restores the running session`,async({page},info)=>{
    test.setTimeout(360000);const verify=await start(page,mode);await match(page);await openOverlay(page);
    const before={generation:await generation(page),loop:(await state(page)).loop};
    await page.evaluate(()=>{const surface=Module.glob2PresentationSurface,original=surface.prepare;Module.rendererFaultConsumed=0;Module.rendererSurfaceCleanup=()=>{surface.prepare=original;};surface.prepare=function(...args){surface.prepare=original;Module.rendererFaultConsumed++;throw Error('injected presentation surface creation failure');};});
    try {
      await clickControl(page,'graphics.renderer');await clickByLabel(page,/popup/,'Programmable GL');
      await expect.poll(()=>page.evaluate(()=>Module.rendererFaultConsumed)).toBe(1);
      await expect.poll(async()=>(await state(page)).loop,{timeout:120000}).toBeGreaterThan(before.loop+4);
      expect(await generation(page)).toBe(before.generation);
      expect(await page.evaluate(()=>Module.renderer)).toBe('software');
      await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['graphics.renderer']?.label)).toContain('Programmable GL');
      await page.screenshot({path:info.outputPath('surface-creation-rollback.png')});
      await choose(page,'Programmable GL');await choose(page,'Software');await verify();
    } finally { await page.evaluate(()=>{Module.rendererSurfaceCleanup?.();delete Module.rendererSurfaceCleanup;delete Module.rendererFaultConsumed;}); }
  });
  test(`${mode}: canvas replacement preserves active fullscreen host`,async({page},info)=>{
    test.setTimeout(360000);const verify=await start(page,mode);
    test.skip(!await page.evaluate(()=>document.fullscreenEnabled),'Fullscreen unavailable on browser/device');
    await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
    await page.locator('#canvas').click({position:{x:5,y:5}});
    await page.evaluate(()=>Module.glob2PresentationSurface.fullscreenHost.requestFullscreen());
    await expect.poll(()=>page.evaluate(()=>document.fullscreenElement===Module.glob2PresentationSurface.fullscreenHost)).toBe(true);
    for(const label of ['Programmable GL','Software']) {
      await choose(page,label);
      expect(await page.evaluate(()=>document.fullscreenElement===Module.glob2PresentationSurface.fullscreenHost)).toBe(true);
    }
    await page.screenshot({path:info.outputPath('fullscreen-replaced-canvas.png')});await page.evaluate(()=>document.exitFullscreen());await verify();
  });
}

// Serial-only API interception exercises the actual shaders, uploads and
// presentation calls. The application worker owns these APIs in threaded mode;
// the shared surface failure and device-loss cases above cover its public bridge.
test('serial: shader upload presentation and rollback failures preserve the session',async({page},info)=>{
  test.setTimeout(900000);const verify=await start(page,'serial');await match(page);await openOverlay(page);
  test.skip(!await available(page,'WebGPU'),'WebGPU unavailable on browser/device');
  test.skip(!await page.evaluate(()=>typeof FS!=='undefined'&&typeof Glob2WebGPU!=='undefined'&&typeof GL!=='undefined'),'Serial graphics owner APIs unavailable');
  await choose(page,'Programmable GL');
  const shaderPath='/shaders/webgpu/primitive.vert.wgsl';
  await page.evaluate(path=>{Module.rendererShaderOriginal=FS.readFile(path,{encoding:'utf8'});FS.writeFile(path,'this is deliberately invalid WGSL');},shaderPath);
  try {
    await clickControl(page,'graphics.renderer');await clickByLabel(page,/popup/,'WebGPU');
    await expect.poll(()=>page.evaluate(()=>Glob2WebGPU.state),{timeout:120000}).toBe(-1);
    expect(await page.evaluate(()=>Module.renderer)).toBe('webgl2');
    await page.screenshot({path:info.outputPath('shader-creation-rollback.png')});
  } finally {await page.evaluate(path=>{FS.writeFile(path,Module.rendererShaderOriginal);delete Module.rendererShaderOriginal;},shaderPath);}
  await choose(page,'WebGPU');await choose(page,'Programmable GL');
  // Device preparation is retained between backend switches. Fail the actual
  // white/restoration upload once and let the previous GL renderer recover.
  await page.evaluate(()=>{const queue=Glob2WebGPU.device.queue,original=queue.writeTexture;Module.rendererFaultConsumed=0;Module.rendererFaultCleanup=()=>{queue.writeTexture=original;};queue.writeTexture=function(...args){queue.writeTexture=original;Module.rendererFaultConsumed++;throw Error('injected owned upload failure');};});
  try {
    const loop=(await state(page)).loop;await clickControl(page,'graphics.renderer');await clickByLabel(page,/popup/,'WebGPU');
    await expect.poll(()=>page.evaluate(()=>Module.rendererFaultConsumed),{timeout:120000}).toBe(1);
    await expect.poll(async()=>(await state(page)).loop,{timeout:120000}).toBeGreaterThan(loop+4);
    expect(await page.evaluate(()=>Module.renderer)).toBe('webgl2');await page.screenshot({path:info.outputPath('upload-rollback.png')});
  } finally {await page.evaluate(()=>{Module.rendererFaultCleanup?.();delete Module.rendererFaultCleanup;});}
  await choose(page,'WebGPU');
  await page.evaluate(()=>{const context=Glob2WebGPU.context,original=context.getCurrentTexture;Module.rendererFaultConsumed=0;Module.rendererFaultCleanup=()=>{context.getCurrentTexture=original;};context.getCurrentTexture=function(...args){context.getCurrentTexture=original;Module.rendererFaultConsumed++;throw Error('injected presentation failure');};});
  try {
    await expect.poll(()=>page.evaluate(()=>Module.rendererFaultConsumed),{timeout:120000}).toBe(1);
    await expect.poll(()=>page.evaluate(()=>Glob2WebGPU.state),{timeout:120000}).toBe(-1);
    await expect.poll(()=>page.evaluate(()=>Module.renderer),{timeout:120000}).toBe('webgl2');
    await page.screenshot({path:info.outputPath('presentation-fallback.png')});
  } finally {await page.evaluate(()=>{Module.rendererFaultCleanup?.();delete Module.rendererFaultCleanup;});}
  // Restore a healthy prepared device, then fail both the new device's upload
  // and the previous GL context recreation. Software must complete the rollback.
  await choose(page,'WebGPU');await choose(page,'Programmable GL');
  await page.evaluate(()=>{const queue=Glob2WebGPU.device.queue,upload=queue.writeTexture,create=GL.createContext;Module.rendererFaultConsumed=0;Module.rendererRollbackConsumed=0;Module.rendererFaultCleanup=()=>{queue.writeTexture=upload;GL.createContext=create;};queue.writeTexture=function(){queue.writeTexture=upload;Module.rendererFaultConsumed++;throw Error('injected new device upload failure');};GL.createContext=function(){GL.createContext=create;Module.rendererRollbackConsumed++;return 0;};});
  try {
    await clickControl(page,'graphics.renderer');await clickByLabel(page,/popup/,'WebGPU');
    await expect.poll(()=>page.evaluate(()=>Module.rendererFaultConsumed),{timeout:120000}).toBe(1);
    await expect.poll(()=>page.evaluate(()=>Module.rendererRollbackConsumed),{timeout:120000}).toBe(1);
    await expect.poll(()=>page.evaluate(()=>Module.renderer),{timeout:120000}).toBe('software');
    await page.screenshot({path:info.outputPath('failed-previous-device-software-rollback.png')});
  } finally {await page.evaluate(()=>{Module.rendererFaultCleanup?.();delete Module.rendererFaultCleanup;delete Module.rendererFaultConsumed;delete Module.rendererRollbackConsumed;});}
  await choose(page,'Programmable GL');await choose(page,'Software');await resumeOverlay(page);
  const tick=(await state(page)).tick;await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(tick);await verify();
});
