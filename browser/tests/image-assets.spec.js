const {test, expect} = require('@playwright/test');

test('WebAssembly decodes exact WebP pixels and preserves image overrides', async ({page}) => {
  test.setTimeout(180000);
  await page.route('**/image-assets.html', route => route.fulfill({contentType:'text/html', body:`
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
      onRuntimeInitialized(){
        try {
          const code=Module.callMain(['--test-suite=ImageAssets','--reporters=junit','--out=/tmp/image-tests.xml']);
          window.imageResult={code:code??0,report:FS.readFile('/tmp/image-tests.xml',{encoding:'utf8'})};
        } catch(error){window.imageResult={error:String(error)};}
      }};
    </script><script src="/script-tests.js"></script>`}));
  await page.goto('/image-assets.html');
  await page.waitForFunction(() => window.imageResult !== null);
  const result=await page.evaluate(() => window.imageResult);
  expect(result.error).toBeUndefined();
  expect(result.code).toBe(0);
  expect((result.report.match(/<testcase\s/g) || []).length).toBe(2);
  expect(result.report).toContain('failures="0"');
});
