// SPDX-License-Identifier: GPL-3.0-or-later
// The adapter supplies persistence and scheduling; no game or filesystem formats live here.
class Glob2Storage {
  constructor(persist, schedule = callback => setTimeout(callback, 0)) {
    this.persist = persist;
    this.schedule = schedule;
    this.generation = 0;
    this.committed = 0;
    this.active = false;
    this.scheduled = false;
    this.error = null;
    this.waiters = [];
    this.state = 'restoring';
  }
  restored(error) {
    this.error = error || null;
    this.state = error ? 'restore-failed' : 'persisted';
  }
  changed() {
    ++this.generation;
    if (this.state === 'restoring' || this.state === 'restore-failed') return;
    this.error = null;
    this.state = 'writing';
    this.enqueue();
  }
  flush() {
    if (this.state === 'restoring' || this.state === 'restore-failed')
      return Promise.reject(this.error || new Error('Storage has not restored'));
    this.changed();
    return new Promise((resolve, reject) => this.waiters.push({generation:this.generation, resolve, reject}));
  }
  enqueue() {
    if (this.active || this.scheduled) return;
    this.scheduled = true;
    this.schedule(() => { this.scheduled = false; this.write(); });
  }
  write() {
    if (this.active || this.committed === this.generation) return;
    this.active = true;
    const generation = this.generation;
    const complete = error => {
      this.active = false;
      this.error = error || null;
      if (!error) this.committed = generation;
      const remaining = [];
      for (const waiter of this.waiters) {
        if (error) waiter.reject(error);
        else if (waiter.generation <= generation) waiter.resolve();
        else remaining.push(waiter);
      }
      this.waiters = remaining;
      this.state = error ? 'failed' : this.committed === this.generation ? 'persisted' : 'writing';
      if (!error && this.committed !== this.generation) this.enqueue();
    };
    try { this.persist(complete); } catch (error) { complete(error); }
  }
}
if (typeof module !== 'undefined' && module.exports) module.exports = Glob2Storage;
if (typeof globalThis !== 'undefined') globalThis.Glob2Storage = Glob2Storage;

// IDBFS normally reads file bytes after its asynchronous database scan. Worker
// writes can rename/delete those paths meanwhile. Capture bytes and metadata in
// one UI turn, then let the SDK commit that immutable snapshot asynchronously.
function glob2SnapshotPersistence(idbfs) {
  const original = idbfs.syncfs;
  const load = idbfs.loadLocalEntry;
  idbfs.syncfs = (mount, populate, callback) => {
    if (populate) return original(mount, populate, callback);
    idbfs.getLocalSet(mount, (error, local) => {
      if (error) return callback(error);
      const snapshot = new Map();
      for (const path of Object.keys(local.entries)) {
        // The pinned SDK's local loader is synchronous, as is reconciliation's
        // invocation of it. Keep this adapter checked when upgrading IDBFS.
        load(path, (failure, entry) => {
          error ||= failure;
          if (!failure) snapshot.set(path, {...entry,
            ...(entry.contents ? {contents:entry.contents.slice()} : {})});
        });
      }
      if (error) return callback(error);
      idbfs.getRemoteSet(mount, (failure, remote) => {
        if (failure) return callback(failure);
        const previous = idbfs.loadLocalEntry;
        idbfs.loadLocalEntry = (path, done) => done(null, snapshot.get(path));
        try { idbfs.reconcile(local, remote, callback); }
        catch (failure) { callback(failure); }
        finally { idbfs.loadLocalEntry = previous; }
      });
    });
  };
}
Glob2Storage.installSnapshotPersistence = glob2SnapshotPersistence;
if (typeof globalThis !== 'undefined') globalThis.glob2SnapshotPersistence = glob2SnapshotPersistence;
