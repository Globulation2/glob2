const { test } = require("node:test");
const assert = require("node:assert/strict");
const vm = require("node:vm");
const fs = require("node:fs");
const path = require("node:path");
const { validLaunch } = require("../generator-studio.js");
const run = "11111111-1111-4111-8111-111111111111";
const launch = () => ({
  channel: "glob2-generator-studio",
  version: 1,
  type: "launch",
  runId: run,
  revision: 1,
  source: "{}",
  settings: {
    seed: 19,
    params: { width: 7, height: 7, teams: 4, workers: 4 },
    candidates: 1,
    startingUnitLevel: 0,
  },
});
test("validates bounded generator launch payloads", () => {
  assert.ok(validLaunch(launch(), run, 1));
  for (const patch of [
    { revision: 2 },
    { source: "\0" },
    { source: "é".repeat(140000) },
    { settings: { ...launch().settings, seed: -1 } },
    { settings: { ...launch().settings, candidates: 5 } },
    { settings: { ...launch().settings, params: { water: 1.5 } } },
    { settings: { ...launch().settings, params: { water: 2147483648 } } },
    { settings: { ...launch().settings, extra: true } },
  ])
    assert.ok(!validLaunch({ ...launch(), ...patch }, run, 1));
});
test("fences origins and revisions, freezes one launch, and accepts watch only after launch", async () => {
  let listener;
  const sent = [],
    parent = { postMessage: (m) => sent.push(m) },
    context = vm.createContext({
      location: {
        pathname: "/play/generator-studio.html",
        search: `?run=${run}&revision=1`,
        origin: "https://studio.test",
      },
      parent,
      URLSearchParams,
      TextEncoder,
      addEventListener: (_, f) => (listener = f),
    });
  for (const file of ["studio-envelope.js", "generator-studio.js"])
    vm.runInContext(
      fs.readFileSync(path.join(__dirname, "..", file), "utf8"),
      context,
    );
  const send = (data, origin = "https://studio.test", source = parent) =>
    listener({ data, origin, source });
  assert.equal(sent[0].type, "ready");
  send({ ...launch(), type: "watch" });
  assert.equal(context.glob2Studio.watch, false);
  send(launch(), "https://wrong.test");
  send(launch(), "https://studio.test", {});
  send({ ...launch(), revision: 2 });
  send(launch());
  send({ ...launch(), source: "changed" });
  assert.equal((await context.glob2Studio.launch).source, "{}");
  send({ ...launch(), type: "watch" });
  assert.equal(context.glob2Studio.watch, true);
});
