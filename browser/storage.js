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
