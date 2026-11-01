export default {
 testDir:new URL('../../../platform/apps/web/e2e',import.meta.url).pathname,
 testMatch:'localized-layout.spec.ts',
 outputDir:new URL('./responsive-test-results',import.meta.url).pathname,
 workers:1,retries:0,timeout:240000,
 reporter:[['list'],['json',{outputFile:new URL('./responsive-tests.json',import.meta.url).pathname}]],
 use:{baseURL:'http://127.0.0.1:42769',trace:'retain-on-failure'},
 projects:[{name:'desktop',use:{viewport:{width:1280,height:900}}}]
};
