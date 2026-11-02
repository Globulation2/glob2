const {test,expect}=require("/home/bradley/.codex/worktrees/307ed29a-915d-4451-b89a-dd866c5cb3af/glob2/browser/node_modules/@playwright/test");
test('compositor capture preserves clip coordinates and device scale for worker WebGL',async({page},info)=>{
 await page.setContent('<body style="margin:0;background:#fff"><canvas id="c" width="200" height="100" style="position:absolute;left:37px;top:53px;width:200px;height:100px"></canvas>');
 await page.evaluate(()=>new Promise((resolve,reject)=>{
  const worker=new Worker(URL.createObjectURL(new Blob([`onmessage=e=>{const c=e.data;const gl=c.getContext('webgl2');if(!gl){postMessage('unsupported');return;}gl.clearColor(0,0,1,1);gl.clear(gl.COLOR_BUFFER_BIT);gl.enable(gl.SCISSOR_TEST);gl.scissor(0,0,100,100);gl.clearColor(1,0,0,1);gl.clear(gl.COLOR_BUFFER_BIT);gl.flush();postMessage('ready');};`],{type:'application/javascript'})));
  worker.onmessage=e=>e.data==='ready'?resolve():reject(Error(e.data));worker.onerror=reject;const canvas=c.transferControlToOffscreen();worker.postMessage(canvas,[canvas]);
 }));
 const png=await page.screenshot({clip:{x:47,y:63,width:160,height:80},path:info.outputPath('clip.png')});
 const pixels=await page.evaluate(async b64=>{const bitmap=await createImageBitmap(await(await fetch('data:image/png;base64,'+b64)).blob());const c=new OffscreenCanvas(bitmap.width,bitmap.height),ctx=c.getContext('2d');ctx.drawImage(bitmap,0,0);const s=devicePixelRatio;return {width:c.width,height:c.height,scale:s,left:Array.from(ctx.getImageData(20*s,20*s,1,1).data),right:Array.from(ctx.getImageData(140*s,20*s,1,1).data)};},png.toString('base64'));
 expect(pixels.width).toBe(160*pixels.scale);expect(pixels.height).toBe(80*pixels.scale);expect(pixels.left).toEqual([255,0,0,255]);expect(pixels.right).toEqual([0,0,255,255]);
 const canvas=await page.locator('#c').screenshot({path:info.outputPath('canvas.png')});expect(canvas.length).toBeGreaterThan(100);
});
