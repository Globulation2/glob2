#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise generated atlas shaders, all materials, GPU deformation and shells.
// PLAYWRIGHT_MODULE may point to an existing playwright-core package directory.
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || '@playwright/test');
const directory = resolve(process.argv[2] || 'artifacts/shaders/generated');
const shaders = Object.fromEntries(['skin', 'skin-rig', 'primitive', 'world', 'cloud', 'point', 'terrain-compose', 'terrain-reduce'].map(v => [v,
  Object.fromEntries(['vert', 'frag'].map(s => [s, readFileSync(join(directory, 'webgl2', `${v}.${s}`), 'utf8')]))]));
const metadata = JSON.parse(readFileSync(join(directory, 'metadata.json'), 'utf8'));
const browser = await chromium.launch({ executablePath: process.env.PLAYWRIGHT_CHROMIUM, args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
try {
  const page = await browser.newPage();
  const result = await page.evaluate(({ shaders, metadata, fixtures }) => {
    const canvas = document.createElement('canvas'); canvas.width = canvas.height = 128;
    document.body.appendChild(canvas);
    const gl = canvas.getContext('webgl2', { antialias: false, preserveDrawingBuffer: true });
    if (!gl) throw Error('WebGL2 unavailable');
    const programs = {};
    for (const [variant, sources] of Object.entries(shaders)) {
      const program = gl.createProgram();
      for (const [stage, source] of Object.entries(sources)) {
        const shader = gl.createShader(stage === 'vert' ? gl.VERTEX_SHADER : gl.FRAGMENT_SHADER);
        gl.shaderSource(shader, source); gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(shader));
        gl.attachShader(program, shader);
      }
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw Error(gl.getProgramInfoLog(program));
      programs[variant] = program;
    }
    // Triangulated curved surface; UV gradients and normals exercise actual
    // material lighting, derivative detail and outward shell placement.
    const vertices = [];
    function vertex(radius, angle) {
      const x = radius * Math.cos(angle), y = radius * Math.sin(angle), z = Math.sqrt(Math.max(0, 1-radius*radius));
      return [x*.8,y*.8,-z*.25,x,y,z,x*.5+.5,y*.5+.5,x*.5+.5,y*.5+.5,0,0,0,0,1,0,0,0];
    }
    for(let r=0;r<8;r++) for(let a=0;a<32;a++) {
      const points=[vertex(r/8,a*Math.PI/16),vertex((r+1)/8,a*Math.PI/16),vertex((r+1)/8,(a+1)*Math.PI/16),vertex(r/8,(a+1)*Math.PI/16)];
      for(const i of [0,1,2,0,2,3]) vertices.push(...points[i]);
    }
    const buffer=gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER,buffer);
    gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(vertices),gl.STATIC_DRAW);
    const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
    const matrices=new Float32Array(32*16); for(let i=0;i<32;i++) matrices.set(identity,i*16);
    function texture(unit,pixels) {
      gl.activeTexture(gl.TEXTURE0+unit); const t=gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D,t);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
      gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,1,1,0,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array(pixels)); return t;
    }
    texture(0,[237,146,82,255]); const materialTexture=texture(1,[0,0,0,255]);
    gl.viewport(0,0,128,128); gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL);
    function render(variant,id,shells,shift=0,depthShift=0,preserve=false) {
      const p=programs[variant]; gl.useProgram(p);
      for(let i=0;i<5;i++) gl.disableVertexAttribArray(i);
      for(const a of metadata.vertexLayouts[variant].attributes) {
        gl.enableVertexAttribArray(a.location); gl.vertexAttribPointer(a.location,a.components,gl.FLOAT,false,72,a.offset);
      }
      gl.uniform1i(gl.getUniformLocation(p,'paint'),0); gl.uniform1i(gl.getUniformLocation(p,'material'),1);
      gl.uniform2f(gl.getUniformLocation(p,'region'),0,0);
      gl.uniform1f(gl.getUniformLocation(p,'furLength'),metadata.materials.furLength);
      gl.uniform1f(gl.getUniformLocation(p,'shellDepth'),metadata.materials.shellDepth);
      if(variant==='skin-rig') {
        matrices[12]=shift;
        gl.uniformMatrix4fv(gl.getUniformLocation(p,'bones[0]'),false,matrices);
        const view=identity.slice(); view[14]=depthShift; gl.uniformMatrix4fv(gl.getUniformLocation(p,'view'),false,view);
        gl.uniformMatrix3fv(gl.getUniformLocation(p,'normalView'),false,[1,0,0,0,1,0,0,0,1]);
      }
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D,materialTexture);
      gl.texSubImage2D(gl.TEXTURE_2D,0,0,0,1,1,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array([id,0,0,255]));
      gl.clearColor(0,0,0,0); if(!preserve)gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
      for(let k=0;k<=shells;k++) { gl.uniform1f(gl.getUniformLocation(p,'shell'),k/metadata.materials.shells); gl.drawArrays(gl.TRIANGLES,0,vertices.length/18); }
      const pixels=new Uint8Array(128*128*4); gl.readPixels(0,0,128,128,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
      if(gl.getError()!==gl.NO_ERROR) throw Error('GL error during '+variant+' '+id);
      return pixels;
    }
    const different=(a,b)=>a.reduce((n,v,i)=>n+(v!==b[i]),0);
    const results=[],pixelFixtures={};
    for(const m of metadata.materials.materials) {
      const body=render('skin',m.id,0), rig=render('skin-rig',m.id,0), moved=render('skin-rig',m.id,0,.15);
      if(fixtures)pixelFixtures[m.id]=btoa(String.fromCharCode(...body));
      const covered=body.filter((v,i)=>i%4===3&&v).length;
      if(covered<1000) throw Error('Empty material '+m.key);
      if(different(body,rig)>100) throw Error('Rest rig mismatch '+m.key);
      if(different(rig,moved)<1000) throw Error('Deformation inactive '+m.key);
      let shellDifference=0;
      if(m.shells) {shellDifference=different(body,render('skin',m.id,metadata.materials.shells)); if(shellDifference<100) throw Error('Shells inactive '+m.key);}
      results.push({id:m.id,key:m.key,covered,restDifference:different(body,rig),deformationDifference:different(rig,moved),shellDifference});
    }
    const near=render('skin',0,0), occluded=render('skin-rig',1,0,0,.8,true);
    if(different(near,occluded)!==0)throw Error('Depth occlusion failed');
    gl.disable(gl.DEPTH_TEST);
    const primitive=programs.primitive;gl.useProgram(primitive);
    gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([16,16,1,0,0,1,.5,.5,112,16,1,0,0,1,.5,.5,16,112,1,0,0,1,.5,.5]),gl.STATIC_DRAW);
    for(let i=0;i<5;i++)gl.disableVertexAttribArray(i);
    for(const a of metadata.vertexLayouts.primitive.attributes){gl.enableVertexAttribArray(a.location);gl.vertexAttribPointer(a.location,a.components,gl.FLOAT,false,32,a.offset);}
    gl.uniform2f(gl.getUniformLocation(primitive,'viewport'),128,128);gl.uniform1i(gl.getUniformLocation(primitive,'image'),0);
    gl.uniform1f(gl.getUniformLocation(primitive,'alpha'),.5);
    gl.uniform4f(gl.getUniformLocation(primitive,'uvBounds'),0,0,1,1);
    const sample=[];
    for(const textured of [0,1]){
      gl.uniform1f(gl.getUniformLocation(primitive,'textured'),textured);gl.clear(gl.COLOR_BUFFER_BIT);gl.drawArrays(gl.TRIANGLES,0,3);
      const pixel=new Uint8Array(4);gl.readPixels(32,128-33,1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixel);sample.push(Array.from(pixel));
    }
    if(sample[0][0]!==255||sample[1][0]!==237||sample.some(p=>p[1]!==0||p[2]!==0||p[3]!==128))throw Error('Primitive flat/texture/alpha failed '+JSON.stringify(sample));
    gl.activeTexture(gl.TEXTURE0);
    gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,2,1,0,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array([31,0,0,255,197,0,0,255]));
    gl.uniform1f(gl.getUniformLocation(primitive,'textured'),1);
    const boundsSamples=[];
    for(const centre of [.25,.75]){
      gl.uniform4f(gl.getUniformLocation(primitive,'uvBounds'),centre,.5,centre,.5);
      gl.clear(gl.COLOR_BUFFER_BIT);gl.drawArrays(gl.TRIANGLES,0,3);
      const pixel=new Uint8Array(4);gl.readPixels(32,128-33,1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixel);boundsSamples.push(pixel[0]);
    }
    if(boundsSamples[0]!==31||boundsSamples[1]!==197)throw Error('Primitive atlas bounds failed');
    if(gl.getError()!==gl.NO_ERROR)throw Error('Primitive GL error');
    // Canonical depth, diffuse lighting/specular, cloud alpha and smooth-point
    // coverage use the same generated programs consumed by each backend.
    gl.activeTexture(gl.TEXTURE0);
    gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,1,1,0,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array([120,80,40,128]));
    const worldVertices=[[-.8,-.8],[.8,-.8],[-.8,.8]].flatMap(([x,y])=>[x,y,.25,1,.5,.5,.5,.5,.5,0,0,1]);
    gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(worldVertices),gl.STATIC_DRAW);
    const worldSamples=[];
    for(const variant of ['world','cloud']) {
      const p=programs[variant];gl.useProgram(p);
      for(let i=0;i<5;i++)gl.disableVertexAttribArray(i);
      for(const a of metadata.vertexLayouts[variant].attributes){gl.enableVertexAttribArray(a.location);gl.vertexAttribPointer(a.location,a.components,gl.FLOAT,false,48,a.offset);}
      gl.uniformMatrix4fv(gl.getUniformLocation(p,'projection'),false,identity);
      gl.uniform4f(gl.getUniformLocation(p,'uvTransform'),1,1,0,0);
      gl.uniform4f(gl.getUniformLocation(p,'sunSpecular'),0,0,1,0);
      gl.uniform1i(gl.getUniformLocation(p,'world'),0);
      gl.clear(gl.COLOR_BUFFER_BIT);gl.drawArrays(gl.TRIANGLES,0,3);
      const pixel=new Uint8Array(4);gl.readPixels(32,32,1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixel);worldSamples.push(Array.from(pixel));
    }
    if(JSON.stringify(worldSamples)!==JSON.stringify([[60,40,20,255],[60,40,20,128]]))throw Error('World/cloud lighting or alpha failed '+JSON.stringify(worldSamples));
    gl.useProgram(programs.point);
    gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([[16,16,0,0],[112,16,1,0],[112,112,1,1],[16,16,0,0],[112,112,1,1],[16,112,0,1]].flatMap(([x,y,u,v])=>[x,y,1,1,1,1,u,v])),gl.STATIC_DRAW);
    for(let i=0;i<5;i++)gl.disableVertexAttribArray(i);
    for(const a of metadata.vertexLayouts.point.attributes){gl.enableVertexAttribArray(a.location);gl.vertexAttribPointer(a.location,a.components,gl.FLOAT,false,32,a.offset);}
    gl.uniform2f(gl.getUniformLocation(programs.point,'viewport'),128,128);gl.uniform1f(gl.getUniformLocation(programs.point,'alpha'),1);
    gl.clear(gl.COLOR_BUFFER_BIT);gl.drawArrays(gl.TRIANGLES,0,6);
    const pointPixels=new Uint8Array(128*128*4);gl.readPixels(0,0,128,128,gl.RGBA,gl.UNSIGNED_BYTE,pointPixels);
    if(pointPixels[(64*128+64)*4+3]!==255||pointPixels[(18*128+18)*4+3]!==0||!pointPixels.some((v,i)=>i%4===3&&v>0&&v<255))throw Error('Smooth point interior/edge/AA failed');
    if(gl.getError()!==gl.NO_ERROR)throw Error('Torus pass GL error');
    return {sourceRevision:metadata.sourceRevision,renderer:gl.getParameter(gl.RENDERER),depthOcclusion:true,primitiveSamples:sample,boundsSamples,worldSamples,smoothPoint:true,pixelFixtures,vertexFixture:fixtures?btoa(String.fromCharCode(...new Uint8Array(new Float32Array(vertices).buffer))):null,materials:results};
  }, { shaders, metadata, fixtures:!!process.env.SHADER_PROOF_PIXELS });
  for(const[id,pixels]of Object.entries(result.pixelFixtures))writeFileSync(join(directory,`webgl-skin-${id}.rgba`),Buffer.from(pixels,'base64'));
  if(result.vertexFixture){const rig=Buffer.from(result.vertexFixture,'base64');writeFileSync(join(directory,'webgl-rig-vertices.f32'),rig);const skin=Buffer.alloc(rig.length/72*40);for(let i=0;i<rig.length/72;i++)rig.copy(skin,i*40,i*72,i*72+40);writeFileSync(join(directory,'webgl-skin-vertices.f32'),skin);}
  delete result.vertexFixture;delete result.pixelFixtures;
  writeFileSync(join(directory, 'webgl-proof.json'), JSON.stringify(result,null,2)+'\n');
  console.log(`WebGL2 proof passed: ${result.materials.length} materials, rest/posed GPU rig, depth, derivatives and fur shells`);
} finally { await browser.close(); }
