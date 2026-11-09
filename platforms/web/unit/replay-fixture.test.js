const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '../../..');

test('browser replay fixture stays within the engine replay acceptance boundary', () => {
  const bytes = fs.readFileSync(path.join(root, 'platforms/web/tests/fixtures/cross-replay.replay'));
  assert.equal(bytes.subarray(4, 16).toString(), 'replayHeader');
  const version = bytes.readUInt32BE(20);
  const reader = fs.readFileSync(path.join(root, 'src/replay/ReplayReader.h'), 'utf8');
  const versions = fs.readFileSync(path.join(root, 'src/app/Version.h'), 'utf8');
  // The floor is a number or a named format constant from FileFormatVersions.h.
  const floor = reader.match(/REPLAY_MINIMUM_VERSION_MINOR\s*=\s*(\w+)/)[1];
  const formats = fs.readFileSync(path.join(root, 'src/app/FileFormatVersions.h'), 'utf8');
  const minimum = /^\d+$/.test(floor) ? Number(floor)
    : Number(formats.match(new RegExp(`\\b${floor}\\s*=\\s*(\\d+)`))[1]);
  const current = Number(versions.match(/#define VERSION_MINOR\s+(\d+)/)[1]);
  assert.ok(version >= minimum && version <= current,
    `Replay fixture ${version} is outside ${minimum}..${current}; re-record it using fixtures/README.md`);
});

// The Studio specs compare browser traces with a live native run of the same
// scenario, so their committed manifests name inputs only: no outputs, versions
// or sim keys that every simulation change would have to refresh by hand.
const scenarios = {
  'studio-scenario.json': ['source', 'map', 'seed', 'opponent', 'ticks'],
  'generator-studio-scenario.json': ['manifest', 'source', 'generator', 'seed', 'params', 'candidates', 'player', 'ticks'],
};
for (const [name, keys] of Object.entries(scenarios)) {
  test(`${name} pins only existing scenario inputs`, () => {
    const scenario = JSON.parse(fs.readFileSync(path.join(root, 'platforms/web/tests/fixtures', name)));
    assert.deepEqual(Object.keys(scenario).sort(), [...keys].sort());
    for (const key of ['source', 'map', 'manifest'])
      if (key in scenario) assert.ok(fs.existsSync(path.join(root, scenario[key])), `${name}: ${scenario[key]} is missing`);
  });
}
