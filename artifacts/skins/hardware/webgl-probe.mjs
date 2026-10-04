import {chromium} from '../../../platform/node_modules/@playwright/test/index.mjs';
import {writeFile} from 'node:fs/promises';
const browser = await chromium.launch({headless:false, args:['--use-angle=gl','--enable-gpu','--disable-software-rasterizer']});
try {
  const page = await browser.newPage();
  const info = await page.evaluate(() => {
    const gl = document.createElement('canvas').getContext('webgl2');
    if (!gl) throw new Error('No WebGL2 context');
    const ext = gl.getExtension('WEBGL_debug_renderer_info');
    return {vendor:gl.getParameter(ext.UNMASKED_VENDOR_WEBGL),renderer:gl.getParameter(ext.UNMASKED_RENDERER_WEBGL),version:gl.getParameter(gl.VERSION)};
  });
  if (!info.renderer.includes('NVIDIA') || /SwiftShader|llvmpipe/.test(info.renderer)) throw new Error(JSON.stringify(info));
  await writeFile('artifacts/skins/hardware/webgl-info.json',JSON.stringify(info,null,2)+'\n');
  console.log(info);
} finally {await browser.close();}
