// SPDX-License-Identifier: GPL-3.0-or-later
// WebCodecs adapter; software encoding stays in the shared WASM pipeline.
function codecConfiguration(width,height,fps) {
  const blocks=Math.ceil(width/16)*Math.ceil(height/16);
  const levels=[[31,3600,108000],[40,8192,245760],[42,8704,522240],[51,36864,983040],[52,36864,2073600],[60,139264,4177920]];
  const level=levels.find(([,frames,rate])=>blocks<=frames && blocks*fps<=rate)?.[0] || 62;
  return {codec:'avc1.4200'+level.toString(16).padStart(2,'0'),width,height,framerate:fps,
    bitrate:Math.max(1000000,Math.round(width*height*fps*0.12)),latencyMode:'realtime',
    hardwareAcceleration:'prefer-hardware',avc:{format:'annexb'}};
}
const recordingVideo = {
  supported:new Map(),encoders:new Map(),next:1,fps:30,
  async probe(width,height) {
    width+=width%2; height+=height%2; const key=width+'x'+height;
    if (this.supported.has(key)) return;
    let supported=false;
    try {
      if (typeof VideoEncoder==='function') {
        const configuration=codecConfiguration(width,height,this.fps);
        if ((await VideoEncoder.isConfigSupported(configuration)).supported) {
          // Exercise actual initialization before the MP4 stream is committed.
          let error=null,output=false;
          const codec=new VideoEncoder({output:()=>{output=true;},error:e=>{error=e;}});
          try {
            codec.configure(configuration);
            const frame=new VideoFrame(new Uint8Array(width*height*4),{format:'RGBA',codedWidth:width,codedHeight:height,timestamp:0});
            try { codec.encode(frame,{keyFrame:true}); } finally { frame.close(); }
            let timer;
            try { await Promise.race([codec.flush(),new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('WebCodecs initialization timed out')),5000);})]); supported=output&&!error; }
            finally { clearTimeout(timer); }
          } finally { if (codec.state!=='closed') codec.close(); }
        }
      }
    } catch (_) {}
    this.supported.set(key,supported);
  },
  open(width,height,fps) {
    if (!this.supported.get(width+'x'+height)) return -1;
    const id=this.next++,value={id,width,height,fps,pending:0,frames:0,flushing:false,finished:false,error:null};
    try {
      value.codec=new VideoEncoder({
        output:chunk=>{
          const bytes=new Uint8Array(chunk.byteLength); chunk.copyTo(bytes);
          const pointer=runtime._malloc(bytes.length);
          try { runtime.HEAPU8.set(bytes,pointer); runtime._glob2_web_packet(id,pointer,bytes.length,chunk.timestamp,chunk.duration || 1000000/fps,chunk.type==='key'); }
          finally { runtime._free(pointer); value.pending--; }
        }, error:error=>{ value.error=error; prepareFallback(); }
      });
      value.codec.configure(codecConfiguration(width,height,fps)); this.encoders.set(id,value); return id;
    } catch (_) { value.codec?.close(); return -1; }
  },
  ready(id) { const v=this.encoders.get(id); return v.error || (!v.flushing && v.pending<3) ? 1 : 0; },
  submit(id,rgba,width,height,pts) {
    const v=this.encoders.get(id); if (v.error || v.flushing) return -1;
    let bytes=rgba;
    if (width!==v.width || height!==v.height) {
      bytes=new Uint8Array(v.width*v.height*4);
      for (let i=3;i<bytes.length;i+=4) bytes[i]=255;
      for (let y=0;y<height;y++) bytes.set(rgba.subarray(y*width*4,(y+1)*width*4),y*v.width*4);
    }
    let frame;
    try {
      frame=new VideoFrame(bytes,{format:'RGBA',codedWidth:v.width,codedHeight:v.height,timestamp:Math.round(pts),duration:Math.round(1000000/v.fps),
        colorSpace:{primaries:'bt709',transfer:'iec61966-2-1',matrix:'rgb',fullRange:true}});
      v.codec.encode(frame,{keyFrame:v.frames++%(v.fps*2)===0}); v.pending++; return 0;
    } catch (e) { v.error=e; prepareFallback(); return 0; }
    finally { frame?.close(); }
  },
  finish(id) {
    const v=this.encoders.get(id); if (v.error) return -1; if (v.finished) return 1;
    if (!v.flushing) { v.flushing=true; v.codec.flush().then(()=>{v.finished=true;},e=>{v.error=e;}); }
    return 0;
  },
  close(id) { const v=this.encoders.get(id); if (v) { try { v.codec.close(); } catch (_) {} this.encoders.delete(id); } }
};

