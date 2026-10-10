const fs=require('node:fs'),path=require('node:path');
const {chromium}=require(path.resolve(__dirname,'../../../browser/node_modules/playwright'));
(async()=>{const args=['--use-angle=vulkan','--ozone-platform=x11','--enable-gpu','--enable-features=Vulkan'];
 const browser=await chromium.launch({channel:'chromium',headless:true,args});
 try {const page=await browser.newPage();const gpu=await page.evaluate(()=>{const gl=document.createElement('canvas').getContext('webgl2');if(!gl)throw new Error('WebGL2 unavailable');const ext=gl.getExtension('WEBGL_debug_renderer_info');return {renderer:gl.getParameter(ext.UNMASKED_RENDERER_WEBGL),vendor:gl.getParameter(ext.UNMASKED_VENDOR_WEBGL)};});
 if(!gpu.renderer.includes('NVIDIA')||/SwiftShader|llvmpipe/.test(gpu.renderer))throw new Error(JSON.stringify(gpu));
 const result={channel:'chromium',version:browser.version(),headless:true,args,...gpu};fs.writeFileSync(path.join(__dirname,'gpu-environment.json'),JSON.stringify(result,null,2)+'\n');console.log(result);
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
