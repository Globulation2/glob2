const {test, expect} = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {openRuntimeHost} = require('./runtime-host');

// Run the compiled engine with the same fixture and orders as the native CI
// lanes. A minimal host supplies CLI arguments without changing the game shell.
for (const [variant, threads] of [['serial',1], ['threaded',1], ['threaded',2], ['threaded',4]]) {
test(`WebAssembly produces a complete per-tick simulation trace (${variant}/${threads})`, async ({page}, info) => {
  const root = path.resolve(__dirname, '../..');
  const fixture = fs.readFileSync(path.join(root, 'games/cross-replay.game.gz'));
  await openRuntimeHost(page, `<!doctype html><canvas id="canvas"></canvas><script>
      window.engineLog = [];
      var Module = {
        noInitialRun: true,
        canvas: document.getElementById('canvas'),
        locateFile:name=>name.endsWith('.data') ? '/' + name : '/${variant === 'threaded' ? 'threaded/' : ''}' + name,
        print: message => engineLog.push(String(message)),
        printErr: message => engineLog.push(String(message)),
        preRun: [function() {
          ENV.GLOB2_REPLAY_PATH = '/tmp/wasm.replay';
          ENV.GLOB2_CHECKSUM_SIDECAR = '1';
          FS.writeFile('/tmp/initial.game.gz', Uint8Array.from(atob('${fixture.toString('base64')}'), c => c.charCodeAt(0)));
        }],
        async onRuntimeInitialized() {
          const code = await Module.start(['--nox', '/tmp/initial.game.gz', '1500', '1', '--ai-threads', '${threads}']);
          if (code !== 0) throw new Error('Engine exited: ' + code);
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
    </script><script src="/${variant === 'threaded' ? 'threaded/' : ''}index.js"></script>`);
  await page.waitForFunction(() => typeof window.simulationTrace === 'string');
  const trace = Buffer.from(await page.evaluate(() => window.simulationTrace), 'base64');
  expect(trace.length).toBeGreaterThan(1000);
  fs.mkdirSync(info.outputDir, {recursive: true});
  fs.writeFileSync(info.outputPath('wasm.replay.checksums'), trace);
  const output = path.join(root, 'artifacts/browser-determinism/wasm', variant + '-' + threads);
  fs.mkdirSync(output, {recursive: true});
  fs.writeFileSync(path.join(output, 'wasm.replay.checksums'), trace);
  fs.writeFileSync(path.join(root, 'artifacts/browser-determinism/wasm/wasm.replay.checksums'), trace);

  fs.writeFileSync(path.join(output, 'run.log'), (await page.evaluate(() => window.engineLog)).join('\n'));
  fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify({
    fixture: 'games/cross-replay.game.gz', seed: 42, ticks: 1500, variant, threads,
    fixture_sha256: crypto.createHash('sha256').update(fixture).digest('hex'),
    trace_sha256: crypto.createHash('sha256').update(trace).digest('hex'),
  }, null, 2) + '\n');
});
}

// The committed turn-protocol match record, verified by the WebAssembly engine
// exactly as the native lanes verify it (test/run-browser-determinism.py). The
// comparison job requires every platform's per-tick trace to be identical.
test('WebAssembly verifies the committed match record', async ({page}, info) => {
  test.setTimeout(300000);
  const root = path.resolve(__dirname, '../..');
  const record = fs.readFileSync(path.join(root, 'test/fixtures/multiplayer/FourSquares1.g2mr'));
  const map = fs.readFileSync(path.join(root, 'maps/FourSquares1.map.gz'));
  await page.route('**/verify-match.html', route => route.fulfill({
    contentType: 'text/html',
    body: `<!doctype html><canvas id="canvas"></canvas><script>
      window.engineLog = [];
      var Module = {
        noInitialRun: true,
        canvas: document.getElementById('canvas'),
        print: message => engineLog.push(String(message)),
        printErr: message => engineLog.push(String(message)),
        preRun: [function() {
          FS.writeFile('/tmp/match.g2mr', Uint8Array.from(atob('${record.toString('base64')}'), c => c.charCodeAt(0)));
          FS.writeFile('/tmp/FourSquares1.map.gz', Uint8Array.from(atob('${map.toString('base64')}'), c => c.charCodeAt(0)));
        }],
        onRuntimeInitialized() {
          window.verifyExit = Module.callMain(['--verify-match', '/tmp/match.g2mr', '--map', '/tmp/FourSquares1.map.gz', '--out', '/tmp/verify']);
          window.verifyTrace = FS.readFile('/tmp/verify/checksums.txt', {encoding: 'utf8'});
          window.verifyVerdict = FS.readFile('/tmp/verify/verdict.json', {encoding: 'utf8'});
        }
      };
    </script><script src="/index.js"></script>`,
  }));
  await page.goto('/verify-match.html');
  await page.waitForFunction(() => typeof window.verifyTrace === 'string', null, {timeout: 280000});
  const trace = await page.evaluate(() => window.verifyTrace);
  const verdict = JSON.parse(await page.evaluate(() => window.verifyVerdict));
  expect(trace.split('\n').length).toBeGreaterThan(600);
  expect(verdict.verdict).toBe('verified');
  expect(trace).toBe(fs.readFileSync(path.join(root, 'test/fixtures/multiplayer/FourSquares1.verify-trace.txt'), 'utf8').replace(/\r\n/g, '\n'));
  const output = path.join(root, 'artifacts/browser-determinism/wasm');
  fs.mkdirSync(output, {recursive: true});
  fs.writeFileSync(path.join(output, 'verify-match.checksums.txt'), trace);
  fs.writeFileSync(path.join(output, 'verify-match.verdict.json'), JSON.stringify(verdict) + '\n');
  fs.writeFileSync(path.join(output, 'verify-match.log'), (await page.evaluate(() => window.engineLog)).join('\n'));
  fs.mkdirSync(info.outputDir, {recursive: true});
  fs.writeFileSync(info.outputPath('verify-match.checksums.txt'), trace);
});

// The same production-runtime cases and realistic engine observations used by
// desktop and mobile harnesses. Golden comparisons occur inside C++, not in a
// JavaScript reimplementation of the engine or its math.
for (const variant of ['serial','threaded']) {
test(`WebAssembly executes the shared scripting corpus (${variant})`, async ({page}, info) => {
  test.setTimeout(600000);
  const root = path.resolve(__dirname, '../..');
  const output=path.join(root,'artifacts/browser-determinism',variant==='serial'?'script-corpus':'script-corpus-threaded',info.project.name);
  // Remove only this case's previous exports, so a new successful manifest
  // cannot accidentally certify artifacts left by an earlier execution.
  fs.rmSync(output,{recursive:true,force:true});
  fs.mkdirSync(output,{recursive:true});
  const progress=[];
  page.on('console',message=>{progress.push(message.text());
    fs.appendFileSync(path.join(output,'progress.log'),message.text()+'\n');});
  page.on('pageerror',error=>{progress.push(String(error));
    fs.appendFileSync(path.join(output,'progress.log'),String(error)+'\n');});
  await openRuntimeHost(page, `
    <!doctype html><canvas id="canvas"></canvas><script>
    window.engineLog=[]; window.corpusExit=null;
    var Module={noInitialRun:true,canvas:document.getElementById('canvas'),
      locateFile:name=>name.endsWith('.data')?'/'+name:'/${variant==='threaded'?'threaded/':''}'+name,
      print:m=>{engineLog.push(String(m));console.log(String(m));},
      printErr:m=>{engineLog.push(String(m));console.error(String(m));},
      onExit:code=>window.corpusExit=code,
      onAbort:reason=>{window.corpusError=String(reason);window.corpusDone=true;},
      preRun:[()=>{
        FS.mkdirTree('/evidence/profile');
        ENV.GLOB2_TEST_SOURCE_ROOT='/'; ENV.GLOB2_USER_DATA_DIR='/evidence/profile';
        ENV.GLOB2_TEST_ARTIFACTS_ROOT='/evidence/corpus';
      }],
      async onRuntimeInitialized(){
        try { const result=await Module.start(['--test-suite=JavaScript*,ImageAssets','--reporters=junit','--out=/evidence/tests.xml']);
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
    </script><script src="/${variant==='threaded'?'threaded/':''}script-tests.js"></script>`);
  let waitError;
  try {await page.waitForFunction(()=>window.corpusDone===true,null,{timeout:550000});}
  catch(error){waitError=error;}
  // Retain partial logs and any exported data before reporting a timeout.
  // An unresponsive page must not prevent the host from retaining its evidence.
  let collectionTimer;
  const result=await Promise.race([
    page.evaluate(()=>({exit:window.corpusExit,error:window.corpusError,
      files:window.corpusFiles||{},log:window.engineLog})).catch(error=>
        ({exit:null,error:String(error),files:{},log:progress})),
    new Promise(resolve=>{collectionTimer=setTimeout(()=>resolve({exit:null,
      error:'Timed out collecting page evidence',files:{},log:progress}),waitError?5000:60000);})]);
  clearTimeout(collectionTimer);
  for(const [relative,base64] of Object.entries(result.files)){
    const destination=path.join(output,relative);
    fs.mkdirSync(path.dirname(destination),{recursive:true});
    fs.writeFileSync(destination,Buffer.from(base64,'base64'));
  }
  fs.writeFileSync(path.join(output,'run.log'),result.log.join('\n'));
  const source=JSON.parse(require('node:child_process').execFileSync('python3',
    [path.join(root,'test/build_provenance.py')],{cwd:root,encoding:'utf8'}));
  const hash=file=>require('node:crypto').createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const packageRoot=path.join(root,'build/emscripten/client/release');
  const build=variant==='threaded'?path.join(packageRoot,'threaded'):packageRoot;
  const fixtures={};
  for(const file of fs.readdirSync(path.join(root,'test/fixtures/javascript'))){
    const target=path.join(root,'test/fixtures/javascript',file);
    if(fs.statSync(target).isFile())fixtures[file]=hash(target);
  }
  let producer, provenanceError;
  try {producer=JSON.parse(fs.readFileSync(path.join(output,'corpus/build-provenance.json'),'utf8'));}
  catch(error){provenanceError=String(error);}
  const provenanceIssues=provenanceError ? ['Missing or invalid executed-binary build provenance: '+provenanceError]
    : ['revision','dirty','sourceTreeSha256'].filter(key=>producer[key]!==source[key])
      .map(key=>'Executed binary differs from runner source: '+key);
  fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify({...source,build:producer||null,provenanceIssues,
    browser:info.project.name,browserVersion:page.context().browser().version(),
    toolchain:JSON.parse(fs.readFileSync(path.join(root,'browser/toolchain.json'))),
    buildIdentity:JSON.parse(fs.readFileSync(path.join(build,'identity.json'))),
    binaries:Object.fromEntries(['script-tests.js','script-tests.wasm','script-tests.data']
      .map(file=>[file,hash(path.join(file.endsWith('.data')?packageRoot:build,file))])),fixtureHashes:fixtures,
    command:'playwright test determinism.spec.js --grep "shared scripting corpus"',
    exit:result.exit,error:result.error,files:Object.keys(result.files)},null,2)+'\n');
  if(waitError)throw waitError;
  expect(provenanceIssues).toEqual([]);
  expect(result.error).toBeUndefined();
  expect(result.exit).toBe(0);
  expect(Object.keys(result.files).some(name=>name.endsWith('numeric-profile1.value'))).toBeTruthy();
  expect(Object.keys(result.files).some(name=>name.endsWith('realistic-economy-3.value'))).toBeTruthy();
});

}
