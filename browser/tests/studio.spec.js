const { test, expect } = require("@playwright/test");
const fs = require("node:fs");
const path = require("node:path");
const { createHash } = require("node:crypto");
const { openRuntimeHost } = require("./runtime-host");
const fixture = require("./fixtures/studio-trace.json");
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const root = path.resolve(__dirname, "../..");
const source = fs.readFileSync(path.join(root, fixture.source), "utf8");
const map = fs.readFileSync(path.join(root, fixture.map)).toString("base64");
test("Studio launches an ephemeral AI-only game and reports live progress", async ({
  page,
}, info) => {
  test.setTimeout(180000);
  await openRuntimeHost(
    page,
    `<!doctype html><iframe id="game" width="1000" height="700"></iframe><script>
    window.messages=[];const runId='11111111-1111-4111-8111-111111111111';
    const frame=document.getElementById('game');
    addEventListener('message',event=>{if(event.source!==frame.contentWindow||event.origin!==location.origin)return;const m=event.data;messages.push(m);if(m.type==='ready'){
      const map=Uint8Array.from(atob('${map}'),c=>c.charCodeAt(0)).buffer;
      frame.contentWindow.postMessage({channel:'glob2-ai-studio',version:1,type:'launch',runId:m.runId,revision:m.revision,source:${JSON.stringify(source)},seed:19,opponent:'numbi',map},location.origin,[map]);
    }});
    frame.src='/studio.html?run='+runId+'&revision=1';
    </script>`,
  );
  await page.waitForFunction(
    () =>
      messages.some((m) => m.type === "progress" && m.tick >= 128) ||
      messages.some((m) => m.type === "error"),
    null,
    { timeout: 150000 },
  );
  const messages = await page.evaluate(() => messages);
  expect(messages.filter((m) => m.type === "error")).toEqual([]);
  expect(messages.some((m) => m.type === "progress" && m.tick >= 128)).toBe(
    true,
  );
  const frame = page.frames().find((f) => f.url().includes("/studio.html"));
  expect(await frame.evaluate(() => !!Module.saveMount)).toBe(false);
  await page.screenshot({ path: info.outputPath("studio-live.png") });
  fs.mkdirSync(path.join(root, "artifacts/ai-studio"), { recursive: true });
  fs.writeFileSync(
    path.join(root, "artifacts/ai-studio/live-" + info.project.name + ".json"),
    JSON.stringify(messages, null, 2),
  );
  if (info.project.name === "chromium") {
    await frame.locator("#canvas").press("p");
    const beforePause = await frame.evaluate(
      () => glob2Diagnostics.snapshot().frames,
    );
    await expect
      .poll(() => frame.evaluate(() => glob2Diagnostics.snapshot().frames))
      .toBeGreaterThan(beforePause + 5);
    const paused = await frame.evaluate(() => glob2Diagnostics.snapshot());
    const tick = paused.tick;
    await expect
      .poll(() => frame.evaluate(() => glob2Diagnostics.snapshot().frames))
      .toBeGreaterThan(paused.frames + 5);
    expect(await frame.evaluate(() => glob2Diagnostics.snapshot().tick)).toBe(
      tick,
    );
    await page.locator("iframe").evaluate((f) => {
      f.width = "560";
      f.height = "420";
    });
    await expect.poll(() => frame.evaluate(() => innerWidth)).toBe(560);
    expect(await frame.evaluate(() => glob2Diagnostics.snapshot().tick)).toBe(
      tick,
    );
    await frame.locator("#canvas").press("p");
    await frame.locator("#canvas").press("Control+=");
    await expect
      .poll(() => frame.evaluate(() => glob2Diagnostics.snapshot().tick))
      .toBeGreaterThan(tick);
    await page.evaluate(() =>
      document.getElementById("game").contentWindow.postMessage(
        {
          channel: "glob2-ai-studio",
          version: 1,
          type: "stop",
          runId: "11111111-1111-4111-8111-111111111111",
          revision: 1,
        },
        location.origin,
      ),
    );
    await expect.poll(() => frame.url()).toBe("about:blank");
    await page.locator("iframe").evaluate((f) => {
      f.src =
        "/studio.html?run=22222222-2222-4222-8222-222222222222&revision=1";
    });
    await page.waitForFunction(() =>
      messages.some(
        (m) =>
          m.runId === "22222222-2222-4222-8222-222222222222" &&
          m.type === "progress" &&
          m.tick >= 32,
      ),
    );
  }
  await page.evaluate(() => document.getElementById("game").remove());
  await expect(page.locator("iframe")).toHaveCount(0);
});
for (const variant of ["serial", "threaded"])
  test(`Studio source produces deterministic trace (${variant})`, async ({
    page,
  }, info) => {
    test.setTimeout(180000);
    // Fail visibly when setup changes; never bless a new browser result implicitly.
    expect(sha256(source)).toBe(fixture.sourceSha256);
    expect(sha256(Buffer.from(map, "base64"))).toBe(fixture.mapSha256);
    await openRuntimeHost(
      page,
      `<!doctype html><canvas id="canvas"></canvas><script>
    window.engineLog=[];var Module={noInitialRun:true,canvas:document.getElementById('canvas'),
      locateFile:name=>name.endsWith('.data')?'/'+name:'/${variant === "threaded" ? "threaded/" : ""}'+name,
      print:text=>engineLog.push(String(text)),printErr:text=>engineLog.push(String(text)),
      preRun:[function(){ENV.GLOB2_CHECKSUM_SIDECAR='1';FS.writeFile('/tmp/studio.map.gz',Uint8Array.from(atob('${map}'),c=>c.charCodeAt(0)));FS.writeFile('/tmp/ai.js',${JSON.stringify(source)});}],
      async onRuntimeInitialized(){try{
        const code=await Module.start(['--run-game','--map-file','/tmp/studio.map.gz','--game-seed','${fixture.seed}','--player','javascript','--ai-script','0:/tmp/ai.js','--player','${fixture.opponent}','--ticks','${fixture.ticks}','--replay','true','--telemetry','checksums','--save','initial','--save','final','--output-dir','/tmp/result']);
        if(code!==0)throw Error('Engine exited '+code);
        const bytes=FS.readFile('/tmp/result/game.replay.checksums');let binary='';for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));window.trace=btoa(binary);
      }catch(e){window.failure=String(e);}}
    };</script><script src="/${variant === "threaded" ? "threaded/" : ""}index.js"></script>`,
    );
    await page.waitForFunction(() => window.trace || window.failure, null, {
      timeout: 150000,
    });
    expect(await page.evaluate(() => window.failure)).toBeUndefined();
    const directory = path.join(
      root,
      "artifacts/ai-studio",
      `${info.project.name}-${variant}`,
    );
    fs.mkdirSync(directory, { recursive: true });
    const trace = Buffer.from(await page.evaluate(() => window.trace), "base64");
    fs.writeFileSync(path.join(directory, "game.replay.checksums"), trace);
    // The reference was produced by native execution, then matched by both browser
    // runtimes. Checking all bytes covers every recorded per-tick checksum.
    expect(trace.length).toBe(fixture.traceBytes);
    expect(sha256(trace)).toBe(fixture.traceSha256);
    fs.writeFileSync(
      path.join(directory, "run.log"),
      (await page.evaluate(() => engineLog)).join("\n"),
    );
  });

test("Studio returns bounded runtime diagnostics for a failed controller", async ({
  page,
}) => {
  test.setTimeout(180000);
  const broken = source.replace(
    "decisions++;",
    'throw new Error("Studio runtime fixture error");',
  );
  await openRuntimeHost(
    page,
    `<!doctype html><iframe id="game" width="900" height="650"></iframe><script>
    window.messages=[];const frame=document.getElementById('game');
    addEventListener('message', event=>{if(event.source!==frame.contentWindow||event.origin!==location.origin)return;const m=event.data;messages.push(m);if(m.type==='ready'){
      const map=Uint8Array.from(atob('${map}'),c=>c.charCodeAt(0)).buffer;
      frame.contentWindow.postMessage({channel:'glob2-ai-studio',version:1,type:'launch',runId:m.runId,revision:7,source:${JSON.stringify(broken)},seed:19,opponent:'numbi',map},location.origin,[map]);
    }});
    frame.src='/studio.html?run=33333333-3333-4333-8333-333333333333&revision=7';
  </script>`,
  );
  await page.waitForFunction(
    () => messages.some((m) => m.type === "complete" || m.type === "error"),
    null,
    { timeout: 150000 },
  );
  const result = await page.evaluate(() =>
    messages.find((m) => m.type === "complete" || m.type === "error"),
  );
  expect(result.type).toBe("complete");
  expect(result.revision).toBe(7);
  expect(result.disabled).toBe(true);
  expect(result.diagnostic).toContain("Studio runtime fixture error");
  expect(result.diagnostic.length).toBeLessThanOrEqual(2000);
  await page.locator("iframe").evaluate((frame) => frame.remove());
});
