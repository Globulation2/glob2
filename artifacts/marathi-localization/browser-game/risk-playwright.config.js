const path=require('node:path');const base=require(path.resolve(__dirname,'../../../browser/playwright.config.js'));module.exports={...base,testDir:__dirname,testMatch:'risk-ui.spec.js',workers:1};
