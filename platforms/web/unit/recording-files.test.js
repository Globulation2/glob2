// SPDX-License-Identifier: GPL-3.0-or-later
// The browser Recordings list reads site storage (OPFS) directly; it must describe
// each saved recording well enough to tell them apart before downloading.
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');
const source = readFileSync(path.join(__dirname, '../recording.js'), 'utf8');

// A flat OPFS directory whose entries are named encodeURIComponent(path).
function directory(files) {
  const entries = new Map(Object.entries(files).map(([name, file]) => [encodeURIComponent(name), {
    kind: 'file',
    async getFile() { return {size: file.text.length, lastModified: file.modified, async text() { return file.text; }}; },
  }]));
  return {
    async *entries() { yield* entries.entries(); },
    async getFileHandle(name) {
      if (!entries.has(name)) throw Object.assign(new Error('missing ' + name), {name: 'NotFoundError'});
      return entries.get(name);
    },
  };
}

function filesUI(files) {
  const root = directory(files);
  const context = vm.createContext({
    Module: {},
    navigator: {storage: {async getDirectory() { return {async getDirectoryHandle() { return root; }}; }}},
    decodeURIComponent, encodeURIComponent, JSON,
  });
  vm.runInContext(source, context);
  return context.Module.glob2RecordingFilesUI;
}

test('browser recordings list newest first with their size and duration', async () => {
  const ui = filesUI({
    'glob2-2026-11-18_09-00-00.mp4': {text: 'v'.repeat(2048), modified: 1000},
    'glob2-2026-11-18_09-00-00.mp4.json': {text: '{"duration_us":5000000}', modified: 1000},
    'glob2-2026-11-18_09-00-00.mp4.complete': {text: '1', modified: 1000},
    'glob2-2026-11-18_10-00-00.mp4': {text: 'v'.repeat(4096), modified: 2000},
    'glob2-2026-11-18_10-00-00.mp4.json': {text: '{"duration_us":23266666}', modified: 2000},
    'glob2-2026-11-18_10-00-00.mp4.complete': {text: '1', modified: 2000},
    'glob2-2026-11-18_11-00-00.mp4.recording/manifest.json': {text: '{}', modified: 3000},
  });
  await ui.refresh();
  assert.equal(ui.error, '');
  const files = JSON.parse(JSON.stringify(ui.status().files));
  assert.deepEqual(files.map(file => [file.path, file.recoverable]), [
    ['glob2-2026-11-18_11-00-00.mp4', true],
    ['glob2-2026-11-18_10-00-00.mp4', false],
    ['glob2-2026-11-18_09-00-00.mp4', false],
  ]);
  assert.equal(files[1].bytes, 4096);
  assert.equal(files[1].durationUs, 23266666);
  assert.equal(files[1].modified, 2000);
  assert.equal(files[2].bytes, 2048);
  assert.equal(files[2].durationUs, 5000000);
});

test('a saved recording without readable metadata is still listed', async () => {
  const ui = filesUI({
    'clip.mp4.complete': {text: '1', modified: 5},
  });
  await ui.refresh();
  const [file] = ui.status().files;
  assert.equal(file.path, 'clip.mp4');
  assert.equal(file.bytes, 0);
  assert.equal(file.durationUs, 0);
});
