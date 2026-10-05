const {test,expect}=require('@playwright/test');
const {openRuntimeHost}=require('./runtime-host');
const fs=require('node:fs'),path=require('node:path');
test('final PNG case: both real WebAssembly decoders, one strict inventory result',async({page})=>{
 await openRuntimeHost(page,`<canvas id="canvas"></canvas><script>
 window.pngResult=null;
 var Module={noInitialRun:true,canvas:document.getElementById('canvas'),onAbort:r=>window.pngResult={error:String(r)},onRuntimeInitialized(){
 try{const code=Module.callMain(['--reporters=junit','--out=/tmp/png.xml']);window.pngResult={code:code??0,report:Module.FS.readFile('/tmp/png.xml',{encoding:'utf8'})};}catch(e){window.pngResult={error:String(e)};}}};
 </script><script src="/codec-browser.js"></script>`);
 await page.waitForFunction(()=>window.pngResult!==null);
 const result=await page.evaluate(()=>window.pngResult);
 expect(result.error).toBeUndefined();expect(result.code).toBe(0);
 expect((result.report.match(/<testcase\s/g)||[]).length).toBe(1);
 expect(result.report).toContain('name="16-bit RGBA rounds normalized channels to the exporter reference"');
 expect(result.report).toContain('failures="0"');
 fs.writeFileSync(path.resolve(__dirname,'../../artifacts/image-inventory/codec-browser.xml'),result.report);
});
