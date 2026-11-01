const { test } = require("node:test");
const assert = require("node:assert/strict");
const { readFileSync } = require("node:fs");
const vm = require("node:vm");
const { webcrypto } = require("node:crypto");
const { validLaunch, MAP_HASH } = require("../studio.js");
const run = "37a04960-d807-40ce-b578-289c251fa161";
const map = readFileSync(
  require("node:path").join(
    __dirname,
    "../../platform/apps/engine-agent/fixtures/ais/two.map.gz",
  ),
);
const mapBuffer = () =>
  map.buffer.slice(map.byteOffset, map.byteOffset + map.byteLength);
const launch = () => ({
  channel: "glob2-ai-studio",
  version: 1,
  type: "launch",
  runId: run,
  revision: 1,
  source: "function step(){}",
  seed: 19,
  opponent: "numbi",
  map: mapBuffer(),
});
test("validates the pinned source, map, setup and revision envelope", () => {
  assert.ok(validLaunch(launch(), run, 1));
  for (const patch of [
    { revision: 2 },
    { version: 2 },
    { runId: "wrong" },
    { seed: -1 },
    { seed: 1.5 },
    { opponent: "javascript" },
    { source: "\0" },
    { source: "é".repeat(100000) },
    { map: new ArrayBuffer(0) },
  ])
    assert.ok(!validLaunch({ ...launch(), ...patch }, run, 1));
  assert.equal(
    require("node:crypto").createHash("sha256").update(map).digest("hex"),
    MAP_HASH,
  );
});
function host() {
  let listener;
  const sent = [],
    parent = { postMessage: (m) => sent.push(m) };
  const context = vm.createContext({
    location: {
      pathname: "/play/studio.html",
      search: `?run=${run}&revision=1`,
      origin: "https://studio.test",
    },
    parent,
    URLSearchParams,
    TextEncoder,
    ArrayBuffer,
    Uint8Array,
    crypto: webcrypto,
    addEventListener: (_t, fn) => (listener = fn),
  });
  vm.runInContext(readFileSync(require("node:path").join(__dirname,"../studio-envelope.js"),"utf8"),context);
  vm.runInContext(
    readFileSync(require("node:path").join(__dirname, "../studio.js"), "utf8"),
    context,
  );
  return {
    context,
    parent,
    sent,
    message: (origin, source, data) => listener({ origin, source, data }),
  };
}
test("ignores wrong origins, windows and stale runs; launches only once", async () => {
  const h = host();
  assert.equal(h.sent[0].type, "ready");
  await h.message("https://other.test", h.parent, launch());
  await h.message("https://studio.test", {}, launch());
  await h.message("https://studio.test", h.parent, {
    ...launch(),
    runId: "stale",
  });
  const good = launch();
  await h.message("https://studio.test", h.parent, good);
  assert.equal((await h.context.glob2Studio.launch).source, good.source);
  await h.message("https://studio.test", h.parent, {
    ...launch(),
    source: "second",
  });
  assert.equal((await h.context.glob2Studio.launch).source, good.source);
});
test("rejects a map that does not match the pinned fixture", async () => {
  const h = host();
  await h.message("https://studio.test", h.parent, {
    ...launch(),
    map: new ArrayBuffer(10),
  });
  await assert.rejects(h.context.glob2Studio.launch, /pinned fixture/);
  assert.equal(h.sent.at(-1).type, "error");
});
