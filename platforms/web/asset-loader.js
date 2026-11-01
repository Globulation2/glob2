// SPDX-License-Identifier: GPL-3.0-or-later
// Writes the game's data packages (built by scons/web_assets.py) into the virtual
// file system. Core and game are run dependencies: main() starts only after
// both are installed, exactly as with Emscripten's --preload-file. Optional packages
// (in-game music, high-resolution artwork) are fetched only when the page asks,
// with bounded parallel transfers, and become visible to the game all at once when complete.
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
    this.installed = [];
    this.retryAfter = {};
    this.inFlight = new Map();
    this.installing = Promise.resolve();
    this.activeParts = 0;
    this.partWaiters = [];
    this.partConcurrency = Math.max(1, Math.min(16, host.partConcurrency || 4));
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
  async acquirePart() {
    if (this.activeParts < this.partConcurrency) { this.activeParts++; return; }
    await new Promise(resolve => this.partWaiters.push(resolve));
  }
  releasePart() {
    const next = this.partWaiters.shift();
    if (next) next(); else this.activeParts--;
  }
  async fetchPart(part, counted, sized, between) {
    await this.acquirePart();
    try {
      await between?.();
      return await this.readPart(part, counted, sized);
    }
    finally { this.releasePart(); }
  }
  async readPart(part, counted, sized) {
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
    const progress = entry.parts.map(() => 0);
    await Promise.all(entry.parts.map(async (part, index) => {
      const counted = count => {
        progress[index] += count;
        loaded = progress.reduce((sum, value) => sum + value, 0);
        this.report(name, loaded, total());
      };
      const {cached, bytes} = await this.fetchPart(part, counted, size => { totals[index] = size; }, between);
      if (cached) {
        progress[index] = totals[index];
        loaded = progress.reduce((sum, value) => sum + value, 0);
        this.report(name, loaded, total());
      }
      contents[index] = bytes;
    }));
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
        // A later package may replace a file (the full font replaces the core
        // package's subset); the game reopens it when told (takeInstalled).
        if (this.host.fs.analyzePath(path).exists) this.host.fs.unlink(path);
        // The file system owns the slice; packages are never written back.
        this.host.fs.createDataFile(directory, path.slice(slash + 1), contents[index].subarray(start, end), true, true, true);
      }
    });
    this.states[name] = 'ready';
    this.installed.push(name);
  }
  // Packages installed since the last call (ApplicationHost::takeInstalledAssetPackages).
  takeInstalled() { return this.installed.splice(0); }
  // Non-blocking demand from the running game. Repeated frames share the
  // in-flight load; failed optional downloads retry at most once per ten seconds.
  request(name, now = Date.now()) {
    if (this.states[name] === 'ready' || this.states[name] === 'downloading' ||
        now < (this.retryAfter[name] || 0)) return;
    this.retryAfter[name] = now + 10000;
    void this.load(name).catch(() => {}); // state() exposes failure; gameplay continues.
  }
  load(name, between) {
    if (this.states[name] === 'ready') return Promise.resolve();
    if (this.inFlight.has(name)) return this.inFlight.get(name);
    this.states[name] = 'downloading';
    const downloaded = this.download(name, between);
    downloaded.catch(() => {});
    // Downloads overlap, installation follows request order so replacements win
    // consistently regardless of which package finishes first.
    const installed = this.installing.then(async () => {
      try { this.install(name, await downloaded); }
      catch (error) { this.states[name] = 'failed'; throw error; }
    });
    this.installing = installed.catch(() => {});
    const pending = installed.finally(() => this.inFlight.delete(name));
    this.inFlight.set(name, pending);
    return pending;
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
  // Fetch both startup packages alongside WebAssembly, rather than making the
  // live menu wait for another download after the loading page disappears.
  const startup = new Map();
  for (const name of ['core', 'game']) {
    if (!loader.manifest.packages.some(entry => entry.name === name)) continue;
    loader.states[name] = 'downloading';
    const contents = loader.download(name);
    // A fast failure can precede preRun; requireNow reports it below.
    contents.catch(() => {});
    startup.set(name, contents);
  }
  // Startup packages are run dependencies, like --preload-file: main() waits for
  // them. Core and game always; the page may add others (the full font for a Chinese,
  // Japanese or Korean interface) until the runtime starts. They install in
  // request order, so a later package can replace a core file.
  const required = new Set(), queued = [];
  let preRunDone = false, installing = Promise.resolve(), failed = false;
  const fail = (name, error) => {
    loader.states[name] = 'failed';
    if (failed) return;
    failed = true;
    Module.printErr?.('Game data download failed: ' + (error?.message || error));
    if (Module.glob2AssetError) Module.glob2AssetError(error);
    else Module.setStatus?.('Game data download failed');
  };
  const requireNow = name => {
    if (required.has(name) || loader.states[name] === 'ready' || !loader.manifest.packages.some(entry => entry.name === name)) return;
    required.add(name);
    const dependency = 'glob2-assets-' + name;
    addRunDependency(dependency);
    loader.states[name] = 'downloading';
    const contents = startup.get(name) || loader.download(name);
    installing = installing.then(async () => {
      try {
        loader.install(name, await contents);
        removeRunDependency(dependency);
        if (name === 'core') loader.prune();
      } catch (error) { fail(name, error); }
    });
  };
  Module.glob2RequireAsset = name => { if (preRunDone) requireNow(name); else queued.push(name); };
  if (typeof Module.preRun === 'function') Module.preRun = [Module.preRun];
  (Module.preRun ??= []).push(() => {
    preRunDone = true;
    requireNow('core');
    if (startup.has('game')) requireNow('game');
    for (const name of queued.splice(0)) requireNow(name);
  });
}
