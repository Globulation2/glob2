const e2e=new URL('../../../platform/apps/web/e2e',import.meta.url).pathname;
const common={baseURL:'http://127.0.0.1:42769',locale:'bg-BG',navigationTimeout:90000,actionTimeout:30000,trace:'retain-on-failure'};
export default{
 workers:1,retries:0,timeout:360000,expect:{timeout:20000},
 outputDir:new URL('./verified/test-results',import.meta.url).pathname,
 reporter:[['list'],['json',{outputFile:new URL('./verified/tests.json',import.meta.url).pathname}],['html',{outputFolder:new URL('./verified/html-report',import.meta.url).pathname,open:'never'}]],
 projects:[
  {name:'desktop',testDir:e2e,testMatch:['bulgarian.spec.ts','localized-layout.spec.ts'],use:{...common,viewport:{width:1280,height:900}}},
  {name:'phone',testDir:e2e,testMatch:['bulgarian.spec.ts','localized-layout.spec.ts'],use:{...common,viewport:{width:390,height:844},isMobile:true,hasTouch:true}},
  {name:'new-desktop',testDir:import.meta.dirname,testMatch:['new-domains.spec.mjs','new30.spec.mjs'],use:{...common,viewport:{width:1280,height:900}}},
  {name:'new-phone',testDir:import.meta.dirname,testMatch:['new-domains.spec.mjs','new30.spec.mjs'],use:{...common,viewport:{width:390,height:844},isMobile:true,hasTouch:true}},
  {name:'controls-desktop',testDir:import.meta.dirname,testMatch:['translated-controls.spec.mjs','context-wording-current.spec.mjs','sets-caption.spec.mjs','readonly-caption.spec.mjs'],use:{...common,viewport:{width:1280,height:900}}},
  {name:'controls-phone',testDir:import.meta.dirname,testMatch:['translated-controls.spec.mjs','context-wording-current.spec.mjs','sets-caption.spec.mjs','readonly-caption.spec.mjs'],use:{...common,viewport:{width:390,height:844},isMobile:true,hasTouch:true}},
 ]
};
