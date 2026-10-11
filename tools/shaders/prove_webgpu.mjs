#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Actual generated skin materials, deformation, shells and depth on WebGPU.
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'@playwright/test');
const directory=resolve(process.argv[2]||'artifacts/shaders/generated');
const metadata=JSON.parse(readFileSync(join(directory,'metadata.json'),'utf8'));
const shaders=Object.fromEntries(metadata.variants.map(v=>[v,Object.fromEntries(['vert','frag'].map(s=>[s,readFileSync(join(directory,'webgpu',`${v}.${s}.wgsl`),'utf8')]))]));
const browser=await chromium.launch({executablePath:process.env.PLAYWRIGHT_CHROMIUM,args:['--enable-unsafe-webgpu','--use-vulkan=swiftshader','--use-angle=swiftshader']});
try{
  const page=await browser.newPage();await page.goto(process.env.SHADER_PROOF_URL||'http://127.0.0.1:8784/');
  const result=await page.evaluate(async({metadata,shaders,fixtures})=>{
    const adapter=await navigator.gpu.requestAdapter();if(!adapter)throw Error('No WebGPU adapter');
    const device=await adapter.requestDevice();const errors=[];device.addEventListener('uncapturederror',e=>errors.push(e.error.message));
    const target=device.createTexture({size:[128,128],format:'rgba8unorm',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});
    const depth=device.createTexture({size:[128,128],format:'depth24plus',usage:GPUTextureUsage.RENDER_ATTACHMENT});
    const readback=device.createBuffer({size:128*128*4,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
    const pipelines={};
    for(const [variant,source]of Object.entries(shaders)){
      const modules={};for(const [stage,code]of Object.entries(source)){modules[stage]=device.createShaderModule({code});const info=await modules[stage].getCompilationInfo();if(info.messages.some(m=>m.type==='error'))throw Error(variant+' '+stage+' '+JSON.stringify(info.messages));}
      pipelines[variant]=await device.createRenderPipelineAsync({layout:'auto',vertex:{module:modules.vert,entryPoint:'main',buffers:[{arrayStride:metadata.vertexLayouts[variant].stride,attributes:metadata.vertexLayouts[variant].attributes.map(a=>({shaderLocation:a.location,offset:a.offset,format:'float32x'+a.components}))}]},fragment:{module:modules.frag,entryPoint:'main',targets:[{format:'rgba8unorm'}]},primitive:{topology:'triangle-list'},depthStencil:{format:'depth24plus',depthWriteEnabled:true,depthCompare:'less-equal'}});
    }
    const vertices=[];
    function vertex(radius,angle){const x=radius*Math.cos(angle),y=radius*Math.sin(angle),z=Math.sqrt(Math.max(0,1-radius*radius));return[x*.8,y*.8,-z*.25,x,y,z,x*.5+.5,y*.5+.5,x*.5+.5,y*.5+.5];}
    for(let r=0;r<8;r++)for(let a=0;a<32;a++){const p=[vertex(r/8,a*Math.PI/16),vertex((r+1)/8,a*Math.PI/16),vertex((r+1)/8,(a+1)*Math.PI/16),vertex(r/8,(a+1)*Math.PI/16)];for(const i of[0,1,2,0,2,3])vertices.push(p[i]);}
    function buffer(data,usage){const b=device.createBuffer({size:data.byteLength,usage:usage|GPUBufferUsage.COPY_DST});device.queue.writeBuffer(b,0,data);return b;}
    const skinBuffer=buffer(new Float32Array(vertices.flat()),GPUBufferUsage.VERTEX);
    const rigBuffer=buffer(new Float32Array(vertices.flatMap(v=>[...v,0,0,0,0,1,0,0,0])),GPUBufferUsage.VERTEX);
    function texture(pixels){const t=device.createTexture({size:[1,1],format:'rgba8unorm',usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST});device.queue.writeTexture({texture:t},new Uint8Array(pixels),{},[1,1]);return t;}
    const paint=texture([237,146,82,255]),material=texture([0,0,0,255]),sampler=device.createSampler({magFilter:'nearest',minFilter:'nearest'});
    const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
    async function render(variant,id,shells,shift=0,depthShift=0,preserve=false){
      device.queue.writeTexture({texture:material},new Uint8Array([id,0,0,255]),{},[1,1]);
      const temporary=[];const pipeline=pipelines[variant],encoder=device.createCommandEncoder();
      const pass=encoder.beginRenderPass({colorAttachments:[{view:target.createView(),loadOp:preserve?'load':'clear',clearValue:[0,0,0,0],storeOp:'store'}],depthStencilAttachment:{view:depth.createView(),depthLoadOp:preserve?'load':'clear',depthClearValue:1,depthStoreOp:'store'}});
      pass.setPipeline(pipeline);pass.setVertexBuffer(0,variant==='skin'?skinBuffer:rigBuffer);
      const image=device.createBindGroup({layout:pipeline.getBindGroupLayout(2),entries:[{binding:0,resource:paint.createView()},{binding:1,resource:sampler},{binding:2,resource:material.createView()},{binding:3,resource:sampler}]});pass.setBindGroup(2,image);
      pass.setBindGroup(0,device.createBindGroup({layout:pipeline.getBindGroupLayout(0),entries:[]}));
      for(let k=0;k<=shells;k++){
        const uniforms=new Float32Array(metadata.uniformBlocks[variant].size/4);
        if(variant==='skin-rig'){for(let i=0;i<32;i++)uniforms.set(identity,i*16);uniforms[3]=0;uniforms[12]=shift;uniforms.set(identity,512);uniforms[526]=depthShift;uniforms.set([1,0,0,0,0,1,0,0,0,0,1,0],528);uniforms[540]=k/metadata.materials.shells;uniforms[541]=metadata.materials.furLength;uniforms[542]=metadata.materials.shellDepth;}else uniforms.set([k/metadata.materials.shells,metadata.materials.furLength,metadata.materials.shellDepth]);
        const vertexUBO=buffer(uniforms,GPUBufferUsage.UNIFORM),fragmentUBO=buffer(new Float32Array([0,0,k/metadata.materials.shells,0]),GPUBufferUsage.UNIFORM);
        temporary.push(vertexUBO,fragmentUBO);pass.setBindGroup(1,device.createBindGroup({layout:pipeline.getBindGroupLayout(1),entries:[{binding:0,resource:{buffer:vertexUBO}}]}));
        pass.setBindGroup(3,device.createBindGroup({layout:pipeline.getBindGroupLayout(3),entries:[{binding:0,resource:{buffer:fragmentUBO}}]}));pass.draw(vertices.length);
      }pass.end();encoder.copyTextureToBuffer({texture:target},{buffer:readback,bytesPerRow:512},[128,128]);device.queue.submit([encoder.finish()]);await readback.mapAsync(GPUMapMode.READ);const pixels=new Uint8Array(readback.getMappedRange()).slice();readback.unmap();temporary.forEach(b=>b.destroy());if(errors.length)throw Error(errors.join('\n'));return pixels;
    }
    const diff=(a,b)=>a.reduce((n,v,i)=>n+(v!==b[i]),0);const results=[],pixelFixtures={};
    for(const m of metadata.materials.materials){const body=await render('skin',m.id,0),rig=await render('skin-rig',m.id,0),moved=await render('skin-rig',m.id,0,.15);if(fixtures)pixelFixtures[m.id]=btoa(String.fromCharCode(...body));const covered=body.filter((v,i)=>i%4===3&&v).length;if(covered<1000)throw Error('Empty material '+m.key);if(diff(body,rig)>100)throw Error('Rest rig mismatch '+m.key+' '+diff(body,rig));if(diff(rig,moved)<1000)throw Error('Inactive deformation '+m.key);let shellDifference=0;if(m.shells){shellDifference=diff(body,await render('skin',m.id,metadata.materials.shells));if(shellDifference<100)throw Error('Inactive shells '+m.key);}results.push({key:m.key,covered,restDifference:diff(body,rig),deformationDifference:diff(rig,moved),shellDifference});}
    const near=await render('skin',0,0),occluded=await render('skin-rig',1,0,0,.8,true);if(diff(near,occluded))throw Error('Depth occlusion failed');await device.queue.onSubmittedWorkDone();if(errors.length)throw Error(errors.join('\n'));
    async function draw(variant,vertexData,vertexUniforms,fragmentUniforms=null,textured=true){
      const pipeline=pipelines[variant],encoder=device.createCommandEncoder(),temporary=[];
      const pass=encoder.beginRenderPass({colorAttachments:[{view:target.createView(),loadOp:'clear',clearValue:[0,0,0,0],storeOp:'store'}],depthStencilAttachment:{view:depth.createView(),depthLoadOp:'clear',depthClearValue:1,depthStoreOp:'store'}});
      pass.setPipeline(pipeline);const vb=buffer(new Float32Array(vertexData),GPUBufferUsage.VERTEX);temporary.push(vb);pass.setVertexBuffer(0,vb);
      for(let group=0;group<=(fragmentUniforms?3:2);group++){
        const entries=[];
        if(group===1){const ubo=buffer(new Float32Array(vertexUniforms),GPUBufferUsage.UNIFORM);temporary.push(ubo);entries.push({binding:0,resource:{buffer:ubo}});}
        if(group===2&&textured)entries.push({binding:0,resource:paint.createView()},{binding:1,resource:sampler});
        if(group===3){const ubo=buffer(new Float32Array(fragmentUniforms),GPUBufferUsage.UNIFORM);temporary.push(ubo);entries.push({binding:0,resource:{buffer:ubo}});}
        pass.setBindGroup(group,device.createBindGroup({layout:pipeline.getBindGroupLayout(group),entries}));
      }
      pass.draw(vertexData.length/(metadata.vertexLayouts[variant].stride/4));pass.end();encoder.copyTextureToBuffer({texture:target},{buffer:readback,bytesPerRow:512},[128,128]);device.queue.submit([encoder.finish()]);await readback.mapAsync(GPUMapMode.READ);const pixels=new Uint8Array(readback.getMappedRange()).slice();readback.unmap();temporary.forEach(b=>b.destroy());if(errors.length)throw Error(errors.join('\n'));return pixels;
    }
    const primitiveVertices=[16,16,1,0,0,1,.5,.5,112,16,1,0,0,1,.5,.5,16,112,1,0,0,1,.5,.5];
    const primitiveSamples=[];
    for(const textured of[0,1]){const pixels=await draw('primitive',primitiveVertices,[128,128,0,0],[textured,.5,0,0,0,0,1,1]);primitiveSamples.push(Array.from(pixels.slice((32*128+32)*4,(32*128+32)*4+4)));}
    if(JSON.stringify(primitiveSamples)!==JSON.stringify([[255,0,0,128],[237,0,0,128]]))throw Error('Primitive RGBA mismatch '+JSON.stringify(primitiveSamples));
    device.queue.writeTexture({texture:paint},new Uint8Array([120,80,40,128]),{},[1,1]);
    const worldVertices=[[-.8,-.8],[.8,-.8],[-.8,.8]].flatMap(([x,y])=>[x,y,.25,1,.5,.5,.5,.5,.5,0,0,1]);
    const worldSamples=[];
    for(const variant of['world','cloud']){const pixels=await draw(variant,worldVertices,[...identity,1,1,0,0],variant==='world'?[0,0,1,0]:null);worldSamples.push(Array.from(pixels.slice((96*128+32)*4,(96*128+32)*4+4)));}
    if(JSON.stringify(worldSamples)!==JSON.stringify([[60,40,20,255],[60,40,20,128]]))throw Error('World/cloud mismatch '+JSON.stringify(worldSamples));
    const pointVertices=[[16,16,0,0],[112,16,1,0],[112,112,1,1],[16,16,0,0],[112,112,1,1],[16,112,0,1]].flatMap(([x,y,u,v])=>[x,y,1,1,1,1,u,v]);
    const pointPixels=await draw('point',pointVertices,[128,128,0,0],[0,1,0,0,0,0,1,1],false);
    if(pointPixels[(64*128+64)*4+3]!==255||pointPixels[(18*128+18)*4+3]!==0||!pointPixels.some((v,i)=>i%4===3&&v>0&&v<255))throw Error('Point interior/edge/AA mismatch');
    return{sourceRevision:metadata.sourceRevision,adapter:adapter.info,materials:results,depthOcclusion:true,primitiveSamples,worldSamples,smoothPoint:true,pixelFixtures,pipelineVariants:Object.keys(pipelines)};
  },{metadata,shaders,fixtures:!!process.env.SHADER_PROOF_PIXELS});for(const[id,pixels]of Object.entries(result.pixelFixtures))writeFileSync(join(directory,`webgpu-skin-${id}.rgba`),Buffer.from(pixels,'base64'));delete result.pixelFixtures;writeFileSync(join(directory,'webgpu-proof.json'),JSON.stringify(result,null,2)+'\n');console.log(`WebGPU proof passed: ${result.materials.length} materials, rest/posed GPU rig, shells, derivatives, depth; ${result.pipelineVariants.length} pipelines`);
}finally{await browser.close();}
