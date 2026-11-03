#!/usr/bin/env node
// A stand-in for the glob2 binary in engine-agent unit tests. It speaks the
// same command line and writes the same files as the real headless commands
// (see src/engineCli.ts), with maps in the real header layout followed by a
// fake body: u32 width, u32 height.
//
// Behaviour switches (environment is not passed through by the agent, so they
// travel in the inputs instead):
//   generator param  moat=9          generation refused (exit 2, result.json status invalid_request)
//   map name         "hang"          map preview never exits (timeout test)
//   record JSON      {"verdict":…}   written to verdict.json; "exit" overrides the exit code;
//                                    "noVerdict": true writes nothing
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { gzipSync } from 'node:zlib';

const args = process.argv.slice(2);
const command = args.slice(0, 2).join(' ');

function option(name) {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
}
function options(name) {
  const values = [];
  args.forEach((a, i) => {
    if (a === name) values.push(args[i + 1]);
  });
  return values;
}

function fakeMap({
  name = 'fake',
  minor = 125,
  teams = 2,
  saved = false,
  width = 128,
  height = 128,
}) {
  const nameBytes = Buffer.from(name, 'utf8');
  const buffer = Buffer.alloc(4 + nameBytes.length + 17 + 8 + 64);
  let o = 0;
  buffer.writeUInt32BE(nameBytes.length, o);
  o += 4;
  nameBytes.copy(buffer, o);
  o += nameBytes.length;
  buffer.writeInt32BE(0, o);
  buffer.writeInt32BE(minor, o + 4);
  buffer.writeInt32BE(teams, o + 8);
  buffer.writeUInt32BE(1234, o + 12);
  buffer.writeUInt8(saved ? 1 : 0, o + 16);
  o += 17;
  buffer.writeUInt32BE(width, o);
  buffer.writeUInt32BE(height, o + 4);
  return buffer;
}

function readFakeMap(bytes) {
  if (bytes.length < 4) return undefined;
  const length = bytes.readUInt32BE(0);
  if (length > 1024 || bytes.length < 4 + length + 25) return undefined;
  const name = bytes.subarray(4, 4 + length).toString('utf8');
  const o = 4 + length;
  return {
    name,
    minor: bytes.readInt32BE(o + 4),
    teams: bytes.readInt32BE(o + 8),
    saved: bytes.readUInt8(o + 16) === 1,
    width: bytes.readUInt32BE(o + 17),
    height: bytes.readUInt32BE(o + 21),
  };
}

function png(width, height) {
  const bytes = Buffer.alloc(33);
  Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]).copy(bytes, 0);
  bytes.writeUInt32BE(13, 8);
  bytes.write('IHDR', 12, 'ascii');
  bytes.writeUInt32BE(width, 16);
  bytes.writeUInt32BE(height, 20);
  return bytes;
}

function json(path, value) {
  writeFileSync(path, JSON.stringify(value));
}

const GENERATORS = [
  {
    method: 15,
    id: 'symmetric-arena',
    revision: 2,
    editorOnly: false,
    controls: [
      { id: 'width', values: [6, 7, 8, 9] },
      { id: 'height', values: [6, 7, 8, 9] },
      { id: 'teams', values: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12] },
      { id: 'workers', values: [1, 2, 3, 4, 5, 6, 7, 8] },
      { id: 'moat', values: [0, 1, 9] },
    ],
  },
  { method: 0, id: 'uniform', revision: 1, editorOnly: true, controls: [] },
];

switch (command) {
  case 'help --format': {
    process.stdout.write(JSON.stringify({ schema_version: 1, cli_version: 2, commands: [] }));
    break;
  }
  case 'info catalog': {
    process.stderr.write('startup noise on stderr\n');
    process.stdout.write(
      JSON.stringify({
        schema_version: 1,
        save_version: 125,
        protocol_version: 49,
        commands: ['game', 'generate_map'],
        generators: GENERATORS,
      }),
    );
    break;
  }
  case 'info sim-version': {
    // Like an old binary without the flag: never returns.
    setInterval(() => {}, 1000);
    break;
  }
  case 'map study': {
    const out = option('--output-dir');
    mkdirSync(out, { recursive: true });
    const params = Object.fromEntries(options('--set').map((p) => p.split('=')));
    if (params.moat === '9') {
      json(join(out, 'result.json'), {
        schema_version: 1,
        status: 'invalid_request',
        diagnostic: 'moat 9 is not buildable',
      });
      process.exit(2);
    }
    const seed = Number(option('--seed'));
    const side = 2 ** Number(params.width ?? 7);
    const teams = Number(params.teams ?? 2);
    const map = fakeMap({ name: `study-15-${seed}-r0`, teams, width: side, height: side });
    writeFileSync(join(out, 'map-r0.map.gz'), gzipSync(map));
    json(join(out, 'result.json'), {
      schema_version: 1,
      job_type: 'generate_map',
      status: 'completed',
      generator: 'symmetric-arena',
      revision: 2,
      map_seed: seed,
      chosen_seed: (seed * 7 + 1) % 4294967296,
      quality: { score: 0.99, fairness: 0.98 },
      map_report: {
        schema_version: 2,
        map: {
          name: null,
          width: side,
          height: side,
          player_slots: teams,
          saved_game: false,
          tick: 0,
        },
      },
    });
    break;
  }
  case 'map preview': {
    const input = args[2];
    const map = readFakeMap(readFileSync(input));
    if (!map) {
      process.stderr.write(`Map command: Cannot load map/save: ${input}\n`);
      process.exit(1);
    }
    if (map.name === 'hang') setInterval(() => {}, 1000);
    else {
      const report = option('--report-file');
      if (report) {
        json(report, {
          schema_version: 2,
          report_type: 'map',
          engine: { version_major: 0, version_minor: 125 },
          map: {
            name: map.name,
            width: map.width,
            height: map.height,
            player_slots: map.teams,
            saved_game: map.saved,
            tick: 0,
            // Saves name their players, except a save called "nameless"
            // (engines before the name field). The last seat is an AI.
            controllers: map.saved
              ? Array.from({ length: map.teams }, (_, slot) => ({
                  slot,
                  team: slot,
                  type: slot === map.teams - 1 ? 10 : 3,
                  ...(map.name === 'nameless' ? {} : { name: `${map.name}-p${slot}` }),
                }))
              : [],
          },
        });
      }
      const output = option('--output');
      if (output) {
        const size = Number(option('--preview-size') ?? 256);
        writeFileSync(output, png(size, size));
      }
    }
    break;
  }
  case 'match verify': {
    const record = JSON.parse(readFileSync(args[2], 'utf8'));
    const out = option('--output-dir');
    mkdirSync(out, { recursive: true });
    if (!record.noVerdict) {
      json(join(out, 'verdict.json'), {
        verdict: record.verdict,
        ...(record.seats ? { seats: record.seats } : {}),
        ...(record.reason ? { reason: record.reason } : {}),
        ...(record.orderRejections ? { orderRejections: record.orderRejections } : {}),
      });
      if (record.verdict !== 'unverifiable') {
        const result = JSON.parse(
          readFileSync(new URL('./fixtures/game-result.json', import.meta.url), 'utf8'),
        );
        (record.outcomes ?? []).forEach((outcome, team) => {
          result.teams[team].outcome = outcome;
          if (outcome === 'lost') result.teams[team].eliminated_tick = 2000;
        });
        json(join(out, 'result.json'), result);
        writeFileSync(join(out, 'match.replay'), Buffer.from(`replay of ${record.verdict}`));
      }
    }
    process.exit(record.exit ?? 0);
    break;
  }
  default:
    process.stderr.write(`unknown command ${command}\n`);
    process.exit(2);
}
