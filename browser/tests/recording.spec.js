// SPDX-License-Identifier: GPL-3.0-or-later
const {test:base,expect}=require('@playwright/test');
const os=require('node:os'),path=require('node:path');
const fs=require('node:fs');
// WebKit's ephemeral contexts disable OPFS; qualify its persistent browser mode.
const test=base.extend({page:async({page,browserName,playwright},use,info)=>{
 if(browserName!=='webkit') return use(page);
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'glob2-recording-webkit-'));
 const context=await playwright.webkit.launchPersistentContext(directory,{headless:true,viewport:info.project.use.viewport,baseURL:info.project.use.baseURL});
 try {await use(await context.newPage());}finally{await context.close();fs.rmSync(directory,{recursive:true,force:true});}
}});
const {gameURL,clickControl}=require('./main-menu');
const screen=(page,name)=>expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().screen)).toContain(name);
for (const variant of ['serial','threaded']) {
 test(`${variant} recording segments full framebuffer and exports OPFS files`,async({page},info)=>{
  const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads',variant);
  await page.goto(url.pathname+url.search); await screen(page,'MainMenuScreen');
  const completed=()=>page.evaluate(async()=>{
   const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true});
   const values=[];for await(const [name,handle] of root.entries()) if(name.endsWith('.complete')) {try {if((await handle.getFile()).size) values.push(decodeURIComponent(name).slice(0,-9));}catch(_){}};return values.sort();
  });
  const existing=new Set(await completed());
  const sessionOutputs=async()=>(await completed()).filter(path=>!existing.has(path));
  await clickControl(page,'recording/toggle');
  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Stop recording/i);
  await page.waitForTimeout(1500);
  await page.setViewportSize({width:1001,height:701});await page.waitForTimeout(1500);
  await clickControl(page,'recording/toggle');
  await expect.poll(async()=>(await sessionOutputs()).length,{timeout:30000}).toBe(2);
  const paths=await sessionOutputs();
  const metadata=await page.evaluate(async paths=>{
   const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings');
   return await Promise.all(paths.map(async path=>JSON.parse(await(await(await root.getFileHandle(encodeURIComponent(path+'.json'))).getFile()).text())));
  },paths);
  expect(metadata.map(v=>[v.width,v.height]).sort()).toEqual([[1002,702],[1200,900]].sort());
  for(const value of metadata){expect(value.complete).toBe(true);expect(value.video_codec).toBe('h264');expect(value.audio_codec).toBe('aac');expect(value.fps).toBe(30);expect(value.chapters.length).toBeGreaterThan(0);}
  await clickControl(page,'recording/files'); await screen(page,'RecordingFilesScreen');
  const download=page.waitForEvent('download'); await clickControl(page,'export/'+paths[0]);
  const video=await download; const file=info.outputPath('recording.mp4');await video.saveAs(file);
  expect(fs.statSync(file).size).toBeGreaterThan(1000);await info.attach('video',{path:file,contentType:'video/mp4'});
  await info.attach('metadata',{body:JSON.stringify(metadata,null,2),contentType:'application/json'});
 });
}
test('missing WebCodecs uses embedded x264',async({page})=>{
 await page.route('**/recording-video.js',async route=>{const response=await route.fetch();await route.fulfill({response,body:'self.VideoEncoder=undefined;\n'+await response.text()});});
 await page.goto(gameURL());await screen(page,'MainMenuScreen');
 const existing=await page.evaluate(async()=>{const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true});const names=[];for await(const [name]of root.entries())names.push(name);return names;});
 await clickControl(page,'recording/toggle');await page.waitForTimeout(4000);await clickControl(page,'recording/toggle');
 await expect.poll(()=>page.evaluate(async existing=>{
  const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true});
  for await(const [name,handle]of root.entries()) if(!existing.includes(name)&&name.endsWith('.complete')&&(await handle.getFile()).size){const path=decodeURIComponent(name).slice(0,-9);return JSON.parse(await(await(await root.getFileHandle(encodeURIComponent(path+'.json'))).getFile()).text()).encoder;}return null;
 },existing),{timeout:30000}).toBe('libx264');
});
test('OPFS unavailable reports a recording error',async({page})=>{
 await page.route('**/recording-storage.js',async route=>{const response=await route.fetch();await route.fulfill({response,body:await response.text()+"\nrecordingStorage.initialize=async()=>{throw new Error('OPFS unavailable fixture');};"});});
 await page.goto(gameURL());await screen(page,'MainMenuScreen');await clickControl(page,'recording/toggle');
 await expect.poll(()=>page.title()).toMatch(/recording failed/i);
 await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['recording/toggle']?.label)).toMatch(/Start recording/i);
});
