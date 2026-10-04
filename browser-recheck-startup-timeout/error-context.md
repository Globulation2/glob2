# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: recording.spec.js >> threaded interrupted recording recovers committed fragments
- Location: tests/recording.spec.js:75:2

# Error details

```
Error: expect(received).toContain(expected) // indexOf

Expected substring: "MainMenuScreen"
Received string:    "loading"

Call Log:
- Test timeout of 90000ms exceeded
```

# Page snapshot

```yaml
- generic [ref=e1]:
  - generic [ref=e2]:
    - strong [ref=e3]: Globulation 2
    - status [ref=e5]: Starting the game…
    - generic [ref=e6]:
      - progressbar "Download progress" [ref=e7]
      - generic [ref=e9]: 26 of 26 MB
  - generic "Globulation 2" [active] [ref=e10]
```

# Test source

```ts
  1   | // SPDX-License-Identifier: GPL-3.0-or-later
  2   | const {test:base,expect}=require('@playwright/test');
  3   | const os=require('node:os'),path=require('node:path');
  4   | const fs=require('node:fs');
  5   | // WebKit's ephemeral contexts disable OPFS; qualify its persistent browser mode.
  6   | const test=base.extend({page:async({page,browserName,playwright},use,info)=>{
  7   |  if(browserName!=='webkit') return use(page);
  8   |  const directory=fs.mkdtempSync(path.join(os.tmpdir(),'glob2-recording-webkit-'));
  9   |  const context=await playwright.webkit.launchPersistentContext(directory,{headless:true,viewport:info.project.use.viewport,baseURL:info.project.use.baseURL});
  10  |  try {await use(await context.newPage());}finally{await context.close();fs.rmSync(directory,{recursive:true,force:true});}
  11  | }});
  12  | const {gameURL,clickControl:clickPublishedControl,clickMainMenu}=require('./main-menu');
  13  | async function clickControl(page,key) {
  14  |  if(key.startsWith('recording/') && !await page.evaluate(key=>Boolean(glob2Diagnostics.snapshot().controls[key]),key)) {
  15  |   await clickMainMenu(page,'settings');
  16  |   await clickPublishedControl(page,'nav.8');
  17  |  }
  18  |  return clickPublishedControl(page,key);
  19  | }
> 20  | const screen=(page,name)=>expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().screen),{timeout:120000}).toContain(name);
      |                                                                                                                   ^ Error: expect(received).toContain(expected) // indexOf
  21  | for (const variant of ['serial','threaded']) {
  22  |  test(`${variant} recording segments full framebuffer and exports OPFS files`,async({page},info)=>{
  23  |   const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads',variant);
  24  |   await page.goto(url.pathname+url.search); await screen(page,'MainMenuScreen');
  25  |   const completed=()=>page.evaluate(async()=>{
  26  |    const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true});
  27  |    const values=[];for await(const [name,handle] of root.entries()) if(name.endsWith('.complete')) {try {if((await handle.getFile()).size) values.push(decodeURIComponent(name).slice(0,-9));}catch(_){}};return values.sort();
  28  |   });
  29  |   const existing=new Set(await completed());
  30  |   const sessionOutputs=async()=>(await completed()).filter(path=>!existing.has(path));
  31  |   await clickControl(page,'recording/toggle');
  32  |   await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Stop recording/i);
  33  |   await page.waitForTimeout(1500);
  34  |   await page.setViewportSize({width:1001,height:701});await page.waitForTimeout(1500);
  35  |   await clickControl(page,'recording/toggle');
  36  |   await expect.poll(async()=>(await sessionOutputs()).length,{timeout:30000}).toBe(2);
  37  |   const paths=await sessionOutputs();
  38  |   const metadata=await page.evaluate(async paths=>{
  39  |    const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings');
  40  |    return await Promise.all(paths.map(async path=>JSON.parse(await(await(await root.getFileHandle(encodeURIComponent(path+'.json'))).getFile()).text())));
  41  |   },paths);
  42  |   expect(metadata.map(v=>[v.width,v.height]).sort()).toEqual([[1002,702],[1200,900]].sort());
  43  |   for(const value of metadata){expect(value.complete).toBe(true);expect(value.video_codec).toBe('h264');expect(value.audio_codec).toBe('aac');expect(value.fps).toBe(30);expect(value.chapters.length).toBeGreaterThan(0);}
  44  |   await clickControl(page,'recording/files'); await screen(page,'RecordingFilesScreen');
  45  |   const download=page.waitForEvent('download'); await clickControl(page,'export/'+paths[0]);
  46  |   const video=await download; const file=info.outputPath('recording.mp4');await video.saveAs(file);
  47  |   expect(fs.statSync(file).size).toBeGreaterThan(1000);await info.attach('video',{path:file,contentType:'video/mp4'});
  48  |   await info.attach('metadata',{body:JSON.stringify(metadata,null,2),contentType:'application/json'});
  49  |  });
  50  | }
  51  | for(const variant of ['serial','threaded']) {
  52  | test(`${variant} missing WebCodecs uses embedded x264`,async({page})=>{
  53  |  await page.route('**/recording-video.js',async route=>{const response=await route.fetch();await route.fulfill({response,body:'self.VideoEncoder=undefined;\n'+await response.text()});});
  54  |  const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads',variant);
  55  |  await page.goto(url.pathname+url.search);await screen(page,'MainMenuScreen');
  56  |  const existing=await page.evaluate(async()=>{const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true});const names=[];for await(const [name]of root.entries())names.push(name);return names;});
  57  |  await clickControl(page,'recording/toggle');
  58  |  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Stop recording/i);
  59  |  await page.waitForTimeout(4000);await clickControl(page,'recording/toggle');
  60  |  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Start recording/i);
  61  |  await expect.poll(()=>page.evaluate(async existing=>{
  62  |   const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true});
  63  |   for await(const [name,handle]of root.entries()) if(!existing.includes(name)&&name.endsWith('.complete')&&(await handle.getFile()).size){const path=decodeURIComponent(name).slice(0,-9);return JSON.parse(await(await(await root.getFileHandle(encodeURIComponent(path+'.json'))).getFile()).text()).encoder;}return null;
  64  |  },existing),{timeout:30000}).toBe('libx264');
  65  | });
  66  | test(`${variant} OPFS unavailable reports a recording error`,async({page})=>{
  67  |  await page.route('**/recording-storage.js',async route=>{const response=await route.fetch();await route.fulfill({response,body:await response.text()+"\nrecordingStorage.initialize=async()=>{throw new Error('OPFS unavailable fixture');};"});});
  68  |  const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads',variant);
  69  |  await page.goto(url.pathname+url.search);await screen(page,'MainMenuScreen');await clickControl(page,'recording/toggle');
  70  |  await expect.poll(()=>page.title()).toMatch(/recording failed/i);
  71  |  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Start recording/i);
  72  | });
  73  | }
  74  | for(const variant of ['serial','threaded']) {
  75  |  test(`${variant} interrupted recording recovers committed fragments`,async({page},info)=>{
  76  |   const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads',variant);
  77  |   await page.goto(url.pathname+url.search);await screen(page,'MainMenuScreen');
  78  |   const manifests=()=>page.evaluate(async()=>{
  79  |    const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true});const names=[];
  80  |    for await(const [name]of root.entries())if(decodeURIComponent(name).endsWith('.recording/manifest.json'))names.push(name);return names;
  81  |   });
  82  |   const existing=new Set(await manifests());
  83  |   await clickControl(page,'recording/toggle');
  84  |   await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Stop recording/i);
  85  |   await page.waitForTimeout(3500);
  86  |   await page.reload();await screen(page,'MainMenuScreen');
  87  |   const interrupted=(await manifests()).find(name=>!existing.has(name));expect(interrupted).toBeTruthy();
  88  |   const path=decodeURIComponent(interrupted).slice(0,-24);
  89  |   await clickControl(page,'recording/files');await screen(page,'RecordingFilesScreen');
  90  |   await clickControl(page,'recover/'+path);
  91  |   await expect.poll(()=>page.evaluate(async path=>{
  92  |    const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings');
  93  |    try{return(await(await root.getFileHandle(encodeURIComponent(path+'.complete'))).getFile()).size;}catch(_){return 0;}
  94  |   },path),{timeout:60000}).toBe(1);
  95  |   const download=page.waitForEvent('download');await clickControl(page,'export/'+path);
  96  |   const video=await download,file=info.outputPath('recovered.mp4');await video.saveAs(file);
  97  |   expect(fs.statSync(file).size).toBeGreaterThan(1000);await info.attach('recovered video',{path:file,contentType:'video/mp4'});
  98  |  });
  99  |  test(`${variant} OPFS full reports an error and retains recovery files`,async({page})=>{
  100 |   await page.route('**/recording-storage.js',async route=>{
  101 |    const response=await route.fetch();await route.fulfill({response,body:await response.text()+
  102 |     "\nconst fixtureWrite=recordingStorage.write.bind(recordingStorage);let fixtureWritten=0;recordingStorage.write=(id,bytes)=>{fixtureWritten+=bytes.length;return fixtureWritten>8192 ? -5 : fixtureWrite(id,bytes);};"});
  103 |   });
  104 |   const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads',variant);
  105 |   await page.goto(url.pathname+url.search);await screen(page,'MainMenuScreen');await clickControl(page,'recording/toggle');
  106 |   await expect.poll(()=>page.title(),{timeout:60000}).toMatch(/recording failed/i);
  107 |   await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Start recording/i);
  108 |   const retained=await page.evaluate(async()=>{
  109 |    const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings');const names=[];
  110 |    for await(const [name,handle]of root.entries())if(decodeURIComponent(name).endsWith('.recording/manifest.json')&&(await handle.getFile()).size)names.push(name);return names;
  111 |   });expect(retained.length).toBeGreaterThan(0);
  112 |  });
  113 | }
  114 | 
```