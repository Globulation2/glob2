// SPDX-License-Identifier: GPL-3.0-or-later
// Synchronous storage is owned by the recording worker.
const EOF = -541478725, IO_ERROR = -5;
const recordingStorage = {
  files: new Map(), descriptors: new Map(), reservations: new Map(), next: 1,
  async initialize() {
    if (!navigator.storage?.getDirectory) throw new Error('Persistent recording storage is unavailable');
    try {this.root = await (await navigator.storage.getDirectory()).getDirectoryHandle('glob2-recordings', {create:true});}
    catch (e) {throw new Error('Persistent recording storage is unavailable: '+(e.message || e));}
  },
  name(path) { return encodeURIComponent(path); },
  async exists(path) {
    try { await this.root.getFileHandle(this.name(path)); return true; }
    catch (e) { if (e.name === 'NotFoundError') return false; throw e; }
  },
  async prepare(path,sessionIndex=false) {
    if (!navigator.locks) throw new Error('Recording storage locks are unavailable');
    if (!this.locks) this.locks = new Map();
    if (!this.locks.has(path)) await new Promise((resolve,reject)=>{
      navigator.locks.request('glob2-recording:'+path,{ifAvailable:true},async lock=>{
        if (!lock) { reject(new Error('Recording output is in use')); return; }
        await new Promise(release=>{ this.locks.set(path,release); resolve(); });
      }).catch(reject);
    });
    if (this.reservations.has(path)) return;
    const outputs = [path, path+'.json', path+'.events.jsonl', path+'.complete'];
    if (sessionIndex) outputs.push(path+'.session.json');
    for (const name of outputs) if (await this.exists(name)) throw new Error('Recording output already exists or is reserved');
    const files = [...outputs, ...['capture.mp4','final.mp4','manifest.json','manifest.1.json','manifest.2.json','events.jsonl'].map(name => path+'.recording/'+name)];
    this.reservations.set(path,false);
    for (const name of files) {
      const handle = await this.root.getFileHandle(this.name(name), {create:true});
      const sync = await handle.createSyncAccessHandle();
      this.files.set(name, {handle,sync});
    }
    this.reservations.set(path,false);
  },
  reserve(path) {
    if (!this.reservations.has(path) || this.reservations.get(path)) return IO_ERROR;
    this.reservations.set(path,true); return 0;
  },
  async prepareRecovery(path) {
    const names=[path,path+'.json',path+'.events.jsonl',path+'.complete',
      ...['capture.mp4','final.mp4','manifest.json','manifest.1.json','manifest.2.json','events.jsonl'].map(name=>path+'.recording/'+name)];
    for (const name of names) {
      let handle;
      try { handle=await this.root.getFileHandle(this.name(name)); }
      catch (e) { if (/manifest\.[12]\.json$/.test(name) && e.name==='NotFoundError') continue; throw e; }
      this.files.set(name,{handle,sync:await handle.createSyncAccessHandle()});
    }
    if (this.files.get(path+'.complete').sync.getSize()) throw new Error('Recording is already complete');
  },
  open(path, write) {
    const file = this.files.get(path); if (!file) return IO_ERROR;
    try {
      if (write) file.sync.truncate(0);
      const id = this.next++; this.descriptors.set(id,{file,position:0,write}); return id;
    } catch (_) { return IO_ERROR; }
  },
  read(id, bytes) {
    try { const d=this.descriptors.get(id), n=d.file.sync.read(bytes,{at:d.position}); d.position+=n; return n || EOF; }
    catch (_) { return IO_ERROR; }
  },
  write(id, bytes) {
    try { const d=this.descriptors.get(id); if (!d.write) return IO_ERROR;
      const n=d.file.sync.write(bytes,{at:d.position}); d.position+=n; return n===bytes.length ? n : IO_ERROR; }
    catch (_) { return IO_ERROR; }
  },
  seek(id, offset, whence) {
    try { const d=this.descriptors.get(id); if (whence===65536) return d.file.sync.getSize();
      const position=offset+(whence===1 ? d.position : whence===2 ? d.file.sync.getSize() : 0);
      if (!Number.isSafeInteger(position) || position<0) return IO_ERROR; d.position=position; return position; }
    catch (_) { return IO_ERROR; }
  },
  flush(id) { try { this.descriptors.get(id).file.sync.flush(); return 0; } catch (_) { return IO_ERROR; } },
  close(id) { this.descriptors.delete(id); },
  copy(source,destination) {
    const from=this.files.get(source).sync,to=this.files.get(destination).sync,buffer=new Uint8Array(65536);
    to.truncate(0);
    for (let at=0,n; (n=from.read(buffer,{at})); at+=n) {
      if (to.write(buffer.subarray(0,n),{at})!==n) throw new Error('Recording storage write failed');
    }
    to.flush();
  },
  publish(work,path) {
    try {
      this.copy(work+'manifest.json',path+'.json'); this.copy(work+'events.jsonl',path+'.events.jsonl');
      this.copy(work+'final.mp4',path);
      const marker=this.files.get(path+'.complete').sync;
      marker.write(new Uint8Array([1]),{at:0}); marker.flush();
      // WebKit prevents file-backed readers while a synchronous writer is open.
      for (const name of [path,path+'.json',path+'.events.jsonl',path+'.complete']) {
        this.files.get(name).sync.close(); this.files.delete(name);
      }
      return 0;
    } catch (_) { return IO_ERROR; }
  },
  cleanup(path) {
    for (const [name,file] of this.files) if (name.startsWith(path)) {
      file.sync.close(); this.files.delete(name);
      // Removing intermediates is asynchronous; publication already committed.
      (this.cleanups ||= []).push(this.root.removeEntry(this.name(name)).catch(()=>{}));
    }
  },
  async closeAll() {
    const names=[...this.files.keys()];
    for (const file of this.files.values()) { try { file.sync.flush(); file.sync.close(); } catch (_) {} }
    this.files.clear();
    const removals=this.cleanups || [];
    for (const [path,claimed] of this.reservations) if (!claimed) {
      for (const name of names) if ([path,path+'.json',path+'.events.jsonl',path+'.session.json',path+'.complete'].includes(name) || name.startsWith(path+'.recording/')) removals.push(this.root.removeEntry(this.name(name)).catch(()=>{}));
    }
    await Promise.all(removals);
    for (const release of this.locks?.values() || []) release(); this.locks?.clear();
  }
};
