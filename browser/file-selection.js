// SPDX-License-Identifier: GPL-3.0-or-later
// Read browser-selected files without trusting a name, extension, or reported size.
// Format validation belongs to the shared game loader after this boundary.
class Glob2FileSelection {
  constructor(extensions, limit = 64 * 1024 * 1024) {
    this.extensions = new Set(extensions);
    this.limit = limit;
    this.state = 'pending';
    this.file = null;
    this.error = null;
    this.disposed = false;
  }
  async select(file) {
    if (this.disposed || this.state !== 'pending') return;
    if (!file) { this.state = 'cancelled'; return; }
    try {
      const name = file.name;
      if (typeof name !== 'string' || !name.length || name.length > 128 ||
          /[\\/<>:"|?*\x00-\x1f\x7f]/.test(name) || name === '.' || name === '..' ||
          name.endsWith('.') || name.endsWith(' ')) throw new Error('Invalid file name');
      const dot = name.lastIndexOf('.');
      if (dot <= 0) throw new Error('Missing file extension');
      const extension = name.slice(dot + 1).toLowerCase();
      if (!this.extensions.has(extension)) throw new Error('Unsupported file type');
      if (!Number.isSafeInteger(file.size) || file.size <= 0 || file.size > this.limit)
        throw new Error('File exceeds import size limit');
      this.state = 'reading';
      const bytes = new Uint8Array(await file.arrayBuffer());
      if (this.disposed) return;
      if (bytes.length !== file.size || bytes.length > this.limit) throw new Error('File size changed while reading');
      this.file = {name, bytes};
      this.state = 'selected';
    } catch (error) {
      if (!this.disposed) { this.error = error; this.state = 'failed'; }
    }
  }
  pick(document) {
    if (this.disposed || this.input || this.state !== 'pending') return;
    const input = this.input = document.createElement('input');
    input.type = 'file'; input.accept = [...this.extensions].map(extension => '.' + extension).join(',');
    input.hidden = true;
    const cleanup = () => { input.remove(); if (this.input === input) this.input = null; };
    input.addEventListener('change', () => { this.select(input.files?.[0] || null); cleanup(); }, {once:true});
    input.addEventListener('cancel', () => { this.select(null); cleanup(); }, {once:true});
    document.body.appendChild(input);
    try { input.click(); }
    catch (error) { this.error = error; this.state = 'failed'; cleanup(); }
  }
  dispose() {
    this.disposed = true; this.file = null;
    if (this.input) { this.input.remove(); this.input = null; }
  }
}
if (typeof module !== 'undefined' && module.exports) module.exports = Glob2FileSelection;
if (typeof globalThis !== 'undefined') globalThis.Glob2FileSelection = Glob2FileSelection;
