const {test, expect} = require('@playwright/test');
const {openRuntimeHost} = require('./runtime-host');

test('WebAssembly decodes exact WebP and normalized PNG pixels', async ({page}) => {
  test.setTimeout(180000);
  await openRuntimeHost(page, `
    <!doctype html><canvas id="canvas"></canvas><script>
    window.imageResult=null;
    var Module={noInitialRun:true,canvas:document.getElementById('canvas'),
      onAbort:reason=>window.imageResult={error:String(reason)},
      preRun:[()=>{
        FS.mkdirTree('/tmp/image-profile');
        ENV.GLOB2_USER_DATA_DIR='/tmp/image-profile';
        ENV.GLOB2_TEST_SOURCE_ROOT='/';
        ENV.GLOB2_TEST_ARTIFACTS_ROOT='/tmp/image-evidence';
      }],
      async onRuntimeInitialized(){
        try {
          const code=await Module.start(['--test-suite=ImageAssets','--reporters=junit','--out=/tmp/image-tests.xml']);
          window.imageResult={code:code??0,report:FS.readFile('/tmp/image-tests.xml',{encoding:'utf8'})};
        } catch(error){window.imageResult={error:String(error)};}
      }};
    </script><script src="/script-tests.js"></script>`);
  await page.waitForFunction(() => window.imageResult !== null);
  const result=await page.evaluate(() => window.imageResult);
  expect(result.error).toBeUndefined();
  expect(result.code).toBe(0);
  // Both WebP modes, normalized high-depth PNG, and strict artwork lookup.
  // Native PNG/JPEG saving is a separate native-only case.
  expect((result.report.match(/<testcase\s/g) || []).length).toBe(4);
  expect(result.report).toContain('name="Q90 lossy WebP preserves dimensions and exact alpha"');
  expect(result.report).toContain('name="16-bit RGBA rounds normalized channels to the exporter reference"');
  expect(result.report).toContain('failures="0"');
});
