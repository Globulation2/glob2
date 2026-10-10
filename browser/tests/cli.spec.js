const {test, expect} = require('@playwright/test');
const {openRuntimeHost} = require('./runtime-host');
const runtimePath = process.env.GLOB2_TEST_RUNTIME_PATH || '';

// Every launch is a fresh runtime: static commands must resolve Module.start
// without constructing a game/profile or starting graphics and networking.
for (const [name, args, expected] of [
  ['top-level help', ['--help'], 0],
  ['group help', ['map'], 0],
  ['JSON description', ['help', '--format', 'json'], 0],
  ['leaf help', ['map', 'generate', '--help'], 0],
  ['unavailable command help', ['assets', 'render-skin', '--help'], 0],
  ['completion', ['completion', 'fish'], 0],
  ['version', ['info', 'version', '--format', 'json'], 0],
  ['removed command', ['--run-game'], 2],
]) {
  test(`CLI static ${name}`, async ({page}) => {
    await openRuntimeHost(page, `<!doctype html><canvas id="canvas"></canvas><script>
      window.cliLines=[];
      var Module={noInitialRun:true, canvas:document.getElementById('canvas'),
        locateFile:name=>name.startsWith('assets/') ? '/' + name : ${JSON.stringify(runtimePath)} + '/' + name,
        print:line=>cliLines.push(String(line)), printErr:line=>cliLines.push(String(line)),
        preRun:[()=>{ ENV.GLOB2_USER_DATA_DIR='/tmp/cli-help-profile'; ENV.SDL_VIDEODRIVER='invalid'; }],
        onRuntimeInitialized:async()=>{
          try { const code=await Module.start(${JSON.stringify(args)});
            window.cliResult={code, text:cliLines.join('\\n'), profile:FS.analyzePath('/tmp/cli-help-profile').exists};
          } catch(error) { window.cliError=String(error); }
        }};
      </script><script src="${runtimePath}/index.js"></script>`);
    await expect.poll(()=>page.evaluate(()=>window.cliResult || window.cliError)).toBeTruthy();
    const result=await page.evaluate(()=>window.cliResult);
    expect(result).toBeTruthy(); expect(result.code).toBe(expected); expect(result.profile).toBe(false);
    if(name==='JSON description') {
      const tree=JSON.parse(result.text); expect(tree.cli_version).toBe(2); expect(tree.schema_version).toBe(1);
      expect(tree.commands.find(c=>c.path==='assets render-skin').available).toBe(false);
    }
  });
}
