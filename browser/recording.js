// SPDX-License-Identifier: GPL-3.0-or-later
// Runs in the producing game realm (UI for serial builds, app worker for threaded).
Module.glob2Recording = {
  worker:null, status:null, frames:0, audio:0, active:false,
  start(path,fps,crf,software,chapters,baseURL,time) {
    this.worker?.terminate(); this.frames=this.audio=0; this.status=null; this.active=true;
    this.worker=new Worker(new URL('recording-worker.js',baseURL));
    this.worker.onmessage=event=>{
      const m=event.data;
      if (m.type==='status') { this.status=m.status; if (m.status.state>=4) this.active=false; if (m.status.state===5) Module.printErr?.('Recording: '+m.status.error); }
      else if (m.type==='ack' && m.kind==='frame') this.frames--;
      else if (m.type==='ack' && m.kind==='audio') this.audio-=m.samples;
      else if (m.type==='diagnostic') Module.printErr?.('Recording: '+m.value);
    };
    this.worker.onerror=event=>{ this.status={state:5,error:event.message || 'Recording worker failed'}; this.active=false; };
    this.worker.postMessage({type:'start',path,fps,crf,software,chapters,time,wallTime:performance.timeOrigin+performance.now()});
  },
  frame(pointer,width,height,time,context) {
    if (!this.active || this.frames>=3) return false;
    const details=JSON.parse(context),pixelBytes=(details.pixel_layout || 0)<4 ? 4 : 3;
    const bytes=HEAPU8.slice(pointer,pointer+width*height*pixelBytes).buffer;
    this.frames++; this.worker.postMessage({type:'frame',bytes,width,height,time,context:details},[bytes]); return true;
  },
  pcm(pointer,count,time) {
    if (!this.active || this.audio+count>44100*2*2) return false;
    const bytes=HEAPU8.slice(pointer,pointer+count*2).buffer;
    this.audio+=count; this.worker.postMessage({type:'audio',bytes,time},[bytes]); return true;
  },
  message(m) { this.worker?.postMessage(m); },
  takeStatus() { const status=this.status; this.status=null; return status; }
};
if (typeof ENVIRONMENT_IS_PTHREAD === 'undefined' || !ENVIRONMENT_IS_PTHREAD) {
  Module.glob2RecordingFilesUI = {
    busy:false,error:'',files:[],worker:null,
    status() { return {busy:this.busy,error:this.error,files:this.files}; },
    async root() { return (await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings',{create:true}); },
    async scan() {
      const root=await this.root(),files=[];
      for await (const [name,handle] of root.entries()) {
        const path=decodeURIComponent(name);
        let size,modified;
        try { const file=await handle.getFile();size=file.size;modified=file.lastModified; } catch (_) { continue; }
        if (path.endsWith('.complete') && size) files.push({path:path.slice(0,-9),recoverable:false,modified});
        else if (path.endsWith('.recording/manifest.json')) {
          if (!size) {
            for (const suffix of ['manifest.1.json','manifest.2.json']) {
              try { size ||= (await(await root.getFileHandle(encodeURIComponent(path.slice(0,-13)+suffix))).getFile()).size; } catch (_) {}
            }
            if (!size) continue;
          }
          const video=path.slice(0,-24);
          let complete=false;
          try { complete=(await (await root.getFileHandle(encodeURIComponent(video+'.complete'))).getFile()).size>0; } catch (_) {}
          if (!complete) files.push({path:video,recoverable:true,modified});
        }
      }
      this.files=files.sort((a,b)=>b.modified-a.modified || b.path.localeCompare(a.path));
    },
    async refresh() {
      if (this.busy) return; this.busy=true; this.error='';
      try { await this.scan(); } catch (e) { this.error=String(e.message || e); }
      finally { this.busy=false; }
    },
    async remove(path) {
      if (this.busy) return; this.busy=true; this.error='';
      try {
        const root=await this.root();
        for await (const [name] of root.entries()) {
          const decoded=decodeURIComponent(name);
          if ([path,path+'.json',path+'.events.jsonl',path+'.session.json',path+'.complete'].includes(decoded) || decoded.startsWith(path+'.recording/')) await root.removeEntry(name);
        }
        await this.scan();
      } catch (e) { this.error=String(e.message || e); }
      finally { this.busy=false; }
    },
    recover(path) {
      if (this.busy) return; this.busy=true; this.error=''; this.worker?.terminate();
      this.worker=new Worker(new URL('recording-worker.js',document.baseURI));
      this.worker.onmessage=event=>{
        const m=event.data;
        if (m.type==='status' && m.status.state>=4) {
          this.error=m.status.error || ''; this.busy=false;
          if (!this.error) this.refresh();
        }
      };
      this.worker.onerror=event=>{ this.error=event.message || 'Recording recovery failed'; this.busy=false; };
      this.worker.postMessage({type:'recover',path});
    }
  };
}
