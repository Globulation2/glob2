// SPDX-License-Identifier: GPL-3.0-or-later
const {test} = require('@playwright/test');
// Probe browser APIs independently of the game's advertised backend choices.
// A timeout or worker error is a failure, never evidence of an unsupported API.
exports.presentationWebGL = async page => {
  if (!page.presentationWebGLProbe) page.presentationWebGLProbe = page.evaluate(async () => {
    if (glob2Diagnostics.snapshot().executionMode !== 'threaded') {
      const probe = document.createElement('canvas');
      const context = probe.getContext('webgl2');
      context?.getExtension('WEBGL_lose_context')?.loseContext();
      return Boolean(context);
    }
    const url = URL.createObjectURL(new Blob([`onmessage=()=>{
      try {
        const supported=typeof OffscreenCanvas==='function'&&!!new OffscreenCanvas(1,1).getContext('webgl2');
        postMessage({supported});
      } catch(error) { postMessage({error:String(error)}); }
    }`], {type:'text/javascript'}));
    let worker, timer;
    try {
      worker = new Worker(url);
      return await new Promise((resolve,reject) => {
        timer = setTimeout(() => reject(new Error('Presentation WebGL probe timed out')),5000);
        worker.onerror = event => reject(new Error('Presentation WebGL probe failed: '+event.message));
        worker.onmessage = event => event.data.error ? reject(new Error(event.data.error)) : resolve(event.data.supported);
        worker.postMessage(null);
      });
    } finally { clearTimeout(timer); worker?.terminate(); URL.revokeObjectURL(url); }
  });
  return page.presentationWebGLProbe;
};
exports.requirePresentationWebGL = async page => {
  const supported = await exports.presentationWebGL(page);
  test.info().annotations.push({type:'Presentation WebGL2',description:supported?'Independent owner-realm probe succeeded':'Independent owner-realm probe returned no WebGL2 context'});
  test.skip(!supported,'This presentation realm cannot create WebGL2; software and unavailable-backend contracts remain tested');
};
