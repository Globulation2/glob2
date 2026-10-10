// SPDX-License-Identifier: GPL-3.0-or-later
// Legacy GL emulation prepends this WebGL1 extension when it sees derivatives.
// GLSL ES 3.00 has derivatives built in and requires #version to come first.
// Run after the SDK defines GL, in both the UI and application pthread realms.
const glob2OriginalShaderSource = GL.getSource;
GL.getSource = (...args) => {
  const source = glob2OriginalShaderSource(...args);
  const prefix = '#extension GL_OES_standard_derivatives : enable\n';
  return source.startsWith(prefix + '#version 300 es') ? source.slice(prefix.length) : source;
};

// Generated HD terrain changes during animation. Preparing its entire mip
// chain on the application thread is much slower than the drawing itself.
// Integer arithmetic matches AssetImage::prepareUpload, including transparent
// RGB and rounding; ordinary gl.generateMipmap would filter straight alpha.
const glob2MipPipelines = new WeakMap();
const glob2MipContexts = new WeakMap();
const glob2MipCapabilities = gl => [gl.BLEND,gl.DEPTH_TEST,gl.STENCIL_TEST,gl.SCISSOR_TEST,gl.CULL_FACE,gl.DITHER,gl.RASTERIZER_DISCARD];
const glob2MipPixelStores = gl => [gl.UNPACK_ALIGNMENT,gl.UNPACK_ROW_LENGTH,gl.UNPACK_SKIP_ROWS,gl.UNPACK_SKIP_PIXELS,
  gl.UNPACK_FLIP_Y_WEBGL,gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL];
// Reading CURRENT_PROGRAM can synchronize the browser's GPU process. Track the
// setters used by both SDL and our raw WebGL passes instead of querying state
// for every animated terrain upload. Context restoration invalidates the mirror.
function glob2MipState(gl) {
  let context = glob2MipContexts.get(gl);
  if (!context) {
    const methods = ['activeTexture','bindTexture','bindSampler','useProgram','bindVertexArray',
      'bindFramebuffer','viewport','colorMask','pixelStorei','bindBuffer','enable','disable',
      'deleteTexture','deleteSampler','deleteVertexArray','deleteFramebuffer','deleteBuffer','deleteProgram'];
    context = {originals:Object.fromEntries(methods.map(name => [name,gl[name]])), state:null};
    glob2MipContexts.set(gl,context);
    for (const name of methods) gl[name] = function(...args) {
      const state = glob2MipState(gl);
      const result = context.originals[name].apply(this,args);
      if (this !== gl) return result;
      const [a,b] = args;
      switch (name) {
        case 'activeTexture': state.active=a; break;
        case 'bindTexture': if (a===gl.TEXTURE_2D && state.active===gl.TEXTURE0) state.texture=b; break;
        case 'bindSampler': if (a===0) state.sampler=b; break;
        case 'useProgram': state.program=a; break;
        case 'bindVertexArray': state.vao=a; break;
        case 'bindFramebuffer':
          if (a===gl.FRAMEBUFFER || a===gl.DRAW_FRAMEBUFFER) state.draw=b;
          if (a===gl.FRAMEBUFFER || a===gl.READ_FRAMEBUFFER) state.read=b;
          break;
        case 'viewport': state.viewport=args; break;
        case 'colorMask': state.mask=args; break;
        case 'pixelStorei': state.pixelStore.set(a,b); break;
        case 'bindBuffer': if (a===gl.PIXEL_UNPACK_BUFFER) state.unpackBuffer=b; break;
        case 'enable': state.enabled.add(a); break;
        case 'disable': state.enabled.delete(a); break;
        case 'deleteTexture': if (state.texture===a) state.texture=null; break;
        case 'deleteSampler': if (state.sampler===a) state.sampler=null; break;
        case 'deleteVertexArray': if (state.vao===a) state.vao=null; break;
        case 'deleteFramebuffer':
          if (state.draw===a) state.draw=null;
          if (state.read===a) state.read=null;
          break;
        case 'deleteBuffer': if (state.unpackBuffer===a) state.unpackBuffer=null; break;
        // A current program remains bound until useProgram replaces it.
        // Temporarily switching away would destroy a deletion-pending program.
        case 'deleteProgram': if (a) state.deletedPrograms.add(a); break;
      }
      return result;
    };
  }
  if (!context.state) {
    const active=gl.getParameter(gl.ACTIVE_TEXTURE);
    context.originals.activeTexture.call(gl,gl.TEXTURE0);
    try {
      context.state = {active,texture:gl.getParameter(gl.TEXTURE_BINDING_2D),sampler:gl.getParameter(gl.SAMPLER_BINDING),
        program:gl.getParameter(gl.CURRENT_PROGRAM),vao:gl.getParameter(gl.VERTEX_ARRAY_BINDING),
        draw:gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING),read:gl.getParameter(gl.READ_FRAMEBUFFER_BINDING),
        viewport:gl.getParameter(gl.VIEWPORT),mask:gl.getParameter(gl.COLOR_WRITEMASK),
        unpackBuffer:gl.getParameter(gl.PIXEL_UNPACK_BUFFER_BINDING),
        pixelStore:new Map(glob2MipPixelStores(gl).map(name=>[name,gl.getParameter(name)])),
        enabled:new Set(glob2MipCapabilities(gl).filter(cap=>gl.isEnabled(cap))),deletedPrograms:new WeakSet()};
      const program=context.state.program;
      if (program && gl.getProgramParameter(program,gl.DELETE_STATUS)) context.state.deletedPrograms.add(program);
    } finally { context.originals.activeTexture.call(gl,active); }
  }
  return context.state;
}
Module.glob2ForgetMipmaps = gl => {
  glob2MipPipelines.delete(gl);
  const context=glob2MipContexts.get(gl);
  if (context) context.state=null;
};
Module.glob2GenerateMipmaps = (gl, texture, pixels, width, height, bgra = false) => {
  const state=glob2MipState(gl);
  if (state.program && state.deletedPrograms.has(state.program)) return false;
  let pipeline = glob2MipPipelines.get(gl);
  if (!pipeline) {
    const shader = (type, source) => {
      const value = gl.createShader(type);
      gl.shaderSource(value, source); gl.compileShader(value);
      if (!gl.getShaderParameter(value, gl.COMPILE_STATUS)) {
        const error = gl.getShaderInfoLog(value); gl.deleteShader(value);
        throw new Error(error);
      }
      return value;
    };
    const vertex = shader(gl.VERTEX_SHADER, `#version 300 es
      void main() {
        vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
        gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
      }`);
    const fragment = shader(gl.FRAGMENT_SHADER, `#version 300 es
      precision highp float;
      precision highp int;
      uniform highp sampler2D source;
      uniform int level;
      uniform ivec2 extent;
      uniform bool convertBase;
      out vec4 color;
      uvec4 samplePixel(ivec2 p) {
        return uvec4(round(texelFetch(source, min(p, extent - 1), level) * 255.0));
      }
      void main() {
        if (convertBase) {
          color = texelFetch(source, ivec2(gl_FragCoord.xy), 0).bgra;
          return;
        }
        ivec2 p = ivec2(gl_FragCoord.xy) * 2;
        uvec4 a = samplePixel(p), b = samplePixel(p + ivec2(1, 0));
        uvec4 c = samplePixel(p + ivec2(0, 1)), d = samplePixel(p + ivec2(1, 1));
        uint alpha = a.a + b.a + c.a + d.a;
        uvec3 sum = a.rgb * a.a + b.rgb * b.a + c.rgb * c.a + d.rgb * d.a;
        uvec3 rgb = alpha == 0u ? uvec3(0) : (sum + uvec3(alpha / 2u)) / alpha;
        color = vec4(vec3(rgb), float((alpha + 2u) / 4u)) / 255.0;
      }`);
    const program = gl.createProgram();
    gl.attachShader(program, vertex); gl.attachShader(program, fragment); gl.linkProgram(program);
    gl.deleteShader(vertex); gl.deleteShader(fragment);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      const error = gl.getProgramInfoLog(program); gl.deleteProgram(program);
      throw new Error(error);
    }
    pipeline = {program, framebuffer:gl.createFramebuffer(), temporary:gl.createTexture(),
      vao:gl.createVertexArray(), source:gl.getUniformLocation(program,'source'),
      level:gl.getUniformLocation(program,'level'), extent:gl.getUniformLocation(program,'extent'),
      convertBase:gl.getUniformLocation(program,'convertBase')};
    glob2MipPipelines.set(gl, pipeline);
  }
  const active = state.active;
  gl.activeTexture(gl.TEXTURE0);
  const saved = {texture:state.texture,sampler:state.sampler,program:state.program,vao:state.vao,
    draw:state.draw,read:state.read,viewport:state.viewport,mask:state.mask,
    unpack:state.pixelStore.get(gl.UNPACK_ALIGNMENT),rowLength:state.pixelStore.get(gl.UNPACK_ROW_LENGTH),
    skipRows:state.pixelStore.get(gl.UNPACK_SKIP_ROWS),skipPixels:state.pixelStore.get(gl.UNPACK_SKIP_PIXELS),
    flip:state.pixelStore.get(gl.UNPACK_FLIP_Y_WEBGL),premultiply:state.pixelStore.get(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL),
    unpackBuffer:state.unpackBuffer};
  const capabilities = glob2MipCapabilities(gl);
  const enabled = capabilities.map(cap => state.enabled.has(cap));
  try {
    capabilities.forEach(cap => gl.disable(cap));
    gl.colorMask(true,true,true,true); gl.bindSampler(0,null);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT,1);
    gl.pixelStorei(gl.UNPACK_ROW_LENGTH,0); gl.pixelStorei(gl.UNPACK_SKIP_ROWS,0); gl.pixelStorei(gl.UNPACK_SKIP_PIXELS,0);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,false); gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL,false);
    gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER,null);
    gl.bindTexture(gl.TEXTURE_2D,texture);
    gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA8,width,height,0,gl.RGBA,gl.UNSIGNED_BYTE,bgra ? null : pixels);
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAX_LEVEL,0);
    gl.useProgram(pipeline.program); gl.bindVertexArray(pipeline.vao);
    gl.uniform1i(pipeline.source,0);
    gl.bindFramebuffer(gl.FRAMEBUFFER,pipeline.framebuffer);
    if (bgra) {
      // DrawableSurface stores BGRA bytes. Convert on the GPU before reducing,
      // rather than copying/converting an entire terrain page on the CPU.
      gl.bindTexture(gl.TEXTURE_2D,pipeline.temporary);
      gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA8,width,height,0,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAX_LEVEL,0);
      gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,texture,0);
      gl.uniform1i(pipeline.convertBase,1);gl.viewport(0,0,width,height);gl.drawArrays(gl.TRIANGLES,0,3);
      gl.bindTexture(gl.TEXTURE_2D,texture);
    }
    gl.uniform1i(pipeline.convertBase,0);
    let mip = 0;
    while (width > 1 || height > 1) {
      const w = Math.max(1,width >> 1), h = Math.max(1,height >> 1);
      gl.bindTexture(gl.TEXTURE_2D,pipeline.temporary);
      gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA8,w,h,0,gl.RGBA,gl.UNSIGNED_BYTE,null);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);
      gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,pipeline.temporary,0);
      gl.bindTexture(gl.TEXTURE_2D,texture);
      gl.uniform1i(pipeline.level,mip); gl.uniform2i(pipeline.extent,width,height);
      gl.viewport(0,0,w,h); gl.drawArrays(gl.TRIANGLES,0,3);
      ++mip;
      gl.texImage2D(gl.TEXTURE_2D,mip,gl.RGBA8,w,h,0,gl.RGBA,gl.UNSIGNED_BYTE,null);
      gl.copyTexSubImage2D(gl.TEXTURE_2D,mip,0,0,0,0,w,h);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAX_LEVEL,mip);
      width=w; height=h;
    }
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR_MIPMAP_LINEAR);
    return true;
  } finally {
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER,saved.draw); gl.bindFramebuffer(gl.READ_FRAMEBUFFER,saved.read);
    gl.useProgram(saved.program); gl.bindVertexArray(saved.vao);
    gl.viewport(...saved.viewport); gl.colorMask(...saved.mask);
    capabilities.forEach((cap,i) => enabled[i] ? gl.enable(cap) : gl.disable(cap));
    gl.pixelStorei(gl.UNPACK_ALIGNMENT,saved.unpack);
    gl.pixelStorei(gl.UNPACK_ROW_LENGTH,saved.rowLength); gl.pixelStorei(gl.UNPACK_SKIP_ROWS,saved.skipRows);
    gl.pixelStorei(gl.UNPACK_SKIP_PIXELS,saved.skipPixels); gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,saved.flip);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL,saved.premultiply); gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER,saved.unpackBuffer);
    gl.bindTexture(gl.TEXTURE_2D,saved.texture); gl.bindSampler(0,saved.sampler);
    gl.activeTexture(active);
  }
};
