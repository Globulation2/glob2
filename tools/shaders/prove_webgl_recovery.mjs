#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Generated shader/resource restoration proof; application lifecycle is tested separately.
import { createRequire } from 'node:module';
import { readFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'@playwright/test');
const directory=resolve(process.argv[2]||'artifacts/shaders/generated');
const sources=Object.fromEntries(['vert','frag'].map(stage=>[stage,readFileSync(join(directory,'webgl2',`primitive.${stage}`),'utf8')]));
async function prove(canvas,sources){
  const gl=canvas.getContext('webgl2',{antialias:false,preserveDrawingBuffer:true});
  if(!gl)throw Error('WebGL2 unavailable');
  const loss=gl.getExtension('WEBGL_lose_context');if(!loss)throw Error('Context-loss injection unavailable');
  // Owned source bytes and logical geometry survive device object invalidation.
  const pixels=new Uint8Array([43,160,210,255]);
  const vertices=new Float32Array([0,0,1,1,1,1,0,0, 8,0,1,1,1,1,1,0, 0,8,1,1,1,1,0,1,
    0,8,1,1,1,1,0,1, 8,0,1,1,1,1,1,0, 8,8,1,1,1,1,1,1]);
  function rebuild(){
    const program=gl.createProgram();
    for(const stage of ['vert','frag']){const shader=gl.createShader(stage==='vert'?gl.VERTEX_SHADER:gl.FRAGMENT_SHADER);
      gl.shaderSource(shader,sources[stage]);gl.compileShader(shader);
      if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(shader));gl.attachShader(program,shader);}
    gl.linkProgram(program);if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));
    gl.useProgram(program);gl.bindVertexArray(gl.createVertexArray());gl.bindBuffer(gl.ARRAY_BUFFER,gl.createBuffer());gl.bufferData(gl.ARRAY_BUFFER,vertices,gl.STATIC_DRAW);
    for(const [location,size,offset] of [[0,2,0],[1,4,8],[2,2,24]]){gl.enableVertexAttribArray(location);gl.vertexAttribPointer(location,size,gl.FLOAT,false,32,offset);}
    const texture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,texture);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
    gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA8,1,1,0,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
    gl.uniform1i(gl.getUniformLocation(program,'image'),0);gl.uniform1f(gl.getUniformLocation(program,'textured'),1);gl.uniform1f(gl.getUniformLocation(program,'alpha'),1);
    gl.uniform4f(gl.getUniformLocation(program,'uvBounds'),0,0,1,1);gl.uniform2f(gl.getUniformLocation(program,'viewport'),8,8);
    gl.viewport(0,0,8,8);gl.drawArrays(gl.TRIANGLES,0,6);
    const result=new Uint8Array(8*8*4);gl.readPixels(0,0,8,8,gl.RGBA,gl.UNSIGNED_BYTE,result);
    if(gl.getError()!==gl.NO_ERROR)throw Error('Restored draw failed');
    for(let i=0;i<result.length;i++)if(result[i]!==pixels[i%4])throw Error(`Unexpected channel ${i}: ${result[i]}`);
    return program;
  }
  const initial=rebuild();let lossEvents=0,restoreEvents=0;
  function event(name,callback){return new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error(`${name} timed out`)),10000);
    canvas.addEventListener(name,e=>{callback(e);clearTimeout(timer);resolve();},{once:true});});}
  const lost=event('webglcontextlost',e=>{e.preventDefault();lossEvents++;});loss.loseContext();await lost;
  if(!gl.isContextLost())throw Error('Device did not enter lost state');
  const restored=event('webglcontextrestored',()=>restoreEvents++);
  // Restoration is permitted after the lost-event dispatch task has finished.
  await new Promise(resolve=>setTimeout(resolve,0));loss.restoreContext();await restored;
  if(gl.isProgram(initial))throw Error('Stale shader object survived loss');
  rebuild();return{lossEvents,restoreEvents,pixelChannels:256,retainedSourceBytes:pixels.byteLength+vertices.byteLength};
}
const browser=await chromium.launch({executablePath:process.env.PLAYWRIGHT_CHROMIUM,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
try{
  const page=await browser.newPage();
  const result=await page.evaluate(async({sources,implementation})=>{
    const prove=eval(`(${implementation})`);const canvas=document.createElement('canvas');canvas.width=canvas.height=8;document.body.appendChild(canvas);
    const serial=await prove(canvas,sources);
    const workerCanvas=document.createElement('canvas');workerCanvas.width=workerCanvas.height=8;document.body.appendChild(workerCanvas);
    const source=`const prove=(${implementation});onmessage=async e=>{try{postMessage({result:await prove(e.data.canvas,e.data.sources)});}catch(error){postMessage({error:String(error)});}};`;
    const url=URL.createObjectURL(new Blob([source],{type:'text/javascript'}));const worker=new Worker(url);const offscreen=workerCanvas.transferControlToOffscreen();
    try{const threaded=await new Promise((resolve,reject)=>{worker.onmessage=e=>e.data.error?reject(Error(e.data.error)):resolve(e.data.result);worker.onerror=e=>reject(Error(e.message));worker.postMessage({canvas:offscreen,sources},[offscreen]);});return{serial,threaded};}
    finally{worker.terminate();URL.revokeObjectURL(url);}
  },{sources,implementation:prove.toString()});
  console.log(JSON.stringify({browser:await browser.version(),...result},null,2));
}finally{await browser.close();}
