const {test, expect} = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

// Run the compiled engine with the same fixture and orders as the native CI
// lanes. A minimal host supplies CLI arguments without changing the game shell.
test('WebAssembly produces a complete per-tick simulation trace', async ({page}, info) => {
  const root = path.resolve(__dirname, '../..');
  const fixture = fs.readFileSync(path.join(root, 'games/cross-replay.game.gz'));
  await page.route('**/determinism.html', route => route.fulfill({
    contentType: 'text/html',
    body: `<!doctype html><canvas id="canvas"></canvas><script>
      window.engineLog = [];
      var Module = {
        noInitialRun: true,
        canvas: document.getElementById('canvas'),
        print: message => engineLog.push(String(message)),
        printErr: message => engineLog.push(String(message)),
        preRun: [function() {
          ENV.GLOB2_REPLAY_PATH = '/tmp/wasm.replay';
          ENV.GLOB2_CHECKSUM_SIDECAR = '1';
          FS.writeFile('/tmp/initial.game.gz', Uint8Array.from(atob('${fixture.toString('base64')}'), c => c.charCodeAt(0)));
        }],
        onRuntimeInitialized() {
          Module.callMain(['--nox', '/tmp/initial.game.gz', '1500', '1']);
          // Avoid millions of individually serialized Playwright values.
          // Chunk the conversion so large traces do not overflow the call stack.
          const bytes = FS.readFile('/tmp/wasm.replay.checksums');
          let binary = '';
          for (let offset = 0; offset < bytes.length; offset += 32768) {
            binary += String.fromCharCode(...bytes.subarray(offset, offset + 32768));
          }
          window.simulationTrace = btoa(binary);
        }
      };
    </script><script src="/index.js"></script>`,
  }));
  await page.goto('/determinism.html');
  await page.waitForFunction(() => typeof window.simulationTrace === 'string');
  const trace = Buffer.from(await page.evaluate(() => window.simulationTrace), 'base64');
  expect(trace.length).toBeGreaterThan(1000);
  fs.mkdirSync(info.outputDir, {recursive: true});
  fs.writeFileSync(info.outputPath('wasm.replay.checksums'), trace);
  const output = path.join(root, 'artifacts/browser-determinism/wasm');
  fs.mkdirSync(output, {recursive: true});
  fs.writeFileSync(path.join(output, 'wasm.replay.checksums'), trace);
  fs.writeFileSync(path.join(output, 'run.log'), (await page.evaluate(() => window.engineLog)).join('\n'));
  fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify({
    fixture: 'games/cross-replay.game.gz', seed: 42, ticks: 1500,
    fixture_sha256: crypto.createHash('sha256').update(fixture).digest('hex'),
    trace_sha256: crypto.createHash('sha256').update(trace).digest('hex'),
  }, null, 2) + '\n');
});

// The same production-runtime cases and realistic engine observations used by
// desktop and mobile harnesses. Golden comparisons occur inside C++, not in a
// JavaScript reimplementation of the engine or its math.
test('WebAssembly executes the shared scripting corpus', async ({page}, info) => {
  test.setTimeout(180000);
  const root = path.resolve(__dirname, '../..');
  await page.route('**/script-corpus.html', route => route.fulfill({contentType:'text/html', body:`
    <!doctype html><canvas id="canvas"></canvas><script>
    window.engineLog=[]; window.corpusExit=null;
    var Module={noInitialRun:true,canvas:document.getElementById('canvas'),
      print:m=>engineLog.push(String(m)), printErr:m=>engineLog.push(String(m)),
      onExit:code=>window.corpusExit=code,
      onAbort:reason=>{window.corpusError=String(reason);window.corpusDone=true;},
      preRun:[()=>{
        FS.mkdirTree('/evidence/profile');
        ENV.GLOB2_TEST_SOURCE_ROOT='/'; ENV.GLOB2_USER_DATA_DIR='/evidence/profile';
        ENV.GLOB2_TEST_ARTIFACTS_ROOT='/evidence/corpus';
      }],
      onRuntimeInitialized(){
        try { const result=Module.callMain(['--test-suite=JavaScript*','--reporters=junit','--out=/evidence/tests.xml']);
          if(window.corpusExit===null) window.corpusExit=result??0;
        } catch(error){window.corpusError=String(error);}
        const files={};
        function collect(directory){for(const name of FS.readdir(directory)){
          if(name==='.'||name==='..'||name==='profile')continue;
          const file=directory+'/'+name;
          if(FS.isDir(FS.stat(file).mode)){collect(file);continue;}
          const bytes=FS.readFile(file); let binary='';
          for(let offset=0;offset<bytes.length;offset+=32768)
            binary+=String.fromCharCode(...bytes.subarray(offset,offset+32768));
          files[file.substring('/evidence/'.length)]=btoa(binary);
        }}
        collect('/evidence');window.corpusFiles=files;window.corpusDone=true;
      }};
    </script><script src="/script-tests.js"></script>`}));
  await page.goto('/script-corpus.html');
  await page.waitForFunction(()=>window.corpusDone===true,null,{timeout:160000});
  const result=await page.evaluate(()=>({exit:window.corpusExit,error:window.corpusError,
    files:window.corpusFiles||{},log:window.engineLog}));
  const output=path.join(root,'artifacts/browser-determinism/script-corpus',info.project.name);
  fs.mkdirSync(output,{recursive:true});
  for(const [relative,base64] of Object.entries(result.files)){
    const destination=path.join(output,relative);
    fs.mkdirSync(path.dirname(destination),{recursive:true});
    fs.writeFileSync(destination,Buffer.from(base64,'base64'));
  }
  fs.writeFileSync(path.join(output,'run.log'),result.log.join('\n'));
  const revision=require('node:child_process').execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim();
  fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify({revision,browser:info.project.name,
    exit:result.exit,error:result.error,files:Object.keys(result.files)},null,2)+'\n');
  expect(result.error).toBeUndefined();
  expect(result.exit).toBe(0);
  expect(Object.keys(result.files).some(name=>name.endsWith('numeric-profile1.value'))).toBeTruthy();
  expect(Object.keys(result.files).some(name=>name.endsWith('realistic-economy-3.value'))).toBeTruthy();
});
