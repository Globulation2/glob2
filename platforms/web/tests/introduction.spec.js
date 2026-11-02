const {test, expect} = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');
const {openRuntimeHost} = require('./runtime-host');

for (const variant of ['serial', 'threaded']) {
  test(`data-authored lessons complete and resume in WebAssembly (${variant})`, async ({page}, info) => {
    test.setTimeout(600000);
    await openRuntimeHost(page, `<!doctype html><canvas id="canvas"></canvas><script>
      window.introDone=false; window.introExit=null; window.introLog=[];
      var Module={noInitialRun:true,canvas:document.getElementById('canvas'),
        locateFile:name=>'/${variant==='threaded'?'threaded/':''}'+name,
        print:m=>introLog.push(String(m)), printErr:m=>introLog.push(String(m)),
        onExit:code=>window.introExit=code,
        preRun:[()=>{FS.mkdirTree('/evidence/profile');ENV.GLOB2_TEST_SOURCE_ROOT='/';
          ENV.GLOB2_USER_DATA_DIR='/evidence/profile';ENV.GLOB2_TEST_ARTIFACTS_ROOT='/evidence/cases';}],
        async onRuntimeInitialized(){try{
          const code=await Module.start(['--test-suite=Introduction','--reporters=junit','--out=/evidence/tests.xml']);
          if(window.introExit===null)window.introExit=code??0;
          window.introFiles={};
          function collect(dir){for(const name of FS.readdir(dir)){
            if(name==='.'||name==='..'||name==='profile')continue;
            const file=dir+'/'+name;
            if(FS.isDir(FS.stat(file).mode)){collect(file);continue;}
            window.introFiles[file.substring('/evidence/'.length)]=FS.readFile(file,{encoding:'utf8'});
          }}collect('/evidence');
        }catch(error){window.introError=String(error);}window.introDone=true;}}
    </script><script src="/${variant==='threaded'?'threaded/':''}script-tests.js"></script>`);
    await page.waitForFunction(() => window.introDone, null, {timeout:500000});
    const result=await page.evaluate(()=>({exit:window.introExit,error:window.introError,files:window.introFiles,log:window.introLog}));
    fs.mkdirSync(info.outputDir,{recursive:true});
    const evidence=path.resolve(__dirname,'../../../artifacts/introduction/browser',variant,info.project.name);
    fs.mkdirSync(evidence,{recursive:true});
    fs.writeFileSync(info.outputPath('introduction.log'),result.log.join('\n'));
    fs.writeFileSync(path.join(evidence,'introduction.log'),result.log.join('\n'));
    if(result.files?.['tests.xml']) fs.writeFileSync(path.join(evidence,'tests.xml'),result.files['tests.xml']);
    expect(result.error).toBeUndefined(); expect(result.exit).toBe(0);
    expect(result.files['tests.xml']).toContain('testcase');
    const traces=Object.entries(result.files).filter(([name])=>/\/(introduction|food-supply|prototype|expanded|legacy-campaign|water-access|sustainability|diplomacy|scripted-areas)\.trace$/.test(name));
    expect(traces).toHaveLength(9);
    const native=process.env.GLOB2_INTRODUCTION_NATIVE_TRACE;
    for(const [name,trace] of traces) {
      const filename=path.basename(name);
      fs.writeFileSync(info.outputPath(filename),trace);
      fs.writeFileSync(path.join(evidence,filename),trace);
      if(native) {
        const reference=filename==='introduction.trace' ? path.resolve(native) : path.join(path.dirname(path.resolve(native)),filename);
        expect(trace).toBe(fs.readFileSync(reference,'utf8'));
      }
    }
  });
}
