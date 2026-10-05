const path=require('node:path');
const root=path.resolve(__dirname,'../../..');
const config=require(path.join(root,'browser/playwright.config.js'));
module.exports={...config,testDir:path.join(root,'browser/tests'),outputDir:path.join(__dirname,'browser-results'),reporter:[['list']],workers:2,use:{...config.use,baseURL:'http://127.0.0.1:8798'},webServer:{command:'python3 artifacts/audio/half-second/serve.py',cwd:root,url:'http://127.0.0.1:8798/music-output.js',reuseExistingServer:false}};
