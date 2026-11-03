const {test,expect}=require('@playwright/test');
const {waitForMainMenu}=require('./main-menu');

test('graphics diagnostics sample painting without blocking logical update turns',async({page})=>{
  await page.addInitScript(()=>{
    const queries=globalThis.graphicsQueryProbe={paint:0,update:0,inject:false,injected:0};
    const raf=requestAnimationFrame.bind(globalThis);
    globalThis.requestAnimationFrame=callback=>raf(time=>{
      globalThis.graphicsProbePainting=true;
      try {return callback(time);} finally {globalThis.graphicsProbePainting=false;}
    });
    const getError=WebGL2RenderingContext.prototype.getError;
    WebGL2RenderingContext.prototype.getError=function(){
      const error=getError.call(this);
      // The diagnostic's EM_ASM query is distinct from renderer validation.
      if((new Error().stack||'').includes('runEmAsmFunction')) {
        ++queries[globalThis.graphicsProbePainting?'paint':'update'];
        if(queries.inject) {++queries.injected;return error||this.INVALID_ENUM;}
      }
      return error;
    };
  });
  await page.goto('/?renderer=webgl2&threads=serial&gl-errors=1');
  await waitForMainMenu(page);
  expect(await page.evaluate(()=>glob2Diagnostics.snapshot().renderContext.error)).toBe(0);
  const before=await page.evaluate(()=>{
    graphicsQueryProbe.paint=graphicsQueryProbe.update=0;
    return glob2Diagnostics.snapshot().loop;
  });
  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().loop)).toBeGreaterThan(before+10);
  await expect.poll(()=>page.evaluate(()=>graphicsQueryProbe.paint)).toBeGreaterThan(0);
  expect(await page.evaluate(()=>graphicsQueryProbe.update)).toBe(0);
  // Keep error collection observable and retain its sticky failure evidence.
  await page.evaluate(()=>{graphicsQueryProbe.inject=true;});
  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().renderContext.error)).toBe(0x0500);
  const paints=await page.evaluate(()=>{graphicsQueryProbe.inject=false;return graphicsQueryProbe.paint;});
  await expect.poll(()=>page.evaluate(()=>graphicsQueryProbe.paint)).toBeGreaterThan(paints+1);
  expect(await page.evaluate(()=>glob2Diagnostics.snapshot().renderContext.error)).toBe(0x0500);
  expect(await page.evaluate(()=>graphicsQueryProbe.injected)).toBeGreaterThan(0);
});
