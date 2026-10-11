#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual packed-command library, owned uploads and bounded capture.
import{createRequire}from'node:module';import{readFileSync,writeFileSync}from'node:fs';import{resolve,join}from'node:path';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'@playwright/test');
const directory=resolve(process.argv[2]||'artifacts/shaders/generated');
const shaders=Object.fromEntries(['primitive','world','cloud','point','skin','skin-rig','terrain-compose','terrain-reduce'].flatMap(v=>['vert','frag'].map(s=>[`${v}.${s}.wgsl`,readFileSync(join(directory,'webgpu',`${v}.${s}.wgsl`),'utf8')])));
shaders['metadata.json']=readFileSync(join(directory,'metadata.json'),'utf8');
const library=readFileSync(new URL('../../browser/webgpu-device.js',import.meta.url),'utf8');
const browser=await chromium.launch({executablePath:process.env.PLAYWRIGHT_CHROMIUM,args:['--enable-unsafe-webgpu','--enable-features=Vulkan','--use-vulkan=swiftshader','--use-angle=vulkan','--disable-vulkan-surface']});
try{const page=await browser.newPage();await page.goto(process.env.SHADER_PROOF_URL||'http://127.0.0.1:8784/');
const result=await page.evaluate(async({library,shaders})=>{
  const entries={};globalThis.addToLibrary=value=>Object.assign(entries,value);globalThis.FS={readFile:path=>shaders[path.split('/').pop()]};
  globalThis.HEAPU8=new Uint8Array(new SharedArrayBuffer(1024*1024));globalThis.Module={canvas:document.createElement('canvas')};Module.canvas.width=Module.canvas.height=64;
  document.body.appendChild(Module.canvas);eval(library);globalThis.Glob2WebGPU=entries.$Glob2WebGPU;const g=Glob2WebGPU;
  await g.prepare('/shaders');if(g.state!==1)throw Error(g.error);if(!entries.glob2_webgpu_attach())throw Error(g.error);
  const pixels=HEAPU8.subarray(0,4);pixels.set([180,70,20,255]);if(!entries.glob2_webgpu_upload(1,1,1,0,0,0))throw Error(g.error);pixels.fill(0); // Upload owns a non-shared copy.
  function packed(texture,color=[1,1,1,1],target=0){const data=new Uint8Array(256),v=new DataView(data.buffer);const u=(offset,value)=>v.setUint32(offset,value,true),f=(offset,value)=>v.setFloat32(offset,value,true);
    u(24,texture);u(28,target);u(36,96);u(52,3);[0,0,64,64].forEach((n,i)=>{u(64+i*4,n);u(80+i*4,n);});f(96,64);f(100,64);f(104,1);f(108,+!!texture);f(120,1);f(124,1);
    const vertices=new Float32Array([[0,0],[64,0],[0,64]].flatMap(([x,y])=>[x,y,...color,.25,.25]));return{data,vertices:new Uint8Array(vertices.buffer)};}
  const draw=packed(1),identity=g.submit(draw.data,1,draw.vertices,new Uint8Array(),64,64);g.present();try{await g.device.queue.onSubmittedWorkDone();}catch(e){throw Error(g.error+' '+e.message);}if(g.completed<identity)throw Error('Submission incomplete');
  if(!g.capture(7,64,64)||!g.capture(8,64,64)||!g.capture(9,64,64)||g.capture(10,64,64))throw Error('Capture queue bound failed');
  while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));const captured=[];
  while(g.captures.length){while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));const id=g.captures[0].identity;const status=entries.glob2_webgpu_capture_poll(4096,256);if(status!==1)throw Error('Capture failed');captured.push({id,pixel:Array.from(HEAPU8.slice(4096+8*256+8*4,4096+8*256+8*4+4))});}
  if(captured.some(c=>JSON.stringify(c.pixel)!==JSON.stringify([180,70,20,255])))throw Error('Owned upload/render mismatch '+JSON.stringify(captured));
  if(captured.map(c=>c.id).join(',')!=='7,8,9')throw Error('Capture order changed');
  // A retained target can be sampled with the same orientation on a later pass.
  pixels.set([0,0,0,0]);if(!entries.glob2_webgpu_upload(2,64,64,0,0,0))throw Error(g.error);
  const target=packed(0,[0,1,0,1],2);g.submit(target.data,1,target.vertices,new Uint8Array(),64,64);
  const blit=packed(2);g.submit(blit.data,1,blit.vertices,new Uint8Array(),64,64);
  if(!g.capture(11,64,64))throw Error('Offscreen capture rejected');while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));entries.glob2_webgpu_capture_poll(4096,256);
  const targetPixel=Array.from(HEAPU8.slice(4096+8*256+8*4,4096+8*256+8*4+4));if(JSON.stringify(targetPixel)!==JSON.stringify([0,255,0,255]))throw Error('Offscreen orientation failed '+targetPixel);
  const worldVertices=new Float32Array([[-.8,-.8],[.8,-.8],[-.8,.8]].flatMap(([x,y])=>[x,y,.25,1,.5,.5,.5,.25,.25,0,0,1]));HEAPU8.set(new Uint8Array(worldVertices.buffer),16384);HEAPU8.set(new Uint8Array(new Uint32Array([0,1,2]).buffer),17000);
  if(!entries.glob2_webgpu_geometry_upload(1,16384,worldVertices.byteLength,17000,12))throw Error(g.error);HEAPU8.fill(0,16384,17012);
  const mesh=packed(1),mv=new DataView(mesh.data.buffer),mu=(o,v)=>mv.setUint32(o,v,true),mf=(o,v)=>mv.setFloat32(o,v,true);
  mu(0,1);mu(4,1);mu(36,worldVertices.byteLength);mu(44,3);mu(52,3);mu(252,1);[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,1,0,0].forEach((v,i)=>mf(128+i*4,v));mf(216,1);
  g.submit(mesh.data,1,new Uint8Array(),new Uint8Array(),64,64);g.capture(12,64,64);while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));entries.glob2_webgpu_capture_poll(4096,256);
  const meshPixel=Array.from(HEAPU8.slice(4096+48*256+16*4,4096+48*256+16*4+4));if(JSON.stringify(meshPixel)!==JSON.stringify([90,35,10,255]))throw Error('Persistent indexed mesh mismatch '+meshPixel);
  HEAPU8.set([0,0,0,255],0);entries.glob2_webgpu_upload(3,1,1,0,0,0);
  const skinVertices=new Float32Array([[-.8,-.8],[.8,-.8],[-.8,.8]].flatMap(([x,y])=>[x,y,0,0,0,1,.25,.25,.25,.25]));HEAPU8.set(new Uint8Array(skinVertices.buffer),18000);entries.glob2_webgpu_geometry_upload(2,18000,skinVertices.byteLength,0,0);
  const skinPacket=packed(1),sv=new DataView(skinPacket.data.buffer),su=(o,v)=>sv.setUint32(o,v,true);su(0,1);su(4,4);su(8,3);su(12,2);su(16,1);su(36,skinVertices.byteLength);su(52,3);su(60,3);su(112,0);su(116,16);su(120,16);su(124,16);su(252,2);
  const skinUniforms=new Float32Array([0,.01,.001,0,1,1,0,0]);g.submit(skinPacket.data,1,new Uint8Array(),new Uint8Array(skinUniforms.buffer),64,64);
  entries.glob2_webgpu_capture(13,16,16,0,8,40);while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));entries.glob2_webgpu_capture_poll(4096,64);const skinPixel=Array.from(HEAPU8.slice(4096+8*64+8*4,4096+8*64+8*4+4));if(!skinPixel[0]||skinPixel[3]!==255)throw Error('Packed skin or crop capture failed '+skinPixel);
  entries.glob2_webgpu_capture(14,16,16,2,0,0);while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));entries.glob2_webgpu_capture_poll(4096,64);const cropPixel=Array.from(HEAPU8.slice(4096+8*64+8*4,4096+8*64+8*4+4));if(JSON.stringify(cropPixel)!==JSON.stringify([0,255,0,255]))throw Error('Target crop capture failed '+cropPixel);
  const rigVertices=new Float32Array([[-.8,-.8],[.8,-.8],[-.8,.8]].flatMap(([x,y])=>[x,y,0,0,0,1,.25,.25,.25,.25,0,0,0,0,1,0,0,0]));HEAPU8.set(new Uint8Array(rigVertices.buffer),18000);entries.glob2_webgpu_geometry_upload(3,18000,rigVertices.byteLength,0,0);
  const rigData=skinPacket.data.slice(),rv=new DataView(rigData.buffer);rv.setUint32(4,5,true);rv.setUint32(36,rigVertices.byteLength,true);rv.setUint32(116,2176,true);rv.setUint32(120,2176,true);rv.setUint32(252,3,true);
  const rigUniform=new Float32Array(548),identityMatrix=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];for(let bone=0;bone<32;bone++)rigUniform.set(identityMatrix,bone*16);rigUniform.set(identityMatrix,512);rigUniform.set([1,0,0,0,0,1,0,0,0,0,1,0],528);rigUniform.set([1,1,0,0],544);
  g.submit(rigData,1,new Uint8Array(),new Uint8Array(rigUniform.buffer),64,64);g.capture(16,16,16,0,8,40);while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));entries.glob2_webgpu_capture_poll(4096,64);const rigPixel=Array.from(HEAPU8.slice(4096+8*64+8*4,4096+8*64+8*4+4));if(JSON.stringify(rigPixel)!==JSON.stringify(skinPixel))throw Error('Packed rest rig mismatch '+rigPixel+' '+skinPixel);
  const clear=packed(0),cv=new DataView(clear.data.buffer);cv.setUint32(0,2,true);cv.setUint32(244,1,true);cv.setFloat32(224,1,true);cv.setFloat32(236,1,true);[0,0,8,8].forEach((n,i)=>cv.setUint32(80+i*4,n,true));
  g.submit(clear.data,1,new Uint8Array(),new Uint8Array(),64,64);g.capture(15,64,64);while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));entries.glob2_webgpu_capture_poll(4096,256);
  const clearPixel=Array.from(HEAPU8.slice(4096+4*256+4*4,4096+4*256+4*4+4)),adjacentPixel=Array.from(HEAPU8.slice(4096+48*256+16*4,4096+48*256+16*4+4));
  if(JSON.stringify(clearPixel)!==JSON.stringify([255,0,0,255])||JSON.stringify(adjacentPixel)!==JSON.stringify(skinPixel))throw Error('Clipped clear discarded adjacent contents '+clearPixel+' '+adjacentPixel+' '+skinPixel);
  for(let i=0;i<32;i++)g.submit(draw.data,1,draw.vertices,new Uint8Array(),64,64);g.present();await g.device.queue.onSubmittedWorkDone();if(g.activeFrame||g.frames.length)throw Error('Presentation did not fence all chunks');
  // Admission may fail after earlier recorder chunks have reached the GPU.
  // Restoring the committed image must preserve the old physical presentation,
  // and captures admitted before the interrupted frame remain deliverable.
  const retained=g.screen.texture;const oldCapture=entries.glob2_webgpu_capture(100,64,64,0,0,0);if(!oldCapture)throw Error('Old capture rejected');
  const failed=packed(0,[0,0,1,1]);g.submit(failed.data,1,failed.vertices,new Uint8Array(),64,64);
  const actualBytes=g.bytes;g.bytes=256*1024*1024;if(entries.glob2_webgpu_submit(0,0,0,0,0,0,64,64)!==0||!entries.glob2_webgpu_deferred()||g.lost)throw Error('Budget did not defer');g.bytes=actualBytes;
  if(!entries.glob2_webgpu_discard())throw Error(g.error);await g.device.queue.onSubmittedWorkDone();while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));entries.glob2_webgpu_capture_poll(4096,256);
  const oldPixel=Array.from(HEAPU8.slice(4096+8*256+8*4,4096+8*256+8*4+4));if(JSON.stringify(oldPixel)!==JSON.stringify([180,70,20,255]))throw Error('Old capture corrupted by interrupted frame');
  entries.glob2_webgpu_capture(101,64,64,0,0,0);while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));entries.glob2_webgpu_capture_poll(4096,256);const rollbackPixel=Array.from(HEAPU8.slice(4096+8*256+8*4,4096+8*256+8*4+4));if(JSON.stringify(rollbackPixel)!==JSON.stringify(oldPixel))throw Error('Partial frame remained after discard '+rollbackPixel);
  g.frames=Array.from({length:8},()=>({owned:[]}));if(entries.glob2_webgpu_can_begin()!==0||g.submit(draw.data,1,draw.vertices,new Uint8Array(),64,64))throw Error('Submission queue bound failed');g.frames=[];
  await g.device.queue.onSubmittedWorkDone();entries.glob2_webgpu_reset();if(g.resources.size||g.captures.length||g.frames.length||g.screen)throw Error('Reset retained resources');
  g.device.destroy();await g.device.lost;await new Promise(resolve=>setTimeout(resolve,0));if(entries.glob2_webgpu_poll()!==-1||entries.glob2_webgpu_can_begin()!==-1)throw Error('Loss not reported');await g.prepare('/shaders');if(g.state!==1||!entries.glob2_webgpu_attach())throw Error('Recovery failed '+g.error);
  // Recreate logical content on the new device, then prove submission and
  // readback work. Merely obtaining a fresh GPUDevice is not recovery proof.
  pixels.set([180,70,20,255]);if(!entries.glob2_webgpu_upload(1,1,1,0,0,0))throw Error('Recovery upload failed '+g.error);
  const restored=g.submit(draw.data,1,draw.vertices,new Uint8Array(),64,64);if(!restored||!entries.glob2_webgpu_present())throw Error('Recovery drawing failed '+g.error);
  await g.device.queue.onSubmittedWorkDone();if(g.completed<restored)throw Error('Recovered submission never completed');
  if(!entries.glob2_webgpu_capture(102,64,64,0,0,0))throw Error('Recovery capture rejected');
  while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));if(entries.glob2_webgpu_capture_poll(4096,256)!==1)throw Error('Recovery capture failed');
  const recoveryPixel=Array.from(HEAPU8.slice(4096+8*256+8*4,4096+8*256+8*4+4));if(JSON.stringify(recoveryPixel)!==JSON.stringify(oldPixel))throw Error('Recovered pixels changed '+recoveryPixel);
  entries.glob2_webgpu_reset();
  // Four atlas color pages fit the legacy 64 MiB color class. Their depth
  // attachments add another 64 MiB to the aggregate GPU allocation budget.
  for(let id=200;id<204;++id){g.resources.set(id,g.image(2048,2048,null,true));g.bytes+=2048*2048*8;}
  const atlasBytes=g.bytes;if(atlasBytes!==128*1024*1024||g.totalBytes()>256*1024*1024)throw Error('Atlas aggregate accounting failed');
  entries.glob2_webgpu_reset();if(g.bytes||g.resources.size)throw Error('Atlas allocations not retired');
  entries.glob2_webgpu_attach();
  HEAPU8.set([180,70,20,128],0);entries.glob2_webgpu_upload(301,1,1,0,0,0,0);
  HEAPU8.fill(0,32768,32768+64*64*4);entries.glob2_webgpu_upload(302,64,64,32768,0,0,1);
  const compose=new Uint8Array(256),terrainView=new DataView(compose.buffer);const terrainU=(o,v)=>terrainView.setUint32(o,v,true);
  terrainU(0,4);terrainU(28,302);[8,8,16,16].forEach((v,i)=>terrainU(64+i*4,v));terrainU(128,301);terrainU(148,32);terrainU(152,2);terrainU(156,1);
  const params=new Uint32Array(36);params.set([32,1,1,0]);
  g.submit(compose,1,new Uint8Array(),new Uint8Array(params.buffer),64,64);
  entries.glob2_webgpu_capture(303,64,64,302,0,0);while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));if(entries.glob2_webgpu_capture_poll(32768,256)!==1)throw Error('Terrain capture failed');
  const terrainPixel=Array.from(HEAPU8.slice(32768+10*256+10*4,32768+10*256+10*4+4));if(JSON.stringify(terrainPixel)!==JSON.stringify([90,35,10,128]))throw Error('Packed terrain PMA mismatch '+terrainPixel);
  const gutter=Array.from(HEAPU8.slice(32768+7*256+7*4,32768+7*256+7*4+4));if(gutter.some(v=>v))throw Error('Terrain overwrote atlas gutter');
  const weights=new Uint16Array(32*32*4);for(let i=0;i<32*32;i++)weights[i*4]=32768;HEAPU8.set(new Uint8Array(weights.buffer),65536);HEAPU8.fill(0,75000,75000+32*32);
  if(!entries.glob2_webgpu_mask_upload(1,32,32,65536,75000))throw Error(g.error);HEAPU8.fill(0,65536,65536+weights.byteLength);
  terrainU(144,1);params.set([32,2,1,1]);g.submit(compose,1,new Uint8Array(),new Uint8Array(params.buffer),64,64);
  entries.glob2_webgpu_capture(304,64,64,302,0,0);while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));entries.glob2_webgpu_capture_poll(32768,256);
  const weightedPixel=Array.from(HEAPU8.slice(32768+10*256+10*4,32768+10*256+10*4+4));if(JSON.stringify(weightedPixel)!==JSON.stringify([45,18,5,64]))throw Error('Packed weighted terrain mismatch '+weightedPixel);
  g.present();await g.device.queue.onSubmittedWorkDone();entries.glob2_webgpu_reset();if(g.maskBytes||g.scratchBytes||g.masks.size||g.scratch.size)throw Error('Terrain reset retained cache');
  entries.glob2_webgpu_attach();
  const mipColors=[[255,0,0,255],...[1,2,3,4].map(()=>[0,255,0,255]),[0,0,255,255],[255,255,0,255],[255,255,0,255]];
  for(let level=0;level<8;level++){const size=128>>level;for(let pixel=0;pixel<size*size;pixel++)HEAPU8.set(mipColors[level],pixel*4);if(level===0){if(!entries.glob2_webgpu_upload(401,size,size,0,0,0,0,8))throw Error(g.error);}else if(!entries.glob2_webgpu_mip_upload(401,level,size,size,0))throw Error(g.error);}
  HEAPU8.fill(0);const mipPacket=packed(401),mipView=new DataView(mipPacket.data.buffer);mipView.setUint32(36,192,true);mipView.setUint32(52,6,true);
  const mipVertices=new Float32Array([[0,0,0,0],[1,0,1,0],[1,1,1,1],[0,0,0,0],[1,1,1,1],[0,1,0,1]].flatMap(([x,y,u,v])=>[x*4,y*4,1,1,1,1,u,v]));
  const mipSamples=[];for(const linear of[0,1]){mipView.setUint32(56,linear,true);g.submit(mipPacket.data,1,new Uint8Array(mipVertices.buffer),new Uint8Array(),64,64);entries.glob2_webgpu_capture(402+linear,1,1,0,1,1);while(!g.captures[0].ready)await new Promise(resolve=>setTimeout(resolve,0));entries.glob2_webgpu_capture_poll(4096,4);mipSamples.push(Array.from(HEAPU8.slice(4096,4100)));}
  if(JSON.stringify(mipSamples)!==JSON.stringify([[255,0,0,255],[0,0,255,255]]))throw Error('Authored mip minification mismatch '+JSON.stringify(mipSamples));
  const mipBytes=g.resources.get(401).colorBytes;if(mipBytes!==87380)throw Error('Mip memory accounting '+mipBytes);g.present();await g.device.queue.onSubmittedWorkDone();entries.glob2_webgpu_reset();
  return{mipSamples,mipBytes,authoredMips:true,ownedMipUploads:true,weightedPixel,ownedCoverageUpload:true,terrainPixel,terrainPMAAndGutter:true,terrainReset:true,atlasBytes,fourAtlasPages:true,recoveryPixel,recoveryUploadDrawCapture:true,rigPixel,packedRig:true,clippedClear:true,clearPixel,adjacentPixel,skinPixel,cropPixel,skinAndCrop:true,persistentMesh:true,meshPixel,lossRecovery:true,ownedUpload:true,completion:true,threeCaptures:true,captured,targetPixel,eightFrames:true,rollbackPixel,discardRollback:true,oldCapturePreserved:true,reset:true,manyChunksOneFrame:true};
},{library,shaders});writeFileSync(join(directory,'webgpu-device-proof.json'),JSON.stringify(result,null,2)+'\n');console.log('WebGPU packed-device proof passed: uploads, ordering, offscreen target, completion, capture and bounds');
}finally{await browser.close();}
