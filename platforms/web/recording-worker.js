// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding and synchronous OPFS operations belong exclusively to this worker.
importScripts('recording-runtime.js','recording-storage.js','recording-video.js');
let runtime, base, state, clockOffset = 0, stopped = false, software = false;
let chain = Promise.resolve(), frameCredits = 0, processing = false, preparation = null;
let terminal = null, closing = null;
function prepareFallback() {
  preparation = recordingStorage.prepare(part((state?.segment || 1)+1));
  preparation.catch(error=>invoke('glob2_record_fail',['string'],[String(error.message || error)])).finally(()=>{preparation=null;});
}
function invoke(name,types,values) { return runtime.ccall(name,null,types,values); }
function bytes(buffer,action) {
  const view=new Uint8Array(buffer),pointer=runtime._malloc(view.byteLength);
  try { runtime.HEAPU8.set(view,pointer); return action(pointer); } finally { runtime._free(pointer); }
}
function part(number) { return base.replace(/\.mp4$/,'.part'+String(number).padStart(4,'0')+'.mp4'); }
async function message(m) {
  if (preparation) await preparation;
  if (m.type==='recover') {
    await recordingStorage.initialize(); await recordingStorage.prepareRecovery(m.path);
    runtime=await createRecordingRuntime();
    invoke('glob2_record_recover',['string'],[m.path]); await finishStorage(); return;
  }
  if (m.type==='start') {
    clockOffset=m.wallTime ? m.time+(performance.timeOrigin-m.wallTime)*1000 : m.time-performance.now()*1000;
    base=m.path.split(/[\\/]/).pop(); stopped=false;
    await recordingStorage.initialize(); await recordingStorage.prepare(base,true);
    recordingVideo.fps=m.fps; software=!!m.software;
    runtime=await createRecordingRuntime({printErr:value=>postMessage({type:'diagnostic',value})});
    invoke('glob2_record_start',['string','number','number','number','number'],[base,m.fps,m.crf,m.software,m.chapters || 10000]);
    // No busy wait: WebCodecs callbacks and OPFS preparation retain event-loop turns.
    tick(); return;
  }
  if (!runtime) return;
  if (m.type==='frame') {
    if (!software) await recordingVideo.probe(m.width,m.height);
    if (state?.width && (m.width!==state.sourceWidth || m.height!==state.sourceHeight)) await recordingStorage.prepare(part(state.segment+1));
    bytes(m.bytes,p=>invoke('glob2_record_frame',['number','number','number','number','string'],[p,m.width,m.height,m.time,JSON.stringify(m.context)]));
    ++frameCredits;
    runtime._glob2_record_step(m.time);
    acknowledgeFrames();
  } else if (m.type==='audio') {
    bytes(m.bytes,p=>runtime._glob2_record_audio(p,m.bytes.byteLength/2,m.time));
    postMessage({type:'ack',kind:'audio',samples:m.bytes.byteLength/2});
  } else if (m.type==='event') invoke('glob2_record_event',['number','string','string'],[m.time,m.kind,m.value]);
  else if (m.type==='stop') { runtime._glob2_record_stop(m.time); stopped=true; }
  else if (m.type==='fail') invoke('glob2_record_fail',['string'],[m.error]);
  else if (m.type==='drops') runtime._glob2_record_drops(m.frames,m.audio);
}
// The C++ reporter uses postMessage directly; retain its state locally as well.
const send=postMessage.bind(self);
self.postMessage=(m,...rest)=>{
  if (m.type==='status') { state=m.status; if (state.state>=4) { terminal=m; return; } }
  return send(m,...rest);
};
async function finishStorage() {
  if (!closing) {
    for (const id of recordingVideo.encoders.keys()) recordingVideo.close(id);
    closing=recordingStorage.closeAll();
  }
  await closing;
  if (terminal) { send(terminal); terminal=null; }
}
function acknowledgeFrames() {
  const consumed=frameCredits-runtime._glob2_record_queued();
  frameCredits-=consumed;
  for (let n=0;n<consumed;++n) postMessage({type:'ack',kind:'frame'});
}
async function tick() {
  if (!runtime) return;
  if (processing || preparation) { setTimeout(tick,2); return; }
  const done=runtime._glob2_record_step(performance.now()*1000+clockOffset);
  acknowledgeFrames();
  if (done || state?.state===5) { await finishStorage(); return; }
  setTimeout(tick,2);
}
self.onmessage=event=>{
  chain=chain.then(async()=>{ processing=true; try { await message(event.data); } finally { processing=false; } }).catch(async error=>{
    if (runtime) invoke('glob2_record_fail',['string'],[String(error.message || error)]);
    else postMessage({type:'status',status:{state:5,error:String(error.message || error)}});
    await finishStorage();
  });
};
