const e2e=new URL('../../../platform/apps/web/e2e',import.meta.url).pathname;
const common={baseURL:'http://127.0.0.1:42769',locale:'bg-BG',trace:'retain-on-failure'};
export default{
 workers:1,retries:0,timeout:240000,
 outputDir:new URL('./verified/test-results',import.meta.url).pathname,
 reporter:[['list'],['json',{outputFile:new URL('./verified/tests.json',import.meta.url).pathname}],['html',{outputFolder:new URL('./verified/html-report',import.meta.url).pathname,open:'never'}]],
 projects:[
  {name:'desktop',testDir:e2e,testMatch:['bulgarian.spec.ts','localized-layout.spec.ts'],use:{...common,viewport:{width:1280,height:900}}},
  {name:'phone',testDir:e2e,testMatch:['bulgarian.spec.ts','localized-layout.spec.ts'],use:{...common,viewport:{width:390,height:844},isMobile:true,hasTouch:true}},
  {name:'controls-desktop',testDir:import.meta.dirname,testMatch:'translated-controls.spec.mjs',use:{...common,viewport:{width:1280,height:900}}},
  {name:'controls-phone',testDir:import.meta.dirname,testMatch:'translated-controls.spec.mjs',use:{...common,viewport:{width:390,height:844},isMobile:true,hasTouch:true}},
 ]
};
