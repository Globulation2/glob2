const {waitForMainMenu}=require('./main-menu');
// SPDX-License-Identifier: GPL-3.0-or-later
const {test,expect}=require('@playwright/test');
const {gameURL,clickMainMenu,clickControl,clickByLabel,clickCustomGameStart,clickSettingsDone}=require('./main-menu');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>name==='MainMenuScreen' ? waitForMainMenu(page) : expect.poll(async()=>{const value=await state(page);return value.screen+' '+value.screenClass;},{timeout:120000}).toContain(name);
const surface=page=>page.evaluate(()=>({generation:Module.glob2PresentationSurface.generation,events:globalThis.rendererQueueSurface.events}));
// Sleeping Emscripten asset workers can block DevTools evaluation indefinitely.
// Match the existing application pthread identity; asset workers also contain
// library globals. Software starts without a worker-owned canvas.
async function responsiveRealms(page,predicate,argument){
  return (await Promise.all([page,...page.workers()].map(async realm=>{
    let timer;
    try{return await Promise.race([realm.evaluate(predicate,argument).catch(()=>false),
      new Promise(resolve=>{timer=setTimeout(()=>resolve(false),5000);})])?realm:null;}
    finally{clearTimeout(timer);}
  }))).filter(Boolean);
}
async function select(page,label){await clickControl(page,'graphics.renderer');await clickByLabel(page,/popup/,label);}
async function start(page,mode){
  const errors=[];page.on('pageerror',error=>errors.push(String(error)));
  const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads',mode);url.searchParams.set('renderer','software');
  await page.goto(url.pathname+url.search);await screen(page,'MainMenuScreen');
  test.skip((await state(page)).executionMode!==mode,'Requested application execution mode unavailable');
  await page.evaluate(()=>{
    globalThis.rendererQueueIdentity={module:Module,action:Module._glob2_context_action,starts:Module.glob2ApplicationStarts,thread:Module.glob2ApplicationThreadIdentity};
    const manager=Module.glob2PresentationSurface,prepare=manager.prepare,finish=manager.finish;
    const audit=globalThis.rendererQueueSurface={events:[],cleanup(){manager.prepare=prepare;manager.finish=finish;}};
    manager.prepare=function(...args){audit.events.push({action:'prepare',gpu:args[0].gpu,generation:manager.generation});return prepare.apply(this,args);};
    manager.finish=function(success){const value=finish.call(this,success);audit.events.push({action:'finish',success,generation:manager.generation});return value;};
  });
  return async()=>{
    expect(errors).toEqual([]);expect((await state(page)).executionMode).toBe(mode);
    expect(await page.evaluate(()=>rendererQueueIdentity.module===Module&&rendererQueueIdentity.action===Module._glob2_context_action&&rendererQueueIdentity.starts===Module.glob2ApplicationStarts&&rendererQueueIdentity.thread===Module.glob2ApplicationThreadIdentity)).toBe(true);
    expect(await page.evaluate(()=>Module.glob2ApplicationStarts)).toBe(1);
    await expect(page.locator('#canvas')).toHaveCount(1);
  };
}
async function installGates(page){
  const realms=[];
  const threaded=(await state(page)).executionMode==='threaded';
  const applicationThread=await page.evaluate(()=>Module.glob2ApplicationThreadIdentity);
  for(const realm of await responsiveRealms(page,identity=>typeof Glob2WebGPU==='object'&&typeof Module==='object'&&(typeof ENVIRONMENT_IS_PTHREAD==='undefined'||!ENVIRONMENT_IS_PTHREAD||_pthread_self()===identity),applicationThread)){
    if(threaded&&realm===page)continue;
    await realm.evaluate(()=>{
      const backend=Glob2WebGPU,prepare=backend.prepare;
      let release;
      const held=new Promise(resolve=>{release=resolve;});
      const audit=globalThis.rendererQueueOwner={entered:0,phase:'idle',contexts:[],starts:0,acceptedFrames:0,frames:[],release,cleanup:[]};
      backend.prepare=async function(root,ownedSources){
        backend.prepare=prepare;audit.root=root;audit.entered++;audit.phase='holding';this.state=0.5;
        const fail=await held;
        if(fail){this.fail(Error('injected delayed WebGPU preparation failure'));audit.phase='rejected';return;}
        this.state=0;await prepare.call(this,root,ownedSources);audit.phase=this.state===1?'prepared':'rejected';
        const info=this.adapter?.info;
        audit.adapter=info?{vendor:info.vendor,architecture:info.architecture,device:info.device,description:info.description}:null;
      };
      audit.cleanup.push(()=>{backend.prepare=prepare;release(true);});
      for(const constructor of [globalThis.HTMLCanvasElement,globalThis.OffscreenCanvas])if(constructor){
        const prototype=constructor.prototype,original=prototype.getContext;
        prototype.getContext=function(kind,...args){if(kind==='webgl2'||kind==='webgpu')audit.contexts.push(kind);return original.call(this,kind,...args);};
        audit.cleanup.push(()=>{prototype.getContext=original;});
      }
      const recording=Module.glob2Recording;
      if(recording){
        const start=recording.start,frame=recording.frame;
        recording.start=function(...args){audit.starts++;const result=start.apply(this,args);audit.recordingWorker=this.worker;return result;};
        recording.frame=function(pointer,width,height,time,context){const accepted=frame.call(this,pointer,width,height,time,context);if(accepted){audit.acceptedFrames++;audit.frames.push({time,width,height});if(audit.frames.length>256)audit.frames.shift();}return accepted;};
        audit.cleanup.push(()=>{recording.start=start;recording.frame=frame;});
      }
    });
    realms.push(realm);
  }
  if(!realms.length)test.info().annotations.push({type:'Queue coverage omitted',description:'Presentation owner cannot be inspected through existing browser realms'});
  test.skip(!realms.length,'Presentation owner cannot be inspected through existing browser realms');
  let timer;let adapter;
  try{adapter=await Promise.race([realms[0].evaluate(async()=>Boolean(await navigator.gpu?.requestAdapter())).catch(()=>false),
    new Promise(resolve=>{timer=setTimeout(()=>resolve(false),15000);})]);}
  finally{clearTimeout(timer);}
  if(!adapter)test.info().annotations.push({type:'Queue coverage omitted',description:'Presentation owner has no usable WebGPU adapter or its request timed out'});
  test.skip(!adapter,'Presentation owner has no usable WebGPU adapter or its request timed out');
  return {
    async entered(){let found;await expect.poll(async()=>{for(const realm of realms)if(await realm.evaluate(()=>rendererQueueOwner.entered).catch(()=>0)){found=realm;return true;}return false;},{timeout:120000}).toBe(true);return found;},
    async cleanup(){for(const realm of realms)await realm.evaluate(()=>{for(const cleanup of rendererQueueOwner.cleanup)cleanup();}).catch(()=>{});await page.evaluate(()=>rendererQueueSurface.cleanup());}
  };
}
async function assertSoftwarePreference(page){
  // Pthread filesystem syscalls execute on the main realm; its preloaded FS
  // also owns persisted preferences. A worker-local JS FS is independent.
  const preference=await page.evaluate(()=>{
    const env=typeof ENV==='object'?ENV:{};
    const directories=[env.GLOB2_USER_DATA_DIR,env.GLOB2_USER_DIR,(env.HOME||'/home/web_user')+'/.glob2','/'];
    for(const directory of directories)if(directory)try{return FS.readFile(directory.replace(/\/$/,'')+'/preferences.txt',{encoding:'utf8'});}catch(_){}
    throw Error('Saved renderer preference file unavailable');
  });
  expect(preference).toMatch(/^rendererBackend=software$/m);
}
async function supersede(page,gates,fail){
  const before=await surface(page);
  await select(page,'WebGPU');const owner=await gates.entered();
  expect(await owner.evaluate(()=>rendererQueueOwner.phase)).toBe('holding');
  expect(await surface(page)).toEqual(before);
  // These are real UI callbacks while the active transaction waits. Only the
  // newest unstarted choice may run after that transaction settles.
  await select(page,'Programmable GL');await select(page,'Software');
  expect((await surface(page)).generation).toBe(before.generation);
  await owner.evaluate(fail=>rendererQueueOwner.release(fail),fail);
  const shaderAudit=await owner.evaluate(()=>{
    const root=rendererQueueOwner.root;
    let metadata;try{metadata=FS.readFile(root+'/../metadata.json',{encoding:'utf8'}).length;}catch(error){metadata=String(error);}
    return {root,metadata,phase:rendererQueueOwner.phase,adapter:rendererQueueOwner.adapter,
      state:Glob2WebGPU.state,error:Glob2WebGPU.error};
  });
  const mainMetadata=await page.evaluate(root=>{
    try{return FS.readFile(root+'/../metadata.json',{encoding:'utf8'}).length;}catch(error){return String(error);}
  },shaderAudit.root);
  await test.info().attach('shader-filesystem-owner',{body:JSON.stringify({...shaderAudit,mainMetadata}),contentType:'application/json'});
  const transitions=fail?0:2;
  try { await expect.poll(async()=>(await surface(page)).generation,{timeout:120000}).toBe(before.generation+transitions); }
  catch(error){
    const failure=await owner.evaluate(()=>({phase:rendererQueueOwner.phase,state:Glob2WebGPU.state,error:Glob2WebGPU.error})).catch(e=>({inspectionError:String(e)}));
    await test.info().attach('prepare-failure-state',{body:JSON.stringify(failure),contentType:'application/json'});
    throw error;
  }
  const loop=(await state(page)).loop;
  await expect.poll(async()=>(await state(page)).loop,{timeout:120000}).toBeGreaterThan(loop+4);
  expect(await owner.evaluate(()=>rendererQueueOwner.contexts.filter(kind=>kind==='webgl2'))).toEqual([]);
  const events=(await surface(page)).events.slice(before.events.length);
  if(fail)expect(events).toEqual([]);
  else{
    expect(events.map(event=>event.action)).toEqual(['prepare','finish','prepare','finish']);
    expect(events.filter(event=>event.action==='prepare').map(event=>event.gpu)).toEqual([true,false]);
    expect(events.filter(event=>event.action==='finish').every(event=>event.success)).toBe(true);
    expect(await owner.evaluate(()=>rendererQueueOwner.contexts)).toContain('webgpu');
    await test.info().attach('prepared-webgpu-adapter',{body:JSON.stringify(await owner.evaluate(()=>rendererQueueOwner.adapter)),contentType:'application/json'});
  }
  expect(await page.evaluate(()=>Module.renderer)).toBe('software');await assertSoftwarePreference(page);
  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['graphics.renderer']?.label),{timeout:120000}).toContain('Software');
  return owner;
}
const completed=page=>page.evaluate(async()=>{
  const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true});
  const paths=[];for await(const [name,handle]of root.entries())if(name.endsWith('.complete')&&(await handle.getFile()).size)paths.push(decodeURIComponent(name).slice(0,-9));return paths;
});
for(const mode of ['serial','threaded']){
  test(`${mode}: viewport requests survive a retired surface and coalesce to the latest size`,async({page},info)=>{
    test.setTimeout(240000);const verify=await start(page,mode);
    await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
    const applicationThread=await page.evaluate(()=>Module.glob2ApplicationThreadIdentity);
    const realms=await responsiveRealms(page,identity=>typeof Glob2SurfaceBridge==='object'&&typeof Module==='object'&&(typeof ENVIRONMENT_IS_PTHREAD==='undefined'||!ENVIRONMENT_IS_PTHREAD||_pthread_self()===identity),applicationThread);
    const owner=realms.find(realm=>mode==='serial'?realm===page:realm!==page);
    if(!owner)test.info().annotations.push({type:'Queue coverage omitted',description:'Presentation owner cannot be inspected through existing browser realms'});
    test.skip(!owner,'Presentation owner cannot be inspected through existing browser realms');
    await owner.evaluate(()=>{
      const bridge=Glob2SurfaceBridge,original=bridge.prepare;
      globalThis.rendererViewportHandover={entered:false,release(){bridge.prepare=original;return original.apply(bridge,this.arguments);}};
      bridge.prepare=function(...args){
        rendererViewportHandover.entered=true;rendererViewportHandover.arguments=args;
        this.state=0;return 0;
      };
    });
    const before=(await surface(page)).generation;
    await select(page,'Programmable GL');
    await expect.poll(()=>owner.evaluate(()=>rendererViewportHandover.entered),{timeout:120000}).toBe(true);
    // The real switch has retired SDL's window before calling this bridge.
    // Let each size reach an application turn while no window can accept it.
    for(const viewport of [{width:1001,height:701},{width:1103,height:733}]){
      const loop=(await state(page)).loop;await page.setViewportSize(viewport);
      await expect.poll(async()=>(await state(page)).loop,{timeout:120000}).toBeGreaterThan(loop+3);
      expect((await surface(page)).generation).toBe(before);
    }
    await owner.evaluate(()=>rendererViewportHandover.release());
    await expect.poll(async()=>(await surface(page)).generation,{timeout:120000}).toBe(before+1);
    const logical=()=>page.evaluate(()=>Object.values(glob2Diagnostics.snapshot().controls).find(value=>value.surface)?.surface);
    await expect.poll(logical,{timeout:120000}).toMatchObject({w:1103,h:733});
    const loop=(await state(page)).loop;
    await expect.poll(async()=>(await state(page)).loop,{timeout:120000}).toBeGreaterThan(loop+5);
    expect(await logical()).toMatchObject({w:1103,h:733});
    await info.attach('resized-surface',{body:JSON.stringify({logical:await logical(),surface:await surface(page)}),contentType:'application/json'});
    await verify();
  });
  test(`${mode}: delayed WebGPU preparation commits before the newest queued choice`,async({page},info)=>{
    test.setTimeout(480000);const verify=await start(page,mode);
    await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
    const gates=await installGates(page);
    try{const owner=await supersede(page,gates,false);await info.attach('serialized-surface-events',{body:JSON.stringify(await surface(page),null,2),contentType:'application/json'});expect(await owner.evaluate(()=>rendererQueueOwner.entered)).toBe(1);await verify();}
    finally{await gates.cleanup();}
  });
  test(`${mode}: rejected preparation keeps one recording session and its timeline`,async({page},info)=>{
    test.setTimeout(480000);const verify=await start(page,mode);
    await clickMainMenu(page,'custom');
    await clickControl(page,'generator/colonies');await clickByLabel(page,/popup/,'2');
    await clickControl(page,'generator/width');await clickByLabel(page,/popup/,'128');
    await clickControl(page,'generator/height');await clickByLabel(page,/popup/,'128');
    await clickCustomGameStart(page);
    await expect.poll(async()=>(await state(page)).tick,{timeout:120000}).toBeGreaterThan(25);
    const existing=new Set(await completed(page));const gates=await installGates(page);
    try{
      await page.locator('#canvas').press('Escape',{delay:80});await clickControl(page,'recording/toggle');
      await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Stop recording/i);
      await clickControl(page,'return');const tick=(await state(page)).tick;
      await expect.poll(async()=>(await state(page)).tick,{timeout:120000}).toBeGreaterThan(tick+10);
      await page.locator('#canvas').press('Escape',{delay:80});await clickControl(page,'settings');await screen(page,'SettingsScreen');
      const owner=await supersede(page,gates,true);
      const before=await owner.evaluate(()=>rendererQueueOwner.acceptedFrames);expect(before).toBeGreaterThan(0);
      await clickSettingsDone(page);
      await expect.poll(async()=>(await state(page)).screenClass).not.toContain('SettingsScreen');
      // Let the resumed host finish clearing Settings input before Escape.
      const returnedLoop=(await state(page)).loop;
      await expect.poll(async()=>(await state(page)).loop).toBeGreaterThan(returnedLoop+2);
      // Reopen only if the host returned directly to gameplay.
      if(!(await state(page)).controls['recording/toggle'])await page.locator('#canvas').press('Escape',{delay:80});
      await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Stop recording/i);
      await clickControl(page,'return');const resumed=(await state(page)).tick;
      await expect.poll(async()=>(await state(page)).tick,{timeout:120000}).toBeGreaterThan(resumed+10);
      await expect.poll(()=>owner.evaluate(()=>rendererQueueOwner.acceptedFrames)).toBeGreaterThan(before);
      const audit=await owner.evaluate(()=>({starts:rendererQueueOwner.starts,frames:rendererQueueOwner.frames,workerStable:rendererQueueOwner.recordingWorker===Module.glob2Recording.worker}));
      expect(audit.starts).toBe(1);expect(audit.workerStable).toBe(true);
      for(let index=1;index<audit.frames.length;++index)expect(audit.frames[index].time).toBeGreaterThan(audit.frames[index-1].time);
      expect(new Set(audit.frames.map(frame=>frame.width+'x'+frame.height)).size).toBe(1);
      await page.locator('#canvas').press('Escape',{delay:80});await clickControl(page,'recording/toggle');
      await expect.poll(async()=>(await completed(page)).filter(path=>!existing.has(path)).length,{timeout:60000}).toBe(1);
      const name=(await completed(page)).find(path=>!existing.has(path));
      const metadata=await page.evaluate(async name=>{const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings');return JSON.parse(await(await(await root.getFileHandle(encodeURIComponent(name+'.json'))).getFile()).text());},name);
      expect(metadata.complete).toBe(true);expect(metadata.width).toBeGreaterThan(0);expect(metadata.height).toBeGreaterThan(0);
      const chapters=metadata.chapters.filter(chapter=>chapter.screen==='game_session');
      expect(chapters.length).toBeGreaterThan(1);
      const identity=chapters[0];expect(identity.match).toBeGreaterThan(0);
      for(const chapter of chapters){
        expect(chapter.match).toBe(identity.match);expect(chapter.mode).toBe(identity.mode);
        expect(chapter.map).toBe(identity.map);expect(chapter.team).toBe(identity.team);
        expect(chapter.tick).toBeGreaterThanOrEqual(identity.tick);
      }
      await info.attach('failed-preparation-recording-timeline',{body:JSON.stringify({audit,metadata},null,2),contentType:'application/json'});await verify();
    }finally{await gates.cleanup();}
  });
}
