#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise generated integer coverage and reduction shaders on a real GPU device.
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'@playwright/test');
const directory=resolve(process.argv[2]||'artifacts/shaders/generated-terrain');
const metadata=JSON.parse(readFileSync(join(directory,'metadata.json'),'utf8'));
const sources=Object.fromEntries(['terrain-compose','terrain-reduce'].map(v=>[v,Object.fromEntries(['vert','frag'].map(s=>[s,readFileSync(join(directory,'webgpu',`${v}.${s}.wgsl`),'utf8')]))]));
const browser=await chromium.launch({executablePath:process.env.PLAYWRIGHT_CHROMIUM,args:['--enable-unsafe-webgpu','--enable-features=Vulkan','--use-vulkan=swiftshader','--use-angle=vulkan','--disable-vulkan-surface']});
try {
 const page=await browser.newPage();await page.goto(process.env.SHADER_PROOF_URL||'http://127.0.0.1:8784/');
 const result=await page.evaluate(async({metadata,sources})=>{
  const adapter=await navigator.gpu.requestAdapter();if(!adapter)throw Error('WebGPU adapter unavailable');
  const device=await adapter.requestDevice(),errors=[];device.addEventListener('uncapturederror',e=>errors.push(e.error.message));
  const pipelines={};for(const[v,source]of Object.entries(sources)){
   const modules={};for(const[s,code]of Object.entries(source)){modules[s]=device.createShaderModule({code});const info=await modules[s].getCompilationInfo();if(info.messages.some(m=>m.type==='error'))throw Error(JSON.stringify(info.messages));}
   pipelines[v]=await device.createRenderPipelineAsync({layout:'auto',vertex:{module:modules.vert,entryPoint:'main',buffers:[{arrayStride:32,attributes:metadata.vertexLayouts[v].attributes.map(a=>({shaderLocation:a.location,offset:a.offset,format:'float32x'+a.components}))}]},fragment:{module:modules.frag,entryPoint:'main',targets:[{format:'rgba8unorm'}]},primitive:{topology:'triangle-list'}});
  }
  const buffer=(data,usage)=>{const b=device.createBuffer({size:data.byteLength,usage:usage|GPUBufferUsage.COPY_DST});device.queue.writeBuffer(b,0,data);return b;};
  const texture=(format,data,width=1,height=1)=>{const t=device.createTexture({size:[width,height],format,usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST});device.queue.writeTexture({texture:t},data,{bytesPerRow:data.byteLength/height},[width,height]);return t;};
  const vertex=buffer(new Float32Array([[0,0,0,0],[1,0,1,0],[1,1,1,1],[0,0,0,0],[1,1,1,1],[0,1,0,1]].flatMap(([x,y,u,v])=>[x,y,1,1,1,1,u,v])),GPUBufferUsage.VERTEX);
  const viewport=buffer(new Float32Array([1,1,0,0]),GPUBufferUsage.UNIFORM);
  const target=device.createTexture({size:[1,1],format:'rgba8unorm',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});
  const readback=device.createBuffer({size:256,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
  const paints=[[210,90,30,255],[20,220,80,128],[70,40,250,64],[250,80,160,0]];
  const material=paints.map(p=>texture('rgba8unorm',new Uint8Array(p)));
  const coverage=texture('rgba16uint',new Uint16Array([8192,4096,2048,0]));
  const selector=texture('r8uint',new Uint8Array([0]));
  const entries=material.map((t,i)=>({binding:i*2,resource:t.createView()})).concat([{binding:8,resource:coverage.createView()},{binding:10,resource:selector.createView()}]);
  async function draw(variant,uniforms,resources){const pipeline=pipelines[variant],ubo=buffer(uniforms,GPUBufferUsage.UNIFORM),encoder=device.createCommandEncoder();
   const pass=encoder.beginRenderPass({colorAttachments:[{view:target.createView(),loadOp:'clear',clearValue:[0,0,0,0],storeOp:'store'}]});pass.setPipeline(pipeline);pass.setVertexBuffer(0,vertex);
   pass.setBindGroup(0,device.createBindGroup({layout:pipeline.getBindGroupLayout(0),entries:[]}));
   pass.setBindGroup(1,device.createBindGroup({layout:pipeline.getBindGroupLayout(1),entries:[{binding:0,resource:{buffer:viewport}}]}));
   pass.setBindGroup(2,device.createBindGroup({layout:pipeline.getBindGroupLayout(2),entries:resources}));
   pass.setBindGroup(3,device.createBindGroup({layout:pipeline.getBindGroupLayout(3),entries:[{binding:0,resource:{buffer:ubo}}]}));pass.draw(6);pass.end();encoder.copyTextureToBuffer({texture:target},{buffer:readback,bytesPerRow:256},[1,1]);device.queue.submit([encoder.finish()]);await readback.mapAsync(GPUMapMode.READ);const bytes=Array.from(new Uint8Array(readback.getMappedRange()).slice(0,4));readback.unmap();ubo.destroy();if(errors.length)throw Error(errors.join('\n'));return bytes;}
  const checks=[];function expect(name,actual,expected){if(JSON.stringify(actual)!==JSON.stringify(expected))throw Error(name+' '+JSON.stringify({actual,expected}));checks.push({name,actual});}
  const weights=[51200,8192,4096,2048],alpha=paints.reduce((n,p,i)=>n+p[3]*weights[i],0);
  const straight=[0,1,2].map(c=>Math.floor(paints.reduce((n,p,i)=>n+p[c]*p[3]*weights[i],0)/alpha)).concat(Math.floor((alpha+32768)/65536));
  const pma=p=>p.slice(0,3).map(v=>Math.floor((v*p[3]+127)/255)).concat(p[3]);
  for(const[flags,name,expected]of[[0,'source',paints[0]],[1,'weighted',straight],[3,'weighted-PMA',pma(straight)]]){const params=new Uint32Array(36);params.set([1,4,15,flags]);expect(name,await draw('terrain-compose',params,entries),expected);}
  // All alpha levels and a missing material retain unsigned integer rounding.
  for(const mask of[0,1,3,7]){const params=new Uint32Array(36);params.set([1,4,mask,1]);const a=paints.reduce((n,p,i)=>n+((mask>>i)&1)*p[3]*weights[i],0);const expected=[0,1,2].map(c=>a?Math.floor(paints.reduce((n,p,i)=>n+((mask>>i)&1)*p[c]*p[3]*weights[i],0)/a):0).concat(Math.floor((a+32768)/65536));expect('presence-'+mask,await draw('terrain-compose',params,entries),expected);}
  const rawPixels=[[100,20,200,255],[200,80,30,128],[10,250,90,64],[255,255,255,0]],raw=texture('rgba8unorm',new Uint8Array(rawPixels.flat()),2,2);
  const sumAlpha=rawPixels.reduce((n,p)=>n+p[3],0),reduced=[0,1,2].map(c=>Math.floor((rawPixels.reduce((n,p)=>n+p[c]*p[3],0)+Math.floor(sumAlpha/2))/sumAlpha)).concat(Math.floor((sumAlpha+2)/4));
  for(const encoded of[false,true])expect('reduce-'+(encoded?'PMA':'straight'),await draw('terrain-reduce',new Uint32Array([2|(encoded?65536:0),0,0,0]),[{binding:0,resource:raw.createView()}]),encoded?pma(reduced):reduced);
  const tiledPixels=Array.from({length:32*32},(_,i)=>rawPixels[(i%32)%2+Math.floor(i/32)%2*2]),tiled=texture('rgba8unorm',new Uint8Array(tiledPixels.flat()),32,32);
  for(const divisor of[1,2,4,8,16,32]){const origin=Math.floor((32/divisor)*.5)*divisor,group=[];for(let y=0;y<divisor;y++)for(let x=0;x<divisor;x++)group.push(tiledPixels[(origin+y)*32+origin+x]);const a=group.reduce((n,p)=>n+p[3],0),count=divisor*divisor,expected=[0,1,2].map(c=>a?Math.floor((group.reduce((n,p)=>n+p[c]*p[3],0)+Math.floor(a/2))/a):0).concat(Math.floor((a+Math.floor(count/2))/count));
   for(const encoded of[false,true])expect('divisor-'+divisor+'-'+encoded,await draw('terrain-reduce',new Uint32Array([divisor|(encoded?65536:0),0,0,0]),[{binding:0,resource:tiled.createView()}]),encoded?pma(expected):expected);
  }
  await device.queue.onSubmittedWorkDone();return{sourceRevision:metadata.sourceRevision,adapter:adapter.info,checks,errors};
 },{metadata,sources});writeFileSync(join(directory,'terrain-webgpu-proof.json'),JSON.stringify(result,null,2)+'\n');console.log(`Terrain WebGPU proof passed: ${result.checks.length} exact integer fixtures`);
}finally{await browser.close();}
