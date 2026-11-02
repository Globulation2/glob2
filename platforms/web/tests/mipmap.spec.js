// SPDX-License-Identifier: GPL-3.0-or-later
const {test,expect}=require('@playwright/test');
const fs=require('node:fs');
const path=require('node:path');
const {openRuntimeHost}=require('./runtime-host');
test('WebGL2 mip preparation matches integer CPU filtering and preserves renderer state',async({page})=>{
  await openRuntimeHost(page,'<canvas id="mips"></canvas><script>var GL={getSource:s=>s},Module={};</script>');
  await page.addScriptTag({content:fs.readFileSync(path.resolve(__dirname,'../webgl-shaders.js'),'utf8')});
  const results=await page.evaluate(()=>{
    const gl=document.querySelector('canvas').getContext('webgl2',{antialias:false});
    if(!gl)throw Error('WebGL2 unavailable');
    const program=gl.createProgram();
    for(const [type,source] of [[gl.VERTEX_SHADER,'#version 300 es\nvoid main(){gl_Position=vec4(0,0,0,1);}'],
      [gl.FRAGMENT_SHADER,'#version 300 es\nprecision highp float;out vec4 c;void main(){c=vec4(1);}']]){
      const shader=gl.createShader(type);gl.shaderSource(shader,source);gl.compileShader(shader);
      if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(shader));
      gl.attachShader(program,shader);gl.deleteShader(shader);
    }
    gl.linkProgram(program);if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));
    const vao=gl.createVertexArray(),draw=gl.createFramebuffer(),read=gl.createFramebuffer();
    let stateQueries=0;
    const getParameter=gl.getParameter.bind(gl);
    gl.getParameter=name=>{++stateQueries;return getParameter(name);};
    const results=[];
    for(const [width,height] of [[8,4],[1,8],[8,1],[1,1]]) for(const mode of [0,1,2]) for(const shared of [false,true]) for(const bgra of [false,true]){
      let pixels=new Uint8Array(shared?new SharedArrayBuffer(width*height*4):new ArrayBuffer(width*height*4));
      for(let i=0;i<pixels.length/4;i++){
        pixels.set([(i*37+19)%256,(i*71+23)%256,(i*53+11)%256,
          mode===0?0:mode===1?255:(i*43)%256],i*4);
      }
      const texture=gl.createTexture(),savedTexture=gl.createTexture(),sampler=gl.createSampler();
      const unpackBuffer=gl.createBuffer();gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER,unpackBuffer);
      gl.bufferData(gl.PIXEL_UNPACK_BUFFER,16,gl.STATIC_DRAW);
      gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,savedTexture);gl.bindSampler(0,sampler);
      gl.activeTexture(gl.TEXTURE3);gl.viewport(3,5,7,9);gl.colorMask(false,true,false,true);
      gl.useProgram(program);gl.bindVertexArray(vao);gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER,draw);gl.bindFramebuffer(gl.READ_FRAMEBUFFER,read);
      gl.enable(gl.BLEND);gl.enable(gl.SCISSOR_TEST);gl.enable(gl.DITHER);gl.enable(gl.RASTERIZER_DISCARD);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT,8);gl.pixelStorei(gl.UNPACK_ROW_LENGTH,13);
      gl.pixelStorei(gl.UNPACK_SKIP_ROWS,2);gl.pixelStorei(gl.UNPACK_SKIP_PIXELS,3);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,true);gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL,true);
      const upload=new Uint8Array(shared?new SharedArrayBuffer(pixels.length):new ArrayBuffer(pixels.length));
      upload.set(pixels);
      if(bgra)for(let i=0;i<upload.length;i+=4){upload[i]=pixels[i+2];upload[i+2]=pixels[i];}
      const beforeQueries=stateQueries;
      Module.glob2GenerateMipmaps(gl,texture,upload,width,height,bgra);
      const uploadQueries=stateQueries-beforeQueries;
      const preserved=gl.getParameter(gl.ACTIVE_TEXTURE)===gl.TEXTURE3 &&
        gl.getParameter(gl.CURRENT_PROGRAM)===program&&gl.getParameter(gl.VERTEX_ARRAY_BINDING)===vao&&
        gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING)===draw&&gl.getParameter(gl.READ_FRAMEBUFFER_BINDING)===read&&
        String(gl.getParameter(gl.VIEWPORT))==='3,5,7,9' && String(gl.getParameter(gl.COLOR_WRITEMASK))==='false,true,false,true' &&
        gl.isEnabled(gl.BLEND)&&gl.isEnabled(gl.SCISSOR_TEST)&&gl.isEnabled(gl.DITHER)&&gl.isEnabled(gl.RASTERIZER_DISCARD)&&
        gl.getParameter(gl.UNPACK_ALIGNMENT)===8&&gl.getParameter(gl.UNPACK_ROW_LENGTH)===13&&
        gl.getParameter(gl.UNPACK_SKIP_ROWS)===2&&gl.getParameter(gl.UNPACK_SKIP_PIXELS)===3&&
        gl.getParameter(gl.UNPACK_FLIP_Y_WEBGL)&&gl.getParameter(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL);
      gl.activeTexture(gl.TEXTURE0);
      const bindings=gl.getParameter(gl.TEXTURE_BINDING_2D)===savedTexture&&gl.getParameter(gl.SAMPLER_BINDING)===sampler&&
        gl.getParameter(gl.PIXEL_UNPACK_BUFFER_BINDING)===unpackBuffer;
      const framebuffer=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,framebuffer);
      let w=width,h=height,mip=0,matches=true;
      for(;;){
        gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,texture,mip);
        if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('Incomplete mip '+mip+' error '+gl.getError());
        const actual=new Uint8Array(w*h*4);gl.readPixels(0,0,w,h,gl.RGBA,gl.UNSIGNED_BYTE,actual);
        matches=matches&&actual.every((v,i)=>v===pixels[i]);
        if(w===1&&h===1)break;
        const nw=Math.max(1,w/2),nh=Math.max(1,h/2),next=new Uint8Array(nw*nh*4);
        // Independent CPU reference, as used by ImageAssets' padded-filter test.
        for(let y=0;y<nh;y++)for(let x=0;x<nw;x++){
          let alpha=0,rgb=[0,0,0];
          for(let dy=0;dy<2;dy++)for(let dx=0;dx<2;dx++){
            const i=(Math.min(h-1,y*2+dy)*w+Math.min(w-1,x*2+dx))*4;
            alpha+=pixels[i+3];for(let c=0;c<3;c++)rgb[c]+=pixels[i+c]*pixels[i+3];
          }
          const i=(y*nw+x)*4;next[i+3]=Math.floor((alpha+2)/4);
          for(let c=0;c<3;c++)next[i+c]=alpha?Math.floor((rgb[c]+Math.floor(alpha/2))/alpha):0;
        }
        pixels=next;w=nw;h=nh;++mip;
      }
      results.push({width,height,mode,shared,bgra,matches,preserved,bindings,uploadQueries,error:gl.getError()});
      gl.deleteFramebuffer(framebuffer);
      gl.deleteSampler(sampler);gl.deleteTexture(texture);gl.deleteTexture(savedTexture);
      gl.deleteBuffer(unpackBuffer);
      // Deletion implicitly unbinds objects, without a separate setter call.
      const empty=gl.createTexture(),queries=stateQueries;
      Module.glob2GenerateMipmaps(gl,empty,new Uint8Array(4),1,1);
      if(stateQueries!==queries || gl.getParameter(gl.TEXTURE_BINDING_2D)!==null ||
        gl.getParameter(gl.SAMPLER_BINDING)!==null || gl.getParameter(gl.PIXEL_UNPACK_BUFFER_BINDING)!==null ||
        gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING)!==null || gl.getParameter(gl.READ_FRAMEBUFFER_BINDING)!==null ||
        gl.getError()!==0)throw Error('Deleted binding retained');
      gl.deleteTexture(empty);
    }
    gl.deleteVertexArray(vao);gl.deleteFramebuffer(draw);gl.deleteFramebuffer(read);
    const unused=gl.createTexture();
    Module.glob2ForgetMipmaps(gl);
    const queries=stateQueries;
    Module.glob2GenerateMipmaps(gl,unused,new Uint8Array(4),1,1);
    if(stateQueries===queries || gl.getParameter(gl.VERTEX_ARRAY_BINDING)!==null ||
      gl.getParameter(gl.CURRENT_PROGRAM)!==program || gl.getError()!==0)throw Error('State mirror was not reset');
    gl.deleteProgram(program);
    if(Module.glob2GenerateMipmaps(gl,unused,new Uint8Array(4),1,1)!==false ||
      gl.getParameter(gl.CURRENT_PROGRAM)!==program || gl.getError()!==0)throw Error('Pending program deletion changed');
    gl.useProgram(null);gl.deleteTexture(unused);
    return results;
  });
  expect(results).toHaveLength(48);
  expect(results[0].uploadQueries).toBeGreaterThan(0);
  for(const result of results.slice(1)) expect(result.uploadQueries).toBe(0);
  for(const result of results){expect(result.matches,JSON.stringify(result)).toBe(true);expect(result.preserved).toBe(true);expect(result.bindings).toBe(true);expect(result.error).toBe(0);}
});
