// SPDX-License-Identifier: GPL-3.0-or-later
const {test:base,expect}=require('@playwright/test');
const os=require('node:os'),path=require('node:path'),fs=require('node:fs');
// WebKit's ephemeral contexts disable OPFS; qualify its persistent browser mode.
exports.test=base.extend({page:async({page,browserName,playwright},use,info)=>{
 if(browserName!=='webkit') return use(page);
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'glob2-recording-webkit-'));
 const context=await playwright.webkit.launchPersistentContext(directory,{headless:true,viewport:info.project.use.viewport,baseURL:info.project.use.baseURL});
 try {await use(await context.newPage());}finally{await context.close();fs.rmSync(directory,{recursive:true,force:true});}
}});
exports.expect=expect;
