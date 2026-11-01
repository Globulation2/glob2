import base from './playwright.config.mjs';
export default {...base,outputDir:new URL('./after-ui-fixes/test-results',import.meta.url).pathname,reporter:[['list'],['json',{outputFile:new URL('./after-ui-fixes/tests.json',import.meta.url).pathname}]],projects:base.projects.filter(p=>p.name.startsWith('new-')).map(p=>({...p,grep:/new skin model/}))};
