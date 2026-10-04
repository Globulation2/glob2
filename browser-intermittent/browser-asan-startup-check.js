const {chromium}=require('../../browser/node_modules/playwright');
(async()=>{
 const browser=await chromium.launch();const page=await browser.newPage({viewport:{width:1200,height:900}});
 const errors=[];page.on('pageerror',e=>errors.push(String(e)));page.on('console',m=>{if(/AddressSanitizer|ERROR:|SUMMARY:/.test(m.text())){errors.push(m.text());console.log(m.text());}});
 const start=Date.now();await page.goto('http://127.0.0.1:8776/?renderer=software');
 const timer=setInterval(async()=>console.log('ASan startup progress',Math.round((Date.now()-start)/1000),await page.evaluate(()=>({screen:Module.glob2Screen||'loading',loop:Module.glob2Loop||0,heap:HEAPU8.length})).catch(e=>String(e))),10000);
 try{await page.waitForFunction(()=>glob2Diagnostics.snapshot().screen.includes('MainMenuScreen'),{},{timeout:180000});console.log('ASan main menu ready',Date.now()-start,'ms','errors',JSON.stringify(errors));}
 catch(e){console.log('ASan startup did not complete',String(e),'errors',JSON.stringify(errors));process.exitCode=1;}
 finally{clearInterval(timer);await browser.close();}
})();
