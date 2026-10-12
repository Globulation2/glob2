// SPDX-License-Identifier: GPL-3.0-or-later
// Packed WebGPU submission. Browser objects remain in the presentation owner;
// every Wasm upload is copied to non-shared storage before the WebGPU call.
addToLibrary({
  $Glob2WebGPU: {
    state: 0, error: '', device: null, context: null, lost: false,
    shaders: {}, pipelines: new Map(), resources: new Map(), meshes: new Map(), meshBytes: 0, frames: [], activeFrame: null, captures: [], transients: new Set(), transientBytes: 0, allocationEpoch: 0, masks: new Map(), maskBytes: 0, scratch: new Map(), scratchBytes: 0,
    issued: 0, completed: 0, epoch: 0, width: 0, height: 0, screen: null, committed: null, bytes: 0, deferred: false, presentOwned: [], presentBytes: 0,
    handle(error){if(error?.deferred){this.deferred=true;this.error=String(error.message);}else this.fail(error);},
    fail(error) { this.error=String(error?.message||error);this.state=-1;this.lost=true; },
    async prepare(root, ownedSources=null) {
      if(this.state===0.5)return;
      if(this.device&&!this.lost){this.state=1;return;}
      const epoch=++this.epoch;
      this.state=0.5;this.error='';this.lost=false;
      const current=()=>epoch===this.epoch && !this.lost;
      try {
        if(ownedSources?.error)throw Error(ownedSources.error);
        const gpu=globalThis.navigator?.gpu;if(!gpu)throw Error('WebGPU unavailable');
        const adapter=await gpu.requestAdapter();if(!current())return;
        if(!adapter)throw Error('No WebGPU adapter');
        const device=await adapter.requestDevice();
        if(!current()){device.destroy();return;}
        this.gpu=gpu;this.adapter=adapter;this.device=device;
        device.lost.then(info=>{if(epoch===this.epoch)this.fail('WebGPU device lost: '+info.message);});
        device.addEventListener('uncapturederror',e=>{if(epoch===this.epoch)this.fail(e.error);});
        this.pipelines.clear();this.shaders={};this.metadata=ownedSources ? ownedSources.metadata : JSON.parse(FS.readFile(root+'/../metadata.json',{encoding:'utf8'}));
        for(const variant of['primitive','world','cloud','point','skin','skin-rig','terrain-compose','terrain-reduce']){
          const stages={};for(const stage of['vert','frag']){
            stages[stage]=device.createShaderModule({code:ownedSources ? ownedSources.stages[variant][stage] : FS.readFile(root+'/'+variant+'.'+stage+'.wgsl',{encoding:'utf8'})});
            const info=await stages[stage].getCompilationInfo();if(!current())return;
            if(info.messages.some(m=>m.type==='error'))throw Error(variant+' '+stage+': '+info.messages.map(m=>m.message).join('\n'));
          }this.shaders[variant]=stages;
          const description=this.pipelineDescription(variant,0,0,false,0);
          const pipeline=await device.createRenderPipelineAsync(description);if(!current())return;
          this.pipelines.set(variant+':0:0:0:0',pipeline);
        }
        const present=this.pipelineDescription('primitive',3,0,false,0);present.fragment.targets[0].format=gpu.getPreferredCanvasFormat();
        const pipeline=await device.createRenderPipelineAsync(present);if(!current())return;
        this.presentPipeline=pipeline;this.state=1;
      }catch(error){if(epoch===this.epoch)this.fail(error);}
    },

    pipelineDescription(variant,blend,depth,write,cull) {
      const world=variant==='world'||variant==='cloud';
      const layout=this.metadata.vertexLayouts[variant];
      const state=blend===3?undefined:{color:{srcFactor:blend===1?'one':'src-alpha',dstFactor:blend===2?'one':'one-minus-src-alpha',operation:'add'},alpha:{srcFactor:'one',dstFactor:blend===2?'one':'one-minus-src-alpha',operation:'add'}};
      return{layout:'auto',vertex:{module:this.shaders[variant].vert,entryPoint:'main',buffers:[{arrayStride:layout.stride,attributes:layout.attributes.map(a=>({shaderLocation:a.location,offset:a.offset,format:'float32x'+a.components}))}]},fragment:{module:this.shaders[variant].frag,entryPoint:'main',targets:[{format:'rgba8unorm',blend:state}]},primitive:{topology:'triangle-list',frontFace:'ccw',cullMode:cull?'back':'none'},depthStencil:{format:'depth24plus',depthWriteEnabled:!!write,depthCompare:depth===1?'less':depth===2?'less-equal':'always'}};
    },
    pipeline(variant,blend,depth,write,cull){const key=[variant,blend,depth,+write,cull].join(':');if(!this.pipelines.has(key))this.pipelines.set(key,this.device.createRenderPipeline(this.pipelineDescription(variant,blend,depth,write,cull)));return this.pipelines.get(key);},
    totalBytes(){return this.bytes+this.maskBytes+this.scratchBytes+this.meshBytes+this.transientBytes+this.presentBytes+(this.screen?this.width*this.height*8:0)+(this.committed?this.width*this.height*4:0)+(this.white?4:0)+this.captures.reduce((n,v)=>n+v.pitch*v.height,0);},
    reserve(bytes){if(bytes<0||bytes>256*1024*1024)throw Error('WebGPU resource exceeds device budget');if(this.totalBytes()+bytes>256*1024*1024){const error=Error('WebGPU device memory budget admission deferred');error.deferred=true;throw error;}},
    retireBuffers(owned){owned.forEach(b=>{if(this.transients.delete(b)){this.transientBytes-=b.size;b.destroy();}});},
    finishPresentation(owned){
      const frame=this.activeFrame||{identity:this.issued,owned:[]};frame.owned.push(...owned);this.activeFrame=null;this.frames.push(frame);const epoch=this.allocationEpoch,deviceEpoch=this.epoch;
      this.device.queue.onSubmittedWorkDone().then(()=>{this.retireBuffers(frame.owned);if(epoch===this.allocationEpoch){this.completed=Math.max(this.completed,frame.identity);this.frames=this.frames.filter(v=>v!==frame);}}).catch(e=>{if(epoch===this.allocationEpoch && deviceEpoch===this.epoch)this.fail(e);});
    },
    image(width,height,pixels,hasDepth=false,mipCount=1){
      if(width<1||height<1||width>this.device.limits.maxTextureDimension2D||height>this.device.limits.maxTextureDimension2D)throw Error('WebGPU texture dimensions unsupported');
      if(mipCount<1||mipCount>1+Math.floor(Math.log2(Math.max(width,height))))throw Error("WebGPU mip chain dimensions");
      let colorBytes=0;for(let level=0;level<mipCount;level++)colorBytes+=Math.max(1,width>>level)*Math.max(1,height>>level)*4;
      this.reserve(colorBytes+(hasDepth?width*height*4:0));
      const texture=this.device.createTexture({size:[width,height],mipLevelCount:mipCount,format:'rgba8unorm',usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC|GPUTextureUsage.COPY_DST});
      const depth=hasDepth?this.device.createTexture({size:[width,height],format:'depth24plus',usage:GPUTextureUsage.RENDER_ATTACHMENT}):null;
      if(pixels)this.device.queue.writeTexture({texture},pixels,{bytesPerRow:width*4},[width,height]);
      return{texture,depth,width,height,mipCount,colorBytes,initialized:!!pixels,depthInitialized:false};
    },
    destroyImage(image){image?.texture.destroy();image?.depth?.destroy();},
    size(width,height){
      if(width===this.width&&height===this.height)return;
      this.reserve(width*height*12);const screen=this.image(width,height,null,true),committed=this.image(width,height,null);
      if(this.committed){const encoder=this.device.createCommandEncoder(),size=[Math.min(width,this.width),Math.min(height,this.height)];encoder.copyTextureToTexture({texture:this.committed.texture},{texture:screen.texture},size);encoder.copyTextureToTexture({texture:this.committed.texture},{texture:committed.texture},size);this.device.queue.submit([encoder.finish()]);screen.initialized=committed.initialized=true;}
      this.presentGroups=null;this.presentGeometrySize=null;this.destroyImage(this.screen);this.destroyImage(this.committed);this.width=width;this.height=height;this.screen=screen;this.committed=committed;
    },
    buffer(data,usage,persistent=false){const size=Math.max(4,data.byteLength);this.reserve(size);const b=this.device.createBuffer({size,usage:usage|GPUBufferUsage.COPY_DST});if(!persistent){this.transients.add(b);this.transientBytes+=size;}if(data.byteLength)this.device.queue.writeBuffer(b,0,data);return b;},
    textureView(texture){
      // Views are immutable and die with their texture; reset/device replacement
      // cannot reuse an old view for a new allocation.
      if(!this.views)this.views=new WeakMap();
      let view=this.views.get(texture);if(!view){view=texture.createView();this.views.set(texture,view);}return view;
    },
    submissionBindings(owned){
      // Reuse identical immutable uniforms and binding descriptors only within
      // this ordered chunk. Never rewrite a buffer referenced by queued work.
      const uniforms=new Map(),groups=new Map(),ids=new WeakMap();let nextId=0,bytes=0,uniformCount=0,groupCount=0;
      const id=value=>{if(!ids.has(value))ids.set(value,++nextId);return ids.get(value);};
      return{
        uniform:data=>{
          const value=new Uint8Array(data.buffer,data.byteOffset,data.byteLength);let hash=2166136261;
          for(const byte of value)hash=Math.imul(hash^byte,16777619);
          const key=value.length+':'+hash,entries=uniforms.get(key)||[];
          for(const entry of entries)if(entry.value.every((byte,i)=>byte===value[i]))return entry.buffer;
          const buffer=this.buffer(data,GPUBufferUsage.UNIFORM);owned.push(buffer);
          if(uniformCount<512&&bytes+value.length<=1024*1024){entries.push({value,buffer});uniforms.set(key,entries);uniformCount++;bytes+=value.length;}
          return buffer;
        },
        group:(pipeline,group,entries)=>{
          const key=group+':'+entries.map(e=>e.binding+'='+id(e.resource.buffer||e.resource)).join(',');
          let cache=groups.get(pipeline);if(!cache){cache=new Map();groups.set(pipeline,cache);}
          let binding=cache.get(key);if(!binding){binding=this.device.createBindGroup({layout:pipeline.getBindGroupLayout(group),entries});if(groupCount<4096){cache.set(key,binding);groupCount++;}}
          return binding;
        }
      };
    },
    clearRegion(pass,target,clip,color,owned){
      if(clip[2]<=0||clip[3]<=0)return;
      const pipeline=this.pipeline('primitive',3,0,false,0);pass.setPipeline(pipeline);pass.setScissorRect(...clip);pass.setViewport(0,0,target.width,target.height,0,1);
      const data=new Float32Array([[0,0],[target.width,0],[target.width,target.height],[0,0],[target.width,target.height],[0,target.height]].flatMap(([x,y])=>[x,y,...color,0,0]));
      const vertices=this.buffer(data,GPUBufferUsage.VERTEX);owned.push(vertices);pass.setVertexBuffer(0,vertices);
      for(let group=0;group<4;group++){
        const entries=[];
        if(group===1||group===3){const values=group===1?new Float32Array([target.width,target.height,0,0]):new Float32Array([0,1,0,0,0,0,1,1]);const b=this.buffer(values,GPUBufferUsage.UNIFORM);owned.push(b);entries.push({binding:0,resource:{buffer:b}});}
        if(group===2)entries.push({binding:0,resource:this.textureView(this.white.texture)},{binding:1,resource:this.samplers[0]});
        pass.setBindGroup(group,this.device.createBindGroup({layout:pipeline.getBindGroupLayout(group),entries}));
      }pass.draw(6);
    },
    maskUpload(id,width,height,weights,selectors){
      const bytes=width*height*9;if(bytes>32*1024*1024)throw Error('WebGPU coverage resource limit');
      if(this.maskBytes+bytes>32*1024*1024){const e=Error('WebGPU coverage admission deferred');e.deferred=true;throw e;}this.reserve(bytes);
      const make=(format,data,stride)=>{const texture=this.device.createTexture({size:[width,height],format,usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST});this.device.queue.writeTexture({texture},data,{bytesPerRow:width*stride},[width,height]);return texture;};
      const a=make('rgba16uint',weights,8);let b;try{b=make('r8uint',selectors,1);}catch(e){a.destroy();throw e;}
      this.masks.set(id,{weights:a,selectors:b,bytes});this.maskBytes+=bytes;
    },
    maskDelete(id){const m=this.masks.get(id);if(m){m.weights.destroy();m.selectors.destroy();this.maskBytes-=m.bytes;this.masks.delete(id);}},
    terrain(encoder,target,u,uniforms,owned){
      const size=u(148),divisor=u(152),pma=u(156),rect=[u(64),u(68),u(72),u(76)];
      let raw=target;
      if(divisor>1){raw=this.scratch.get(size);if(!raw){const bytes=size*size*8;if(this.scratchBytes+bytes>32*1024*1024){for(const[key,image]of this.scratch)if(image.lastUse<=this.completed){this.destroyImage(image);this.scratchBytes-=image.width*image.height*(image.depth?8:4);this.scratch.delete(key);}}if(this.scratchBytes+bytes>32*1024*1024){const e=Error('WebGPU terrain scratch admission deferred');e.deferred=true;throw e;}raw=this.image(size,size,null);this.scratch.set(size,raw);this.scratchBytes+=size*size*4;}}
      if(divisor>1)raw.lastUse=this.issued+1;
      const draw=(variant,destination,rectangle,data,images)=>{
        const pipeline=this.pipeline(variant,3,0,false,0),[x,y,w,h]=rectangle;
        const pass=encoder.beginRenderPass({colorAttachments:[{view:this.textureView(destination.texture),loadOp:destination.initialized?'load':'clear',clearValue:[0,0,0,0],storeOp:'store'}],depthStencilAttachment:{view:this.textureView(destination.depth),depthLoadOp:destination.depthInitialized?'load':'clear',depthClearValue:1,depthStoreOp:'store'}});
        destination.initialized=true;destination.depthInitialized=true;pass.setPipeline(pipeline);pass.setViewport(0,0,destination.width,destination.height,0,1);pass.setScissorRect(x,y,w,h);
        const vb=this.buffer(new Float32Array([[x,y,0,0],[x+w,y,1,0],[x+w,y+h,1,1],[x,y,0,0],[x+w,y+h,1,1],[x,y+h,0,1]].flatMap(([px,py,a,b])=>[px,py,1,1,1,1,a,b])),GPUBufferUsage.VERTEX);owned.push(vb);pass.setVertexBuffer(0,vb);
        for(let group=0;group<4;group++){const entries=[];if(group===1||group===3){const b=this.buffer(group===1?new Float32Array([destination.width,destination.height,0,0]):data,GPUBufferUsage.UNIFORM);owned.push(b);entries.push({binding:0,resource:{buffer:b}});}if(group===2)entries.push(...images);pass.setBindGroup(group,this.device.createBindGroup({layout:pipeline.getBindGroupLayout(group),entries}));}pass.draw(6);pass.end();
      };
      const mask=u(144)?this.masks.get(u(144)):null;if(u(144)&&!mask)throw Error('Stale WebGPU coverage');
      if(!this.emptyMask){this.reserve(9);this.emptyMask={weights:this.device.createTexture({size:[1,1],format:'rgba16uint',usage:GPUTextureUsage.TEXTURE_BINDING}),selectors:this.device.createTexture({size:[1,1],format:'r8uint',usage:GPUTextureUsage.TEXTURE_BINDING})};this.maskBytes+=9;}
      const coverage=mask||this.emptyMask,images=[];for(let slot=0;slot<4;slot++){const image=u(128+slot*4)?this.resources.get(u(128+slot*4)):this.white;if(!image||image===target)throw Error('Invalid WebGPU terrain source');images.push({binding:slot*2,resource:this.textureView(image.texture)});}
      images.push({binding:8,resource:this.textureView(coverage.weights)},{binding:10,resource:this.textureView(coverage.selectors)});
      const data=uniforms.slice(u(112),u(112)+144);
      if(!raw.depth){this.reserve(size*size*4);raw.depth=this.device.createTexture({size:[size,size],format:'depth24plus',usage:GPUTextureUsage.RENDER_ATTACHMENT});this.scratchBytes+=size*size*4;}
      draw('terrain-compose',raw,divisor>1?[0,0,size,size]:rect,data,images);
      if(divisor>1)draw('terrain-reduce',target,rect,new Uint32Array([divisor|(pma?65536:0),0,0,0]),[{binding:0,resource:this.textureView(raw.texture)}]);
    },
    submit(records,count,vertices,uniforms,width,height){
      this.deferred=false;if(this.lost)throw Error(this.error);if(!this.activeFrame&&this.frames.length>=8)return 0;
      this.size(width,height);const encoder=this.device.createCommandEncoder(),owned=[];
      const vertex=this.buffer(vertices,GPUBufferUsage.VERTEX);owned.push(vertex);const bindings=this.submissionBindings(owned);
      // Attachment loads/stores and immutable bindings may be reused only for
      // adjacent draws. Clears, explicit barriers and terrain passes end the run.
      let active=null;
      const endPass=()=>{if(active){active.pass.end();active=null;}};
      const sameRect=(a,b)=>a&&a.every((value,index)=>value===b[index]);
      const view=new DataView(records.buffer,records.byteOffset,records.byteLength);
      for(let i=0;i<count;i++){
        const base=i*256,u=o=>view.getUint32(base+o,true),n=o=>view.getInt32(base+o,true),f=o=>view.getFloat32(base+o,true);
        const kind=u(0);if(kind===3){endPass();continue;}
        const target=u(28)?this.resources.get(u(28)):this.screen;if(!target)throw Error('Stale WebGPU target');
        const texture=u(24)?this.resources.get(u(24)):this.white;if(!texture)throw Error('Stale WebGPU image');
        if(kind!==2&&texture===target)throw Error('WebGPU target cannot sample itself');
        if(!target.depth){this.reserve(target.width*target.height*4);target.depth=this.device.createTexture({size:[target.width,target.height],format:'depth24plus',usage:GPUTextureUsage.RENDER_ATTACHMENT});this.bytes+=target.width*target.height*4;}
        if(kind===4){endPass();this.terrain(encoder,target,u,uniforms,owned);continue;}
        const clearColor=kind===2&&u(244),clearDepth=kind===2&&u(248),clip=[n(80),n(84),n(88),n(92)];
        const fullColor=clearColor&&clip[0]===0&&clip[1]===0&&clip[2]===target.width&&clip[3]===target.height;
        if(kind===2||active?.target!==target)endPass();
        if(!active){
          const pass=encoder.beginRenderPass({colorAttachments:[{view:this.textureView(target.texture),loadOp:fullColor||!target.initialized?'clear':'load',clearValue:fullColor?[f(224),f(228),f(232),f(236)]:[0,0,0,0],storeOp:'store'}],depthStencilAttachment:{view:this.textureView(target.depth),depthLoadOp:clearDepth||!target.depthInitialized?'clear':'load',depthClearValue:clearDepth?f(240):1,depthStoreOp:'store'}});
          target.initialized=true;target.depthInitialized=true;active={pass,target,groups:[]};
        }
        const pass=active.pass;
        if(kind===2){if(clearColor&&!fullColor)this.clearRegion(pass,target,clip,[f(224),f(228),f(232),f(236)],owned);endPass();continue;}
        const variant=['primitive','world','cloud','point','skin','skin-rig'][u(4)],skin=u(4)>=4,world=kind===1&&!skin;
        const pipeline=this.pipeline(variant,u(8),u(12),u(16),u(20));
        if(active.pipeline!==pipeline){pass.setPipeline(pipeline);active.pipeline=pipeline;active.groups=[];}
        const viewport=[n(64),n(68),n(72),n(76)];
        if(viewport[2]<=0||viewport[3]<=0||clip[2]<=0||clip[3]<=0){continue;}
        if(!sameRect(active.viewport,viewport)){pass.setViewport(...viewport,0,1);active.viewport=viewport;}
        if(!sameRect(active.clip,clip)){pass.setScissorRect(...clip);active.clip=clip;}
        const mesh=u(252)?this.meshes.get(u(252)):null;if(u(252)&&!mesh)throw Error('Stale WebGPU geometry');
        const vertexBuffer=mesh?mesh.vertices:vertex,vertexOffset=u(32),vertexSize=u(36);
        if(active.vertex!==vertexBuffer||active.vertexOffset!==vertexOffset||active.vertexSize!==vertexSize){
          pass.setVertexBuffer(0,vertexBuffer,vertexOffset,vertexSize);active.vertex=vertexBuffer;active.vertexOffset=vertexOffset;active.vertexSize=vertexSize;
        }
        const vertexData=skin?uniforms.slice(u(112),u(112)+u(116)):world?records.slice(base+128,base+208):new Float32Array([f(96),f(100),0,0]);
        const fragmentData=skin?uniforms.slice(u(120),u(120)+u(124)):world?records.slice(base+208,base+224):records.slice(base+104,base+128);
        // Primitive block: textured/alpha, std140 padding, UV bounds.
        const primitiveData=new Float32Array([f(108),f(104),0,0,f(112),f(116),f(120),f(124)]);
        for(let group=0;group<=(variant==='cloud'?2:3);group++){
          const entries=[];
          if(group===1){const b=bindings.uniform(vertexData);entries.push({binding:0,resource:{buffer:b}});}
          if(group===2&&variant!=='point'){
            const sampler=this.samplers[(!skin&&u(56)?1:0)+(texture.repeatU?2:0)+(texture.repeatV?4:0)];
            entries.push({binding:0,resource:this.textureView(texture.texture)},{binding:1,resource:sampler});
            if(skin){const second=this.resources.get(u(60));if(!second)throw Error('Stale WebGPU material texture');entries.push({binding:2,resource:this.textureView(second.texture)},{binding:3,resource:this.samplers[0]});}
          }
          if(group===3){const b=bindings.uniform(skin||world?fragmentData:primitiveData);entries.push({binding:0,resource:{buffer:b}});}
          const binding=bindings.group(pipeline,group,entries);
          if(active.groups[group]!==binding){pass.setBindGroup(group,binding);active.groups[group]=binding;}
        }
        if(u(44)){
          const offset=u(40),size=u(44)*4;
          if(active.index!==mesh.indices||active.indexOffset!==offset||active.indexSize!==size){pass.setIndexBuffer(mesh.indices,'uint32',offset,size);active.index=mesh.indices;active.indexOffset=offset;active.indexSize=size;}
          pass.drawIndexed(u(52));
        }else pass.draw(u(52),1,u(48));
      }
      endPass();
      const identity=++this.issued;this.device.queue.submit([encoder.finish()]);
      if(!this.activeFrame)this.activeFrame={identity,owned:[]};this.activeFrame.identity=identity;this.activeFrame.owned.push(...owned);return identity;
    },
    present(){
      if(this.lost)throw Error(this.error);if(!this.screen||(!this.activeFrame&&this.frames.length>=8))return;
      const pipeline=this.presentPipeline,encoder=this.device.createCommandEncoder(),owned=[];
      const pass=encoder.beginRenderPass({colorAttachments:[{view:this.context.getCurrentTexture().createView(),loadOp:'clear',clearValue:[0,0,0,1],storeOp:'store'}],depthStencilAttachment:{view:this.textureView(this.screen.depth),depthLoadOp:'load',depthStoreOp:'store'}});
      pass.setPipeline(pipeline);
      if(!this.presentGeometrySize){
        const vertices=new Float32Array([[0,0,0,0],[this.width,0,1,0],[this.width,this.height,1,1],[0,0,0,0],[this.width,this.height,1,1],[0,this.height,0,1]].flatMap(([x,y,u,v])=>[x,y,1,1,1,1,u,v]));
        this.device.queue.writeBuffer(this.presentOwned[0],0,vertices);this.device.queue.writeBuffer(this.presentOwned[1],0,new Float32Array([this.width,this.height,0,0]));this.presentGeometrySize=[this.width,this.height];
      }pass.setVertexBuffer(0,this.presentOwned[0]);
      if(!this.presentGroups)this.presentGroups=[];
      for(let group=0;group<4;group++){
        const entries=[];
        if(group===1||group===3)entries.push({binding:0,resource:{buffer:this.presentOwned[group===1?1:2]}});
        if(group===2)entries.push({binding:0,resource:this.textureView(this.screen.texture)},{binding:1,resource:this.samplers[0]});
        if(!this.presentGroups[group])this.presentGroups[group]=this.device.createBindGroup({layout:pipeline.getBindGroupLayout(group),entries});
        pass.setBindGroup(group,this.presentGroups[group]);
      }pass.draw(6);pass.end();encoder.copyTextureToTexture({texture:this.screen.texture},{texture:this.committed.texture},[this.width,this.height]);this.device.queue.submit([encoder.finish()]);this.committed.initialized=true;this.finishPresentation(owned);
    },
    discard(){
      // Even preparation can enqueue uploads before packet submission fails.
      // Give all those writes a real completion identity, including a failure
      // before the first chunk. Existing submitted chunks precede this fence.
      const protectedBuffers=new Set([...this.frames.flatMap(f=>f.owned),...(this.activeFrame?.owned||[])]);
      const unsubmitted=Array.from(this.transients).filter(b=>!protectedBuffers.has(b));
      if(!this.activeFrame)this.activeFrame={identity:0,owned:[]};
      this.activeFrame.identity=++this.issued;
      this.activeFrame.owned.push(...unsubmitted);
      if(this.screen&&this.committed){const encoder=this.device.createCommandEncoder();encoder.copyTextureToTexture({texture:this.committed.texture},{texture:this.screen.texture},[this.width,this.height]);this.device.queue.submit([encoder.finish()]);}
      if(this.screen)this.present();else if(this.activeFrame)this.finishPresentation([]);this.deferred=false;
    },
    capture(identity,width,height,targetId=0,x=0,y=0){
      const source=targetId?this.resources.get(targetId):this.screen;
      if(this.lost||!source||this.captures.length>=3||width<1||height<1||x<0||y<0||x+width>source.width||y+height>source.height)return 0;
      const pitch=Math.ceil(width*4/256)*256;if(this.totalBytes()+pitch*height>256*1024*1024)return 0;const buffer=this.device.createBuffer({size:pitch*height,usage:GPUBufferUsage.MAP_READ|GPUBufferUsage.COPY_DST});
      const item={identity,width,height,pitch,buffer,ready:false,cancelled:false};this.captures.push(item);
      const encoder=this.device.createCommandEncoder();encoder.copyTextureToBuffer({texture:source.texture,origin:[x,y]},{buffer,bytesPerRow:pitch},[width,height]);this.device.queue.submit([encoder.finish()]);
      buffer.mapAsync(GPUMapMode.READ).then(()=>{if(!item.cancelled)item.ready=true;else buffer.destroy();}).catch(()=>{item.cancelled=true;item.ready=true;});return 1;
    },
    reset(){
      this.presentGroups=null;this.presentGeometrySize=null;this.views=new WeakMap();
      this.presentOwned.forEach(b=>b.destroy());this.presentOwned=[];this.presentBytes=0;
      ++this.allocationEpoch;this.retireBuffers(Array.from(this.transients));this.frames=[];this.activeFrame=null;
      this.meshes.forEach(v=>{v.vertices.destroy();v.indices.destroy();});this.meshes.clear();this.meshBytes=0;
      this.masks.forEach(m=>{m.weights.destroy();m.selectors.destroy();});this.masks.clear();this.maskBytes=0;this.emptyMask?.weights.destroy();this.emptyMask?.selectors.destroy();this.emptyMask=null;this.scratch.forEach(v=>this.destroyImage(v));this.scratch.clear();this.scratchBytes=0;
      this.resources.forEach(v=>this.destroyImage(v));this.resources.clear();this.captures.forEach(v=>{v.cancelled=true;v.buffer.destroy();});this.captures=[];
      this.destroyImage(this.screen);this.destroyImage(this.committed);this.screen=this.committed=null;this.width=this.height=0;this.completed=this.issued;this.bytes=0;
    }
  },
  // SDK preload files live on main. Pthread-local JS FS is a separate tree;
  // transfer owned source text, while GPU objects stay in the presentation owner.
  glob2_webgpu_shader_payload__proxy:'sync',
  glob2_webgpu_shader_payload__deps:['$FS','$UTF8ToString','$lengthBytesUTF8','$stringToUTF8','malloc'],
  glob2_webgpu_shader_payload(root){
    let sources;
    try{
      const directory=UTF8ToString(root),stages={};
      const metadata=JSON.parse(FS.readFile(directory+'/../metadata.json',{encoding:'utf8'}));
      for(const variant of['primitive','world','cloud','point','skin','skin-rig','terrain-compose','terrain-reduce']){
        stages[variant]={};for(const stage of['vert','frag'])stages[variant][stage]=FS.readFile(directory+'/'+variant+'.'+stage+'.wgsl',{encoding:'utf8'});
      }
      sources={metadata,stages};
    }catch(error){sources={error:String(error?.message||error)};}
    let text=JSON.stringify(sources),bytes=lengthBytesUTF8(text)+1;
    if(bytes>2*1024*1024){text=JSON.stringify({error:'Renderer shader payload exceeds preparation budget'});bytes=lengthBytesUTF8(text)+1;}
    const pointer=_malloc(bytes);if(pointer)stringToUTF8(text,pointer,bytes);return pointer;
  },
  glob2_webgpu_prepare__deps:['$Glob2WebGPU','$UTF8ToString','glob2_webgpu_shader_payload','free'],
  glob2_webgpu_prepare(root){
    if(Glob2WebGPU.state===0.5)return 0;
    if(Glob2WebGPU.device&&!Glob2WebGPU.lost){Glob2WebGPU.state=1;return 1;}
    {
      const directory=UTF8ToString(root),pointer=_glob2_webgpu_shader_payload(root);
      if(!pointer){Glob2WebGPU.fail('Renderer shader preparation allocation failed');return -1;}
      let sources;try{sources=JSON.parse(UTF8ToString(pointer));}finally{_free(pointer);}
      Glob2WebGPU.prepare(directory,sources);
    }
    return Glob2WebGPU.state===1?1:Glob2WebGPU.state<0?-1:0;
  },
  glob2_webgpu_issued__deps:['$Glob2WebGPU'],
  glob2_webgpu_issued(){return Glob2WebGPU.issued;},
  glob2_webgpu_poll__deps:['$Glob2WebGPU'],
  glob2_webgpu_poll(){return Glob2WebGPU.state===1&&!Glob2WebGPU.lost?1:Glob2WebGPU.state<0?-1:0;},
  glob2_webgpu_error__deps:['$Glob2WebGPU','$stringToUTF8'],
  glob2_webgpu_error(destination,capacity){stringToUTF8(Glob2WebGPU.error,destination,capacity);},
  glob2_webgpu_attach__deps:['$Glob2WebGPU'],
  glob2_webgpu_attach(){try{const g=Glob2WebGPU;if(g.state!==1||g.lost)throw Error(g.error||'WebGPU not prepared');g.reset();g.destroyImage(g.white);g.white=null;g.context=Module.canvas.getContext('webgpu');if(!g.context)throw Error('WebGPU presentation context unavailable');g.context.configure({device:g.device,format:g.gpu.getPreferredCanvasFormat(),usage:GPUTextureUsage.RENDER_ATTACHMENT,alphaMode:'opaque'});g.white=g.image(1,1,new Uint8Array([255,255,255,255]));g.samplers=Array.from({length:8},(_,v)=>g.device.createSampler({magFilter:v&1?'linear':'nearest',minFilter:v&1?'linear':'nearest',mipmapFilter:v&1?'linear':'nearest',lodMaxClamp:v&1?32:0,addressModeU:v&2?'repeat':'clamp-to-edge',addressModeV:v&4?'repeat':'clamp-to-edge'}));g.reserve(240);g.presentOwned=[g.buffer(new Float32Array(48),GPUBufferUsage.VERTEX,true),g.buffer(new Float32Array(4),GPUBufferUsage.UNIFORM,true),g.buffer(new Float32Array([1,1,0,0,0,0,1,1]),GPUBufferUsage.UNIFORM,true)];g.presentBytes=240;return g.device.limits.maxTextureDimension2D;}catch(e){Glob2WebGPU.handle(e);return 0;}},
  glob2_webgpu_upload__deps:['$Glob2WebGPU'],
  glob2_webgpu_upload(id,width,height,pixels,repeatU,repeatV,target,mipCount=1){try{const g=Glob2WebGPU;const image=g.image(width,height,HEAPU8.slice(pixels,pixels+width*height*4),!!target,mipCount);image.repeatU=!!repeatU;image.repeatV=!!repeatV;g.resources.set(id,image);g.bytes+=image.colorBytes+(target?width*height*4:0);return 1;}catch(e){Glob2WebGPU.handle(e);return 0;}},
  glob2_webgpu_mip_upload__deps:['$Glob2WebGPU'],
  glob2_webgpu_mip_upload(id,level,width,height,pixels){try{const g=Glob2WebGPU,image=g.resources.get(id);if(!image||level<1||level>=image.mipCount||width!==Math.max(1,image.width>>level)||height!==Math.max(1,image.height>>level))throw Error('WebGPU mip dimensions');g.device.queue.writeTexture({texture:image.texture,mipLevel:level},HEAPU8.slice(pixels,pixels+width*height*4),{bytesPerRow:width*4},[width,height]);return 1;}catch(e){Glob2WebGPU.handle(e);return 0;}},
  glob2_webgpu_trim__deps:['$Glob2WebGPU'],
  glob2_webgpu_trim(){const g=Glob2WebGPU;for(const[key,image]of g.scratch){if(image.lastUse<=g.completed){g.destroyImage(image);g.scratchBytes-=image.width*image.height*(image.depth?8:4);g.scratch.delete(key);}}},
  glob2_webgpu_delete__deps:['$Glob2WebGPU'],
  glob2_webgpu_delete(id){const g=Glob2WebGPU,v=g.resources.get(id);if(v){g.destroyImage(v);g.bytes-=v.colorBytes+(v.depth?v.width*v.height*4:0);g.resources.delete(id);}},
  glob2_webgpu_mask_upload__deps:['$Glob2WebGPU'],
  glob2_webgpu_mask_upload(id,width,height,weights,selectors){try{Glob2WebGPU.maskUpload(id,width,height,HEAPU8.slice(weights,weights+width*height*8),HEAPU8.slice(selectors,selectors+width*height));return 1;}catch(e){Glob2WebGPU.handle(e);return 0;}},
  glob2_webgpu_mask_delete__deps:['$Glob2WebGPU'],
  glob2_webgpu_mask_delete(id){Glob2WebGPU.maskDelete(id);},
  glob2_webgpu_geometry_upload__deps:['$Glob2WebGPU'],
  glob2_webgpu_geometry_upload(id,vertices,vertexBytes,indices,indexBytes){try{const g=Glob2WebGPU;g.reserve(Math.max(4,vertexBytes)+Math.max(4,indexBytes));const v=g.buffer(HEAPU8.slice(vertices,vertices+vertexBytes),GPUBufferUsage.VERTEX,true),i=g.buffer(HEAPU8.slice(indices,indices+indexBytes),GPUBufferUsage.INDEX,true);g.meshes.set(id,{vertices:v,indices:i,bytes:v.size+i.size});g.meshBytes+=v.size+i.size;return 1;}catch(e){Glob2WebGPU.handle(e);return 0;}},
  glob2_webgpu_geometry_delete__deps:['$Glob2WebGPU'],
  glob2_webgpu_geometry_delete(id){const g=Glob2WebGPU,m=g.meshes.get(id);if(m){m.vertices.destroy();m.indices.destroy();g.meshBytes-=m.bytes;g.meshes.delete(id);}},
  glob2_webgpu_submit__deps:['$Glob2WebGPU'],
  glob2_webgpu_submit(records,count,vertices,vertexBytes,indices,indexBytes,width,height){try{return Glob2WebGPU.submit(HEAPU8.slice(records,records+count*256),count,HEAPU8.slice(vertices,vertices+vertexBytes),HEAPU8.slice(indices,indices+indexBytes),width,height);}catch(e){Glob2WebGPU.handle(e);return 0;}},
  glob2_webgpu_deferred__deps:['$Glob2WebGPU'],
  glob2_webgpu_deferred(){return +Glob2WebGPU.deferred;},
  glob2_webgpu_discard__deps:['$Glob2WebGPU'],
  glob2_webgpu_discard(){try{Glob2WebGPU.discard();return Glob2WebGPU.issued;}catch(e){Glob2WebGPU.fail(e);return 0;}},
  glob2_webgpu_memory__deps:['$Glob2WebGPU'],
  glob2_webgpu_memory(){return Glob2WebGPU.totalBytes();},
  glob2_webgpu_completed__deps:['$Glob2WebGPU'],
  glob2_webgpu_completed(){return Glob2WebGPU.completed;},
  glob2_webgpu_can_begin__deps:['$Glob2WebGPU'],
  glob2_webgpu_can_begin(){return Glob2WebGPU.lost?-1:(Glob2WebGPU.activeFrame||Glob2WebGPU.frames.length<8)?1:0;},
  glob2_webgpu_present__deps:['$Glob2WebGPU'],
  glob2_webgpu_present(){try{Glob2WebGPU.present();return 1;}catch(e){Glob2WebGPU.handle(e);return 0;}},
  glob2_webgpu_reset__deps:['$Glob2WebGPU'],
  glob2_webgpu_reset(){Glob2WebGPU.reset();Glob2WebGPU.destroyImage(Glob2WebGPU.white);Glob2WebGPU.white=null;Glob2WebGPU.context?.unconfigure();Glob2WebGPU.context=null;},
  glob2_webgpu_capture__deps:['$Glob2WebGPU'],
  glob2_webgpu_capture(identity,width,height,targetId=0,x=0,y=0){return Glob2WebGPU.capture(identity,width,height,targetId,x,y);},
  glob2_webgpu_capture_ready__deps:['$Glob2WebGPU'],
  glob2_webgpu_capture_ready(){const c=Glob2WebGPU.captures[0];return c?.ready?1:0;},
  glob2_webgpu_capture_poll__deps:['$Glob2WebGPU'],
  glob2_webgpu_capture_poll(pixels,pitch){const g=Glob2WebGPU,item=g.captures[0];if(!item||!item.ready)return 0;g.captures.shift();if(item.cancelled){item.buffer.destroy();return -1;}const source=new Uint8Array(item.buffer.getMappedRange());for(let y=0;y<item.height;y++)HEAPU8.set(source.subarray(y*item.pitch,y*item.pitch+item.width*4),pixels+y*pitch);item.buffer.unmap();item.buffer.destroy();return 1;},
  glob2_webgpu_capture_cancel__deps:['$Glob2WebGPU'],
  glob2_webgpu_capture_cancel(){Glob2WebGPU.captures.forEach(v=>{v.cancelled=true;v.buffer.destroy();});Glob2WebGPU.captures=[];}
});
