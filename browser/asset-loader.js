// SPDX-License-Identifier: GPL-3.0-or-later
// Writes the game's data packages (built by scons/web_assets.py) into the virtual
// file system. The core package is a run dependency: main() starts only after it
// is installed, exactly as with Emscripten's --preload-file. Optional packages
// (in-game music, high-resolution artwork) are fetched only when the page asks,
// part by part, and become visible to the game all at once when complete.
//
// Package URLs are content-addressed, so a downloaded part is kept in the Cache
// Storage API and reused on later visits without touching the network. Where Cache
// Storage is unavailable the HTTP cache still applies (immutable responses).
class Glob2AssetLoader {
  constructor(manifest, host) {
    this.manifest = manifest;
    this.host = host;
    this.states = {};
    this.progress = {};
    this.cache = null;
    this.cacheReady = null;
    for (const entry of manifest.packages) this.states[entry.name] = entry.optional ? 'idle' : 'pending';
  }
  static cacheName = 'glob2-assets-v1';
  package(name) {
    const entry = this.manifest.packages.find(candidate => candidate.name === name);
    if (!entry) throw Error('Unknown asset package ' + name);
    return entry;
  }
  state() { return Object.freeze({...this.states}); }
  openCache() {
    this.cacheReady ??= (async () => {
      try { this.cache = await this.host.caches?.open(Glob2AssetLoader.cacheName) || null; } catch (_) { this.cache = null; }
    })();
    return this.cacheReady;
  }
  // Progress in transfer bytes where the page knows a compressed part's size
  // (host.wireSize), otherwise in the decoded bytes the manifest records.
  report(name, loaded, total) {
    this.progress[name] = loaded;
    this.host.onProgress?.(name, Math.min(loaded, total), total);
  }
  wire(part, encoding) {
    return this.host.wireSize?.(part.url, encoding) || part.size;
  }
  async fetchPart(part, counted, sized) {
    await this.openCache();
    const url = this.host.resolve(part.url);
    if (this.cache) {
      try {
        const cached = await this.cache.match(url);
        if (cached) {
          const bytes = new Uint8Array(await cached.arrayBuffer());
          if (bytes.length === part.size) return {bytes, cached:true};
          await this.cache.delete(url);
        }
      } catch (_) { /* fall through to the network */ }
    }
    const response = await this.host.fetch(url, {credentials:'same-origin'});
    if (!response.ok) throw Error('HTTP ' + response.status + ' for ' + part.url);
    const encoding = response.headers?.get?.('Content-Encoding');
    const wire = encoding ? this.wire(part, encoding) : part.size;
    sized?.(wire);
    const scale = wire / part.size;
    const bytes = new Uint8Array(part.size);
    let offset = 0;
    if (response.body?.getReader) {
      const reader = response.body.getReader();
      for (;;) {
        const {done, value} = await reader.read();
        if (done) break;
        if (offset + value.length > bytes.length) throw Error('Unexpected size for ' + part.url);
        bytes.set(value, offset);
        offset += value.length;
        counted(value.length * scale);
      }
    } else {
      const whole = new Uint8Array(await response.arrayBuffer());
      if (whole.length > bytes.length) throw Error('Unexpected size for ' + part.url);
      bytes.set(whole);
      offset = whole.length;
      counted(whole.length * scale);
    }
    if (offset !== part.size) throw Error('Incomplete download of ' + part.url);
    if (this.cache) {
      try {
        await this.cache.put(url, new this.host.Response(bytes, {headers:{'Content-Type':'application/octet-stream'}}));
      } catch (_) { /* quota or private mode: the HTTP cache still applies */ }
    }
    return {bytes, cached:false};
  }
  // Download every part of a package; `between` runs before each network part.
  async download(name, between) {
    const entry = this.package(name);
    const contents = [];
    // Expect the Brotli copies until a response says otherwise.
    const totals = entry.parts.map(part => this.wire(part, 'br'));
    const total = () => totals.reduce((sum, size) => sum + size, 0);
    let loaded = 0;
    this.report(name, 0, total());
    for (const [index, part] of entry.parts.entries()) {
      await between?.();
      const start = loaded;
      const counted = count => { loaded += count; this.report(name, loaded, total()); };
      // A cached part counts as its expected size, reached at once.
      const {cached, bytes} = await this.fetchPart(part, counted, size => { totals[index] = size; });
      if (cached) { loaded = start + totals[index]; this.report(name, loaded, total()); }
      contents.push(bytes);
    }
    return contents;
  }
  install(name, contents) {
    const entry = this.package(name);
    const directories = new Set();
    entry.parts.forEach((part, index) => {
      for (const [path, start, end] of part.files) {
        const slash = path.lastIndexOf('/');
        const directory = path.slice(0, slash) || '/';
        if (!directories.has(directory)) {
          this.host.fs.createPath('/', directory.slice(1), true, true);
          directories.add(directory);
        }
        // The file system owns the slice; packages are never written back.
        this.host.fs.createDataFile(directory, path.slice(slash + 1), contents[index].subarray(start, end), true, true, true);
      }
    });
    this.states[name] = 'ready';
  }
  async load(name, between) {
    if (this.states[name] === 'ready') return;
    this.states[name] = 'downloading';
    try {
      this.install(name, await this.download(name, between));
    } catch (error) {
      this.states[name] = 'failed';
      throw error;
    }
  }
  // Remove cached parts that the current manifest no longer lists.
  async prune() {
    await this.openCache();
    if (!this.cache) return;
    const current = new Set(this.manifest.packages.flatMap(entry => entry.parts.map(part => this.host.resolve(part.url))));
    try {
      for (const request of await this.cache.keys())
        if (!current.has(request.url)) await this.cache.delete(request);
    } catch (_) { /* best effort */ }
  }
}
if (typeof module !== 'undefined' && module.exports) module.exports = Glob2AssetLoader;

if (typeof Module !== 'undefined' && Module.glob2AssetManifest && typeof window === 'object') {
  // Resolve like Emscripten's own data packages: relative to the page, through
  // Module.locateFile when the page provides one.
  const resolve = url => new URL(Module.locateFile?.(url, '') ?? url, document.baseURI).href;
  const loader = new Glob2AssetLoader(Module.glob2AssetManifest, {
    fetch: (...args) => fetch(...args),
    caches: typeof caches !== 'undefined' ? caches : null,
    Response,
    resolve,
    wireSize: (url, encoding) => Module.glob2WireSize?.(url, encoding),
    get fs() { return FS; },
    onProgress: (name, loaded, total) => Module.glob2AssetProgress?.(name, loaded, total),
  });
  Module.glob2Assets = loader;
  // Start the core download while the runtime and WebAssembly are still loading.
  const core = loader.download('core');
  loader.states.core = 'downloading';
  if (typeof Module.preRun === 'function') Module.preRun = [Module.preRun];
  (Module.preRun ??= []).push(() => {
    addRunDependency('glob2-assets-core');
    core.then(contents => {
      loader.install('core', contents);
      removeRunDependency('glob2-assets-core');
      loader.prune();
    }, error => {
      loader.states.core = 'failed';
      Module.printErr?.('Game data download failed: ' + (error?.message || error));
      if (Module.glob2AssetError) Module.glob2AssetError(error);
      else Module.setStatus?.('Game data download failed');
    });
  });
}
