// Native references for the browser Studio tests, produced live from the same
// checkout instead of committed digests. Each scenario runs the native client
// that CI unpacks from the web-native artifact (or GLOB2_NATIVE_BINARY) with the
// same inputs the browser receives, so a simulation change moves both sides.
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const root = path.resolve(__dirname, "../../..");
const cache = new Map();

function nativeBinary() {
  return (
    process.env.GLOB2_NATIVE_BINARY ||
    path.join(root, "build/linux/client/release/src/glob2")
  );
}

// Runs one native command in a fresh profile; throws with its output on failure.
function runNative(args, { timeout = 600000 } = {}) {
  const binary = nativeBinary();
  if (!fs.existsSync(binary))
    throw new Error(
      `Native reference binary ${binary} is missing; build it or set GLOB2_NATIVE_BINARY`,
    );
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), "glob2-native-reference-"));
  try {
    const result = spawnSync(binary, args, {
      cwd: root,
      encoding: "utf8",
      timeout,
      maxBuffer: 64 * 1024 * 1024,
      env: {
        ...process.env,
        GLOB2_USER_DATA_DIR: profile,
        GLOB2_CHECKSUM_SIDECAR: "1",
        SDL_VIDEODRIVER: "dummy",
        SDL_AUDIODRIVER: "dummy",
      },
    });
    if (result.error || result.status !== 0)
      throw new Error(
        `Native ${args.slice(0, 2).join(" ")} failed (${result.error || "exit " + result.status}):\n` +
          `${result.stdout}\n${result.stderr}`.slice(-4000),
      );
    return result.stdout;
  } finally {
    fs.rmSync(profile, { recursive: true, force: true });
  }
}

function memo(key, produce) {
  if (!cache.has(key)) cache.set(key, produce());
  return cache.get(key);
}

function scratch(label) {
  const directory = path.join(root, "artifacts/native-reference", label);
  fs.rmSync(directory, { recursive: true, force: true });
  fs.mkdirSync(directory, { recursive: true });
  return directory;
}

// Options every traced run adds, browser or native: a checksum sidecar and saves.
function traceOptions(output) {
  return [
    "--write-replay",
    "--telemetry",
    "checksums",
    "--save",
    "initial",
    "--save",
    "final",
    "--output-dir",
    output,
  ];
}

// The checksum sidecar of a native `game run`, with its output directory.
function gameTrace(label, args) {
  return memo("trace:" + label, () => {
    const output = scratch(label);
    runNative(["game", "run", ...args, ...traceOptions(output)]);
    return {
      trace: fs.readFileSync(path.join(output, "game.replay.checksums")),
      directory: output,
    };
  });
}

// The AI Studio scenario: a JavaScript controller against a curated opponent.
function studioScenario() {
  const scenario = require("./fixtures/studio-scenario.json");
  return {
    ...scenario,
    sourceText: fs.readFileSync(path.join(root, scenario.source), "utf8"),
    mapBytes: fs.readFileSync(path.join(root, scenario.map)),
  };
}

function studioGameArgs(scenario, mapPath, aiPath) {
  return [
    "--map-file",
    mapPath,
    "--game-seed",
    String(scenario.seed),
    "--player",
    "javascript",
    "--ai-script",
    "0:" + aiPath,
    "--player",
    scenario.opponent,
    "--ticks",
    String(scenario.ticks),
  ];
}

function nativeStudioTrace() {
  const scenario = studioScenario();
  return gameTrace(
    "studio",
    studioGameArgs(
      scenario,
      path.join(root, scenario.map),
      path.join(root, scenario.source),
    ),
  ).trace;
}

// The generator Studio scenario: one Swamp module, frozen settings, AI colonies.
function generatorStudioScenario() {
  const scenario = require("./fixtures/generator-studio-scenario.json");
  const manifest = JSON.parse(
    fs.readFileSync(path.join(root, scenario.manifest), "utf8"),
  );
  const packageJson = JSON.stringify({
    formatVersion: 1,
    manifest: { ...manifest, entry: "generator.js" },
    modules: {
      "generator.js": fs.readFileSync(path.join(root, scenario.source), "utf8"),
    },
  });
  return {
    ...scenario,
    packageJson,
    settings: {
      seed: scenario.seed,
      params: scenario.params,
      candidates: scenario.candidates,
      startingUnitLevel: 0,
    },
  };
}

function generatorGameArgs(scenario, packagePath) {
  const args = [
    "--generator-package",
    packagePath,
    "--generator",
    scenario.generator,
    "--map-seed",
    String(scenario.seed),
  ];
  for (const [key, value] of Object.entries(scenario.params))
    args.push("--set", `${key}=${value}`);
  args.push("--candidates", String(scenario.candidates));
  args.push("--game-seed", String(scenario.seed));
  for (let team = 0; team < scenario.params.teams; ++team)
    args.push("--player", scenario.player);
  args.push("--ticks", String(scenario.ticks));
  return args;
}

// The generated game and the same game continued from its initial save.
function nativeGeneratorTraces() {
  const scenario = generatorStudioScenario();
  const directory = scratch("generator-package");
  const packagePath = path.join(directory, "generator.json");
  fs.writeFileSync(packagePath, scenario.packageJson);
  const generated = gameTrace(
    "generator-generated",
    generatorGameArgs(scenario, packagePath),
  );
  const loaded = gameTrace("generator-loaded", [
    "--load-game",
    path.join(generated.directory, "initial.game.gz"),
    "--ticks",
    String(scenario.ticks),
  ]);
  return { generated: generated.trace, loaded: loaded.trace };
}

function simVersion() {
  return memo("sim-version", () =>
    JSON.parse(runNative(["info", "sim-version", "--format", "json"])),
  );
}

// The canonical package hash, as `map inspect-package` reports it.
function packageHash(packageJson) {
  return memo("package:" + packageJson, () => {
    const directory = scratch("package");
    const input = path.join(directory, "generator.json");
    fs.writeFileSync(input, packageJson);
    runNative([
      "map",
      "inspect-package",
      input,
      "--output",
      path.join(directory, "canonical.json"),
      "--report-file",
      path.join(directory, "report.json"),
    ]);
    return JSON.parse(fs.readFileSync(path.join(directory, "report.json"), "utf8"))
      .packageHash;
  });
}

module.exports = {
  root,
  nativeBinary,
  traceOptions,
  studioScenario,
  studioGameArgs,
  nativeStudioTrace,
  generatorStudioScenario,
  generatorGameArgs,
  nativeGeneratorTraces,
  simVersion,
  packageHash,
};
