const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '../..');

test('browser replay fixture stays within the engine replay acceptance boundary', () => {
  const bytes = fs.readFileSync(path.join(root, 'browser/tests/fixtures/cross-replay.replay'));
  assert.equal(bytes.subarray(4, 16).toString(), 'replayHeader');
  const version = bytes.readUInt32BE(20);
  const reader = fs.readFileSync(path.join(root, 'src/replay/ReplayReader.h'), 'utf8');
  const versions = fs.readFileSync(path.join(root, 'src/app/Version.h'), 'utf8');
  const minimum = Number(reader.match(/REPLAY_MINIMUM_VERSION_MINOR\s*=\s*(\d+)/)[1]);
  const current = Number(versions.match(/#define VERSION_MINOR\s+(\d+)/)[1]);
  assert.ok(version >= minimum && version <= current,
    `Replay fixture ${version} is outside ${minimum}..${current}; re-record it using fixtures/README.md`);
});

for (const name of ['generator-studio-trace.json', 'studio-trace.json']) {
  test(`${name} tracks the engine and simulation revisions`, () => {
    const fixture = JSON.parse(fs.readFileSync(path.join(root, 'browser/tests/fixtures', name)));
    const versions = fs.readFileSync(path.join(root, 'src/app/Version.h'), 'utf8');
    const simulation = fs.readFileSync(path.join(root, 'src/game/SimRevision.h'), 'utf8');
    assert.equal(fixture.versionMinor, Number(versions.match(/#define VERSION_MINOR\s+(\d+)/)[1]),
      'Refresh the native Studio reference after reviewing the version change');
    assert.equal(fixture.simRevision, Number(simulation.match(/#define SIM_REVISION\s+(\d+)/)[1]),
      'Refresh the native Studio reference after reviewing the simulation change');
  });
}
