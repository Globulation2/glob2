const { test, expect } = require("@playwright/test");
const fs = require("node:fs");
const path = require("node:path");
const { openRuntimeHost } = require("./runtime-host");
const traceFixture = require("./fixtures/generator-studio-trace.json");
const root = path.resolve(__dirname, "../..");
const manifest = JSON.parse(
  fs.readFileSync(
    path.join(root, "data/generators/examples/swamp/manifest.json"),
    "utf8",
  ),
);
const source = JSON.stringify({
  formatVersion: 1,
  manifest: { ...manifest, entry: "generator.js" },
  modules: {
    "generator.js": fs.readFileSync(
      path.join(root, "data/generators/examples/swamp/generator.js"),
      "utf8",
    ),
  },
});
const settings = {
  seed: 19,
  params: { width: 7, height: 7, teams: 4, workers: 4 },
  candidates: 1,
  startingUnitLevel: 0,
};
for (const variant of ["serial", "threaded"])
  test(`generator Studio freezes a world and watches its colonies (${variant})`, async ({
    page,
  }, info) => {
    test.setTimeout(180000);
    await openRuntimeHost(
      page,
      `<!doctype html><iframe id="game" width="1000" height="700"></iframe><script>
  window.messages=[]; const frame=document.getElementById('game');
  addEventListener('message',e=>{if(e.source!==frame.contentWindow||e.origin!==location.origin)return;
    const m=e.data; messages.push(m); if(m.type==='ready')frame.contentWindow.postMessage({channel:'glob2-generator-studio',version:1,type:'launch',runId:m.runId,revision:m.revision,source:${JSON.stringify(source)},settings:${JSON.stringify(settings)}},location.origin);
  });
  frame.src='/generator-studio.html?run=11111111-1111-4111-8111-111111111111&revision=1&threads=${variant}';
  </script>`,
    );
    await page.waitForFunction(
      () => messages.some((m) => m.type === "generated" || m.type === "error"),
      null,
      { timeout: 150000 },
    );
    const report = await page.evaluate(() =>
      messages.find((m) => m.type === "generated" || m.type === "error"),
    );
    expect(report.type, JSON.stringify(report)).toBe("generated");
    expect(report.success, JSON.stringify(report)).toBe(true);
    expect(report.playable).toBe(true);
    expect(report.seed).toBe(19);
    const directory = path.join(
      root,
      "artifacts/generator-studio",
      info.project.name + "-" + variant,
    );
    fs.mkdirSync(directory, { recursive: true });
    fs.writeFileSync(
      path.join(directory, "report.json"),
      JSON.stringify(report, null, 2),
    );
    // Native-derived references remain mandatory even without local evidence files.
    expect(report.packageHash).toBe(traceFixture.packageHash);
    expect(report.versionMinor).toBe(traceFixture.versionMinor);
    expect(report.simRevision).toBe(traceFixture.simRevision);
    expect(report.simVersion).toBe(traceFixture.simVersion);
    expect(report.checksum).toBe(traceFixture.checksum);
    expect(report.worldFingerprint).toBe(traceFixture.worldFingerprint);
    const frame = page
      .frames()
      .find((f) => f.url().includes("/generator-studio.html"));
    const runtime = await frame.evaluate(() => ({
      mode: Module.executionMode,
      fallback: Module.threadFallback || null,
    }));
    fs.writeFileSync(
      path.join(directory, "runtime.json"),
      JSON.stringify(runtime, null, 2),
    );
    test.skip(
      runtime.mode !== variant,
      `Interactive ${variant} runtime unavailable: ${runtime.fallback}`,
    );
    expect(await frame.evaluate(() => !!Module.saveMount)).toBe(false);
    const loop = await frame.evaluate(() => glob2Diagnostics.snapshot().loop);
    await expect
      .poll(() => frame.evaluate(() => glob2Diagnostics.snapshot().loop))
      .toBeGreaterThan(loop + 2);
    await page.screenshot({ path: path.join(directory, "preview.png") });
    await page.evaluate(() =>
      document
        .getElementById("game")
        .contentWindow.postMessage(
          {
            channel: "glob2-generator-studio",
            version: 1,
            type: "watch",
            runId: "11111111-1111-4111-8111-111111111111",
            revision: 1,
          },
          location.origin,
        ),
    );
    await page.waitForFunction(
      () =>
        messages.some((m) => m.type === "progress" && m.tick >= 64) ||
        messages.some((m) => m.type === "error"),
      null,
      { timeout: 90000 },
    );
    const messages = await page.evaluate(() => window.messages);
    expect(messages.filter((m) => m.type === "error")).toEqual([]);
    expect(messages.some((m) => m.type === "progress" && m.tick >= 64)).toBe(
      true,
    );
    await frame.locator("#canvas").press("p", { delay: 80 });
    const frames = await frame.evaluate(
      () => glob2Diagnostics.snapshot().frames,
    );
    await expect
      .poll(() => frame.evaluate(() => glob2Diagnostics.snapshot().frames))
      .toBeGreaterThan(frames + 5);
    const tick = await frame.evaluate(() => glob2Diagnostics.snapshot().tick);
    const pausedFrames = await frame.evaluate(
      () => glob2Diagnostics.snapshot().frames,
    );
    await expect
      .poll(() => frame.evaluate(() => glob2Diagnostics.snapshot().frames))
      .toBeGreaterThan(pausedFrames + 5);
    expect(await frame.evaluate(() => glob2Diagnostics.snapshot().tick)).toBe(
      tick,
    );
    await page.evaluate(() => document.getElementById("game").remove());
    await expect(page.locator("iframe")).toHaveCount(0);
  });
const { createHash } = require("node:crypto");
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
async function captureGame(page, variant, args, input, save = false) {
  await openRuntimeHost(
    page,
    `<!doctype html><canvas id="canvas"></canvas><script>
  window.engineLog=[];var Module={noInitialRun:true,canvas:document.getElementById('canvas'),
    locateFile:name=>name.endsWith('.data')?'/'+name:'/${variant === "threaded" ? "threaded/" : ""}'+name,
    print:text=>engineLog.push(String(text)),printErr:text=>engineLog.push(String(text)),
    preRun:[function(){ENV.GLOB2_CHECKSUM_SIDECAR='1';${input}}],
    async onRuntimeInitialized(){try{
      const code=await Module.start(${JSON.stringify(args)});
      if(code!==0)throw Error('Engine exited '+code);
      function base64(path){const bytes=FS.readFile(path);let binary='';for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));return btoa(binary);}
      window.result={trace:base64('/tmp/result/game.replay.checksums'),${save ? "save:base64('/tmp/result/initial.game.gz')" : ""}};
    }catch(e){window.failure=String(e);}}
  };</script><script src="/${variant === "threaded" ? "threaded/" : ""}index.js"></script>`,
  );
  await page.waitForFunction(() => window.result || window.failure, null, {
    timeout: 150000,
  });
  expect(
    await page.evaluate(() => window.failure),
    (await page.evaluate(() => engineLog)).join("\n"),
  ).toBeUndefined();
  return page.evaluate(() => window.result);
}
for (const variant of ["serial", "threaded"])
  test(`frozen generator game and saved continuation match every native checksum (${variant})`, async ({
    page,
  }, info) => {
    test.setTimeout(360000);
    expect(sha256(fs.readFileSync(path.join(root, traceFixture.source)))).toBe(
      traceFixture.sourceSha256,
    );
    const options = [
      "--ticks",
      String(traceFixture.ticks),
      "--write-replay",
      "--telemetry",
      "checksums",
      "--save",
      "initial",
      "--save",
      "final",
      "--output-dir",
      "/tmp/result",
    ];
    const generated = await captureGame(
      page,
      variant,
      [
        "game",
        "run",
        "--generator-package",
        "/tmp/generator.json",
        "--generator",
        "examples:swamp",
        "--map-seed",
        "19",
        "--set",
        "width=7",
        "--set",
        "height=7",
        "--set",
        "teams=4",
        "--set",
        "workers=4",
        "--candidates",
        "1",
        "--game-seed",
        "19",
        "--player",
        "nicowar",
        "--player",
        "nicowar",
        "--player",
        "nicowar",
        "--player",
        "nicowar",
        ...options,
      ],
      `FS.writeFile('/tmp/generator.json',${JSON.stringify(source)});`,
      true,
    );
    const loaded = await captureGame(
      page,
      variant,
      ["game", "run", "--load-game", "/tmp/initial.game.gz", ...options],
      `FS.writeFile('/tmp/initial.game.gz',Uint8Array.from(atob(${JSON.stringify(generated.save)}),c=>c.charCodeAt(0)));`,
    );
    const directory = path.join(
      root,
      "artifacts/generator-studio",
      info.project.name + "-" + variant,
    );
    fs.mkdirSync(directory, { recursive: true });
    for (const [label, result] of [
      ["generated", generated],
      ["loaded", loaded],
    ]) {
      const trace = Buffer.from(result.trace, "base64");
      fs.writeFileSync(path.join(directory, label + ".checksums"), trace);
      expect(trace.length).toBe(traceFixture.traceBytes);
      expect(sha256(trace)).toBe(traceFixture.traceSha256);
    }
  });
