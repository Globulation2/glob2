const path=require('node:path');
const browser=path.resolve(__dirname,'../../../browser');
const config=require(path.join(browser,'playwright.config.js'));
module.exports={...config,testDir:path.join(browser,'tests'),projects:config.projects.map(project=>project.name==='chromium'?{
 ...project,use:{...project.use,launchOptions:{...project.use.launchOptions,args:[...(project.use.launchOptions?.args||[]),'--ozone-platform=x11']}}
}:project)};
