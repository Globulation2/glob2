const { test, expect } = require("@playwright/test");
const fs = require("node:fs");
const path = require("node:path");
const { openRuntimeHost } = require("./runtime-host");
const {
  root,
  generatorStudioScenario,
  generatorGameArgs,
  traceOptions,
  nativeGeneratorTraces,
  simVersion,
  packageHash,
} = require("./native-reference");
const scenario = generatorStudioScenario();
const source = scenario.packageJson;
const settings = scenario.settings;
// Frozen preview identities by runtime variant, compared within one worker.
const previews = new Map();
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
    expect(report.seed).toBe(scenario.seed);
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
    // Identities come from the native client of the same checkout, never a
    // committed digest. The frozen world itself has no native CLI equivalent:
    // both runtimes must freeze the same one, and the traced game below proves
    // the generated world against native.
    const native = simVersion();
    expect(report.packageHash).toBe(packageHash(source));
    expect(report.versionMinor).toBe(native.versionMinor);
    expect(report.simVersion).toBe(
      `${native.versionMinor}-${native.netProtocol}-${native.dataHash}`,
    );
    expect(Number.isInteger(report.simRevision)).toBe(true);
    expect(Number.isInteger(report.checksum)).toBe(true);
    expect(report.worldFingerprint).toMatch(/^[0-9a-f]{64}$/);
    const preview = {
      checksum: report.checksum,
      worldFingerprint: report.worldFingerprint,
    };
    for (const [other, frozen] of previews)
      expect(preview, `frozen world differs from the ${other} runtime`).toEqual(
        frozen,
      );
    previews.set(variant, preview);
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
    // The native reference runs first in this worker (a few minutes on a debug build).
    test.setTimeout(600000);
    const options = traceOptions("/tmp/result");
    const generated = await captureGame(
      page,
      variant,
      [
        "game",
        "run",
        ...generatorGameArgs(scenario, "/tmp/generator.json"),
        ...options,
      ],
      `FS.writeFile('/tmp/generator.json',${JSON.stringify(source)});`,
      true,
    );
    const loaded = await captureGame(
      page,
      variant,
      [
        "game",
        "run",
        "--load-game",
        "/tmp/initial.game.gz",
        "--ticks",
        String(scenario.ticks),
        ...options,
      ],
      `FS.writeFile('/tmp/initial.game.gz',Uint8Array.from(atob(${JSON.stringify(generated.save)}),c=>c.charCodeAt(0)));`,
    );
    const directory = path.join(
      root,
      "artifacts/generator-studio",
      info.project.name + "-" + variant,
    );
    fs.mkdirSync(directory, { recursive: true });
    const native = nativeGeneratorTraces();
    for (const [label, result] of [
      ["generated", generated],
      ["loaded", loaded],
    ]) {
      const trace = Buffer.from(result.trace, "base64");
      fs.writeFileSync(path.join(directory, label + ".checksums"), trace);
      expect(trace.length, label).toBe(native[label].length);
      expect(sha256(trace), label).toBe(sha256(native[label]));
    }
  });
