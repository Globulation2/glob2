// SPDX-License-Identifier: GPL-3.0-or-later
// Matched recording qualification on a real saved game. Timing is never a CI gate.
const {chromium}=require('@playwright/test');
const fs=require('node:fs/promises');
const path=require('node:path');
const os=require('node:os');
const {createHash}=require('node:crypto');
const {execFileSync}=require('node:child_process');
const {clickMainMenu,clickControl,clickListRow}=require('../tests/main-menu');
const seconds=Number(process.env.GLOB2_RECORD_SECONDS || 20),repeats=Number(process.env.GLOB2_RECORD_REPEATS || 3);
const output=path.resolve(process.argv[3] || 'artifacts/recording-benchmark');
const fixture=path.resolve(process.env.GLOB2_RECORD_FIXTURE || 'games/gd-bigarena-long.game.gz');
const percentile=(values,p)=>values.length ? [...values].sort((a,b)=>a-b)[Math.min(values.length-1,Math.floor(values.length*p))] : null;
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
async function measure(browser,runtime,width,height,scale,encoder,repeat) {
 const context=await browser.newContext({viewport:{width,height},deviceScaleFactor:scale});
 const page=await context.newPage(),errors=[];
 page.on('pageerror',e=>errors.push(String(e)));
 if(encoder==='software') await page.route('**/recording-video.js',async route=>{const response=await route.fetch();await route.fulfill({response,body:'self.VideoEncoder=undefined;\n'+await response.text()});});
 const url=new URL(process.argv[2] || 'http://127.0.0.1:8770');url.searchParams.set('threads',runtime);url.searchParams.set('renderer','webgl2');
 await page.goto(url.href);
 await page.waitForFunction(()=>globalThis.glob2Diagnostics?.snapshot().screen.includes('MainMenuScreen'),null,{timeout:120000});
 await clickMainMenu(page,'load');const chooser=page.waitForEvent('filechooser');await clickControl(page,'import');await(await chooser).setFiles(fixture);
 await page.waitForFunction(()=>glob2Diagnostics.snapshot().import==='succeeded');await clickListRow(page,'files',0);await clickControl(page,'ok');
 await page.waitForFunction(()=>glob2Diagnostics.snapshot().screen==='match');
 if(encoder!=='off') await page.keyboard.press('Control+Shift+R');
 await page.waitForTimeout(4000);
 const cdp=await browser.newBrowserCDPSession();
 const usage=async()=>{
  const processes=(await cdp.send('SystemInfo.getProcessInfo')).processInfo;
  let rss=null;
  if(process.platform==='darwin'||process.platform==='linux') {
   try {rss=execFileSync('ps',['-o','rss=','-p',processes.map(p=>p.id).join(',')],{encoding:'utf8'}).trim().split(/\s+/).reduce((sum,n)=>sum+Number(n)*1024,0);}catch(_){}
  }
  return {cpu:processes.reduce((sum,p)=>sum+p.cpuTime,0),rss};
 };
 const snapshot=()=>page.evaluate(()=>({time:performance.now(),...glob2Diagnostics.snapshot()}));
 const before=await snapshot(),startUsage=await usage(),samples=[];
 const intervals=[];let previous=before;
 // Continuous camera movement exercises readback while the busy saved battle runs.
 for(let n=0;n<seconds*10;++n) {
  if(n%20===0){await page.keyboard.up(n%40===0?'ArrowLeft':'ArrowRight');await page.keyboard.down(n%40===0?'ArrowRight':'ArrowLeft');}
  await page.waitForTimeout(100);const sample=await snapshot();samples.push(sample);
  if(sample.frames>previous.frames) intervals.push((sample.time-previous.time)/(sample.frames-previous.frames));previous=sample;
 }
 await page.keyboard.up('ArrowLeft');await page.keyboard.up('ArrowRight');
 const after=await snapshot(),endUsage=await usage(),elapsed=(after.time-before.time)/1000;
 let metadata=[];
 if(encoder!=='off') {
  await page.keyboard.press('Control+Shift+R');
  await page.waitForTimeout(100);
  await page.waitForFunction(()=>!document.title.includes('Finalizing recording'),null,{timeout:120000});
  metadata=await page.evaluate(async()=>{
   const root=await(await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings');const result=[];
   for await(const [name,file]of root.entries()) if(name.endsWith('.json')&&!name.includes('recording%2F')&&!name.endsWith('.session.json')) {try{result.push(JSON.parse(await(await file.getFile()).text()));}catch(_){}}
   return result;
  });
  if(!metadata.length) errors.push('Recording produced no completed metadata');
 }
 const result={runtime,encoder,repeat,viewport:{width,height,scale},framebuffer:{width:after.width,height:after.height},elapsed,
  fps:(after.frames-before.frames)/elapsed,ticksPerSecond:(after.tick-before.tick)/elapsed,
  sampledFrameMs:{p50:percentile(intervals,.5),p95:percentile(intervals,.95),p99:percentile(intervals,.99)},
  cpuCores:(endUsage.cpu-startUsage.cpu)/elapsed,rssBytes:endUsage.rss,rssChangeBytes:endUsage.rss-startUsage.rss,
  audio:after.audio,metadata,errors};
 await cdp.detach();await context.close();return result;
}
(async()=>{
 if(!(seconds>0&&repeats>0))throw Error('Invalid benchmark duration/repetitions');
 await fs.mkdir(output,{recursive:true});
 const browser=await chromium.launch({headless:process.env.GLOB2_RECORD_HEADED!=='1',args:process.platform==='darwin'?['--use-angle=metal']:[]});
 const records=[];
 try {
  const profiles=process.env.GLOB2_RECORD_PROFILES ? JSON.parse(process.env.GLOB2_RECORD_PROFILES) : [[1280,720,1],[1920,1080,1],[1920,1080,2]];
  const runtimes=(process.env.GLOB2_RECORD_RUNTIMES || 'serial,threaded').split(',');
  for(const runtime of runtimes)for(const [width,height,scale]of profiles)for(let repeat=0;repeat<repeats;++repeat)for(const encoder of ['off','auto','software']) {
   const result=await measure(browser,runtime,width,height,scale,encoder,repeat);records.push(result);console.log(JSON.stringify(result));
   await fs.writeFile(path.join(output,'measurements.json'),JSON.stringify({schema:1,host:{platform:os.platform(),release:os.release(),cpus:os.cpus().length,memory:os.totalmem()},browser:browser.version(),fixture:{path:fixture,sha256:hash(await fs.readFile(fixture))},seconds,repeats,records},null,2)+'\n');
  }
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
