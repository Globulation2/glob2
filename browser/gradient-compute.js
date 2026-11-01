// SPDX-License-Identifier: GPL-3.0-or-later
// Compute owns its device independently of presentation. All asynchronous work
// copies its inputs before submission and publishes its completion signal last.
addToLibrary({
  $Glob2GradientGPU: {
    device: null, pipelines: null, lost: null, adapterInfo: null,
    async prepare(frozen, jacobi) {
      if (this.device) return;
      if (!navigator.gpu) throw Error('WebGPU unavailable');
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) throw Error('No WebGPU compute adapter');
      const device = await adapter.requestDevice();
      const loss = device.lost.then(info => { throw Error('WebGPU compute device lost: ' + info.message); });
      loss.catch(() => {});
      // Initialization publishes one complete device/pipeline generation.
      // A failed compilation or device loss owns cleanup before signaling C++.
      try {
        const pipelines = [];
        for (const code of [frozen, jacobi]) {
          const module = device.createShaderModule({code});
          const info = await Promise.race([module.getCompilationInfo(), loss]);
          if (info.messages.some(m => m.type === 'error')) throw Error(info.messages.map(m => m.message).join('\n'));
          pipelines.push(await Promise.race([device.createComputePipelineAsync({layout: 'auto', compute: {module, entryPoint: 'main'}}), loss]));
        }
        this.adapterInfo = adapter.info; this.device = device; this.pipelines = pipelines; this.lost = loss;
      } catch (error) {
        device.destroy();
        throw error;
      }
    },
    finish(signal, result) {
      Atomics.store(HEAP32, signal >> 2, result); Atomics.notify(HEAP32, signal >> 2);
    },
    async execute(signal, seeds, costs, output, width, height, cap, frozen) {
      const device = this.device, cells = width * height, pitch = Math.ceil(width / 16), rows = Math.ceil(height / 16), tiles = pitch * rows;
      if (!device || cells * 8 > device.limits.maxStorageBufferBindingSize) throw Error('WebGPU gradient buffer limit');
      const x = frozen ? pitch : Math.ceil(cells / 128), y = frozen ? rows : 1;
      if (x > device.limits.maxComputeWorkgroupsPerDimension || y > device.limits.maxComputeWorkgroupsPerDimension) throw Error('WebGPU gradient dispatch limit');
      const buffers = [], make = (size, usage) => { const b = device.createBuffer({size, usage}); buffers.push(b); return b; };
      device.pushErrorScope('validation'); device.pushErrorScope('out-of-memory');
      try {
        const storage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC;
        const a = make(cells * 4, storage), b = make(cells * 4, storage), c = make(cells * 8, storage);
        const changed = make(4, storage), params = make(16, storage), active = make(tiles * 4, storage), next = make(tiles * 4, storage);
        const read = make(Math.max(4, cells * 4), GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST);
        // Copies do not retain views of shared Wasm memory across await points.
        device.queue.writeBuffer(a, 0, new Uint32Array(HEAPU32.subarray(seeds >> 2, (seeds >> 2) + cells)));
        device.queue.writeBuffer(c, 0, new Uint32Array(HEAPU32.subarray(costs >> 2, (costs >> 2) + cells * 2)));
        device.queue.writeBuffer(params, 0, new Uint32Array([width, height, cap, pitch]));
        device.queue.writeBuffer(active, 0, new Uint32Array(tiles).fill(1));
        const pipeline = this.pipelines[frozen ? 0 : 1];
        const groups = [[a,b,c,changed,params,active,next], [b,a,c,changed,params,next,active]].map(list => device.createBindGroup({layout: pipeline.getBindGroupLayout(0), entries: list.slice(0, frozen ? 7 : 5).map((buffer,binding) => ({binding,resource:{buffer}}))}));
        let current = 0;
        for (let round = 0; round <= 65535; ++round) {
          if (Atomics.load(HEAP32, (signal >> 2) + 1)) return false;
          const encoder = device.createCommandEncoder(); encoder.clearBuffer(changed); if (frozen) encoder.clearBuffer(current ? active : next);
          const pass = encoder.beginComputePass(); pass.setPipeline(pipeline); pass.setBindGroup(0, groups[current]); pass.dispatchWorkgroups(x,y,1); pass.end();
          encoder.copyBufferToBuffer(changed, 0, read, 0, 4); device.queue.submit([encoder.finish()]);
          await Promise.race([read.mapAsync(GPUMapMode.READ,0,4), this.lost]);
          const modified = new Uint32Array(read.getMappedRange(0,4))[0]; read.unmap(); Atomics.store(HEAP32, (signal >> 2) + 2, 1);
          current = 1 - current;
          HEAP32[(signal >> 2) + 3]++;
          if (!modified) {
            const final = device.createCommandEncoder(); final.copyBufferToBuffer(current ? b : a,0,read,0,cells*4); device.queue.submit([final.finish()]);
            await Promise.race([read.mapAsync(GPUMapMode.READ,0,cells*4), this.lost]);
            HEAPU32.set(new Uint32Array(read.getMappedRange(0,cells*4)), output >> 2); read.unmap();
            return true;
          }
        }
        throw Error('WebGPU gradient failed to converge');
      } finally {
        // Retire accounting only after queued work completes or device loss proves retirement.
        await Promise.race([device.queue.onSubmittedWorkDone(), this.lost]).catch(() => {});
        // Validation errors must resolve before reporting a successful result.
        try {
          const oom = await device.popErrorScope(), invalid = await device.popErrorScope();
          if (oom || invalid) throw Error((oom || invalid).message);
        } finally {
          // Error-scope rejection must not strand successfully created buffers.
          buffers.forEach(b => b.destroy());
        }
      }
    }
  },
  glob2_gradient_prepare__deps: ['$Glob2GradientGPU', '$stringToUTF8'],
  glob2_gradient_prepare__proxy: 'async',
  glob2_gradient_prepare(signal, frozen, jacobi, metadata) {
    const f = UTF8ToString(frozen), j = UTF8ToString(jacobi);
    Glob2GradientGPU.prepare(f,j).then(() => {const info = Glob2GradientGPU.adapterInfo;stringToUTF8(info ? [info.vendor,info.architecture,info.device,info.description].filter(Boolean).join(" / ") : "WebGPU compute adapter",metadata,512);Glob2GradientGPU.finish(signal,1);}, error => { console.error(error); Glob2GradientGPU.finish(signal,-1); });
  },
  glob2_gradient_execute__deps: ['$Glob2GradientGPU'],
  glob2_gradient_execute__proxy: 'async',
  glob2_gradient_execute(signal,seeds,costs,output,width,height,cap,frozen) {
    Glob2GradientGPU.execute(signal,seeds,costs,output,width,height,cap,frozen).then(ok => Glob2GradientGPU.finish(signal,ok ? 1 : -2), error => { console.error(error); Glob2GradientGPU.finish(signal,-1); });
  },
  glob2_gradient_shutdown__deps: ['$Glob2GradientGPU'],
  glob2_gradient_shutdown__proxy: 'async',
  glob2_gradient_shutdown() {
    if (Glob2GradientGPU.device) Glob2GradientGPU.device.destroy();
    Glob2GradientGPU.device = null; Glob2GradientGPU.pipelines = null; Glob2GradientGPU.lost = null; Glob2GradientGPU.adapterInfo = null;
  }
});
