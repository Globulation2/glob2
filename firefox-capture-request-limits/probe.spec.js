const {test,expect}=require("/home/bradley/.codex/worktrees/307ed29a-915d-4451-b89a-dd866c5cb3af/glob2/browser/node_modules/@playwright/test");
test('capture identifies the intended page when another page exists',async({page,context})=>{
 await page.setContent('<body style="background:red">first</body>');const second=await context.newPage();await second.setContent('<body style="background:blue">second</body>');
 try{const png=await page.screenshot();const pixel=await page.evaluate(async b64=>{const im=await createImageBitmap(await(await fetch('data:image/png;base64,'+b64)).blob());const c=new OffscreenCanvas(im.width,im.height),ctx=c.getContext('2d');ctx.drawImage(im,0,0);return Array.from(ctx.getImageData(500,400,1,1).data);},png.toString('base64'));expect(pixel).toEqual([255,0,0,255]);}finally{await second.close();}
 await page.bringToFront();expect((await page.screenshot()).length).toBeGreaterThan(100);
});
test('full document outside the viewport fails explicitly',async({page})=>{
 await page.setContent('<body style="height:2000px;background:red">tall</body>');await expect(page.screenshot({fullPage:true})).rejects.toThrow(/visible viewport clip/);
});
